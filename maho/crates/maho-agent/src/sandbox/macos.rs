//! macOS Seatbelt confinement: commands run under `/usr/bin/sandbox-exec -p`
//! with a profile generated from the policy, inherited by every descendant.

use super::{PreparedSandbox, SandboxBackend, SandboxError, SandboxPolicy};
use std::path::{Path, PathBuf};

pub(crate) const SANDBOX_EXEC: &str = "/usr/bin/sandbox-exec";

const SYSTEM_READ_ROOTS: &[&str] = &[
    "/usr",
    "/bin",
    "/sbin",
    "/System",
    "/Library/Apple",
    "/private/etc",
    "/private/var/db/timezone",
    "/private/var/db/dyld",
    "/dev",
];

pub struct MacSandbox;

impl SandboxBackend for MacSandbox {
    fn prepare(&self, policy: &SandboxPolicy) -> Result<PreparedSandbox, SandboxError> {
        #[cfg(feature = "debug-test-hooks")]
        if std::env::var_os("MAHO_SANDBOX_FORCE_UNAVAILABLE").as_deref()
            == Some(std::ffi::OsStr::new("1"))
        {
            return Err(SandboxError::Unavailable(
                "forced by MAHO_SANDBOX_FORCE_UNAVAILABLE".into(),
            ));
        }
        if !Path::new(SANDBOX_EXEC).is_file() {
            return Err(SandboxError::Unavailable(format!(
                "{SANDBOX_EXEC} not found"
            )));
        }
        let profile = build_profile(policy)?;
        Ok(PreparedSandbox { profile })
    }
}

fn canonical_existing(path: &Path) -> Result<PathBuf, SandboxError> {
    if !path.is_absolute() {
        return Err(SandboxError::Prepare(format!(
            "sandbox path is not absolute: {}",
            path.display()
        )));
    }
    std::fs::canonicalize(path).map_err(|error| {
        SandboxError::Prepare(format!("cannot canonicalize {}: {error}", path.display()))
    })
}

/// SBPL string literal. Control characters are rejected rather than escaped so
/// a crafted path can never close the literal and inject a rule.
fn sbpl_string(path: &Path) -> Result<String, SandboxError> {
    let text = path.to_str().ok_or_else(|| {
        SandboxError::Prepare(format!("sandbox path is not UTF-8: {}", path.display()))
    })?;
    if text.chars().any(char::is_control) {
        return Err(SandboxError::Prepare(format!(
            "sandbox path contains control characters: {}",
            path.display()
        )));
    }
    let mut quoted = String::with_capacity(text.len() + 2);
    quoted.push('"');
    for ch in text.chars() {
        if ch == '"' || ch == '\\' {
            quoted.push('\\');
        }
        quoted.push(ch);
    }
    quoted.push('"');
    Ok(quoted)
}

pub(crate) fn build_profile(policy: &SandboxPolicy) -> Result<String, SandboxError> {
    let writable: Vec<_> = policy
        .writable_roots
        .iter()
        .map(|path| canonical_existing(path))
        .collect::<Result<_, _>>()?;
    let readable: Vec<_> = policy
        .readable_roots
        .iter()
        .map(|path| canonical_existing(path))
        .collect::<Result<_, _>>()?;
    let denied: Vec<_> = policy
        .denied_paths
        .iter()
        .filter(|path| path.exists())
        .map(|path| canonical_existing(path))
        .collect::<Result<_, _>>()?;
    if writable
        .iter()
        .any(|root| readable.iter().any(|read| read.starts_with(root)))
    {
        return Err(SandboxError::Prepare(
            "readable root is nested under a writable root".into(),
        ));
    }
    if denied.iter().any(|path| {
        writable
            .iter()
            .chain(readable.iter())
            .any(|root| path.starts_with(root) || root.starts_with(path))
    }) {
        return Err(SandboxError::Prepare(
            "denied path overlaps an allowed root".into(),
        ));
    }

    let mut profile = String::from(
        "(version 1)\n\
         (deny default)\n\
         (allow process-exec)\n\
         (allow process-fork)\n\
         (allow signal (target same-sandbox))\n\
         (allow process-info* (target same-sandbox))\n\
         (allow sysctl-read)\n\
         (allow file-read-metadata)\n",
    );
    let mut read_rule = String::from("(allow file-read*");
    for root in SYSTEM_READ_ROOTS {
        read_rule.push_str(&format!(" (subpath {})", sbpl_string(Path::new(root))?));
    }
    for root in readable.iter().chain(writable.iter()) {
        read_rule.push_str(&format!(" (subpath {})", sbpl_string(root)?));
    }
    read_rule.push_str(")\n");
    profile.push_str(&read_rule);

    let mut write_rule = String::from("(allow file-write* (literal \"/dev/null\")");
    for root in &writable {
        write_rule.push_str(&format!(" (subpath {})", sbpl_string(root)?));
    }
    write_rule.push_str(")\n");
    profile.push_str(&write_rule);

    for path in &denied {
        let quoted = sbpl_string(path)?;
        profile.push_str(&format!(
            "(deny file-read* file-write* (subpath {quoted}))\n"
        ));
    }
    if policy.network {
        profile.push_str("(allow network*)\n");
    }
    Ok(profile)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn profile_quotes_paths_and_denies_by_default() {
        let temp = tempfile::tempdir().unwrap();
        let profile = build_profile(&SandboxPolicy {
            writable_roots: vec![temp.path().to_path_buf()],
            ..SandboxPolicy::default()
        })
        .unwrap();
        assert!(profile.starts_with("(version 1)\n(deny default)\n"));
        assert!(!profile.contains("network"));
        let canonical = std::fs::canonicalize(temp.path()).unwrap();
        assert!(profile.contains(&format!("(subpath \"{}\")", canonical.display())));
    }

    #[test]
    fn quote_and_backslash_are_escaped_and_control_chars_rejected() {
        assert_eq!(
            sbpl_string(Path::new("/tmp/a\"b\\c")).unwrap(),
            "\"/tmp/a\\\"b\\\\c\""
        );
        assert!(sbpl_string(Path::new("/tmp/a\nb")).is_err());
    }

    #[test]
    fn denied_path_inside_writable_root_is_rejected() {
        let temp = tempfile::tempdir().unwrap();
        let inner = temp.path().join("secret");
        std::fs::create_dir(&inner).unwrap();
        let result = build_profile(&SandboxPolicy {
            writable_roots: vec![temp.path().to_path_buf()],
            denied_paths: vec![inner],
            ..SandboxPolicy::default()
        });
        assert!(result.is_err());
    }

    #[test]
    fn network_rule_only_when_policy_allows_it() {
        let temp = tempfile::tempdir().unwrap();
        let profile = build_profile(&SandboxPolicy {
            writable_roots: vec![temp.path().to_path_buf()],
            network: true,
            ..SandboxPolicy::default()
        })
        .unwrap();
        assert!(profile.contains("(allow network*)"));
    }
}
