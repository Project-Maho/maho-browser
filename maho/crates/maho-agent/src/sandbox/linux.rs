//! Linux Landlock and narrowly-scoped seccomp process confinement.

use super::{PreparedSandbox, SandboxBackend, SandboxError, SandboxPolicy};
use landlock::{
    Access, AccessFs, CompatLevel, Compatible, PathBeneath, PathFd, Ruleset, RulesetAttr,
    RulesetCreatedAttr,
};
use seccompiler::{
    BackendError, BpfProgram, SeccompAction, SeccompCmpArgLen, SeccompCmpOp, SeccompCondition,
    SeccompFilter, SeccompRule,
};
use std::collections::BTreeMap;
use std::convert::TryInto;
use std::path::{Path, PathBuf};

pub struct LinuxSandbox;

impl SandboxBackend for LinuxSandbox {
    fn prepare(&self, policy: &SandboxPolicy) -> Result<PreparedSandbox, SandboxError> {
        #[cfg(feature = "debug-test-hooks")]
        if std::env::var_os("MAHO_SANDBOX_FORCE_UNAVAILABLE").as_deref()
            == Some(std::ffi::OsStr::new("1"))
        {
            return Err(SandboxError::Unavailable(
                "forced by MAHO_SANDBOX_FORCE_UNAVAILABLE".into(),
            ));
        }
        validate_policy(policy)?;
        for path in policy
            .writable_roots
            .iter()
            .chain(policy.readable_roots.iter())
        {
            if !path.is_absolute() {
                return Err(SandboxError::Prepare(format!(
                    "sandbox path is not absolute: {}",
                    path.display()
                )));
            }
        }
        Ok(PreparedSandbox {
            policy: policy.clone(),
        })
    }
}

fn validate_policy(policy: &SandboxPolicy) -> Result<(), SandboxError> {
    if policy.writable_roots.iter().any(|root| {
        policy
            .readable_roots
            .iter()
            .any(|read| read.starts_with(root))
    }) {
        return Err(SandboxError::Prepare(
            "readable root is nested under a writable root".into(),
        ));
    }
    Ok(())
}

fn canonical_existing(path: &Path) -> Result<PathBuf, SandboxError> {
    std::fs::canonicalize(path).map_err(|error| {
        SandboxError::Prepare(format!("cannot canonicalize {}: {error}", path.display()))
    })
}

/// Apply policy after fork and before exec. Landlock restrictions are inherited by descendants.
pub(crate) fn apply_policy(policy: &SandboxPolicy) -> Result<(), SandboxError> {
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
    if denied.len()
        != policy
            .denied_paths
            .iter()
            .filter(|path| path.exists())
            .count()
    {
        return Err(SandboxError::Prepare("failed resolving denied path".into()));
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

    let abi = landlock::ABI::V1;
    let handled = AccessFs::from_all(abi);
    let mut ruleset = Ruleset::default()
        .handle_access(handled)
        .map_err(|error| SandboxError::Unavailable(error.to_string()))?
        .set_compatibility(CompatLevel::HardRequirement)
        .create()
        .map_err(|error| SandboxError::Unavailable(error.to_string()))?;
    for system_root in ["/usr", "/bin", "/sbin", "/lib", "/lib64", "/etc"] {
        let path = Path::new(system_root);
        if path.exists() {
            let hierarchy =
                PathFd::new(path).map_err(|error| SandboxError::Prepare(error.to_string()))?;
            ruleset = ruleset
                .add_rule(PathBeneath::new(hierarchy, AccessFs::from_read(abi)))
                .map_err(|error| SandboxError::Prepare(error.to_string()))?;
        }
    }
    for root in readable {
        let hierarchy =
            PathFd::new(&root).map_err(|error| SandboxError::Prepare(error.to_string()))?;
        ruleset = ruleset
            .add_rule(PathBeneath::new(hierarchy, AccessFs::from_read(abi)))
            .map_err(|error| SandboxError::Prepare(error.to_string()))?;
    }
    // Shells redirect to /dev/null constantly ("2>/dev/null"); without write
    // access every such command fails. Only the null device itself is
    // writable, never the rest of /dev.
    let dev_null =
        PathFd::new("/dev/null").map_err(|error| SandboxError::Prepare(error.to_string()))?;
    ruleset = ruleset
        .add_rule(PathBeneath::new(dev_null, AccessFs::from_file(abi)))
        .map_err(|error| SandboxError::Prepare(error.to_string()))?;
    for root in writable {
        let hierarchy =
            PathFd::new(&root).map_err(|error| SandboxError::Prepare(error.to_string()))?;
        ruleset = ruleset
            .add_rule(PathBeneath::new(hierarchy, AccessFs::from_all(abi)))
            .map_err(|error| SandboxError::Prepare(error.to_string()))?;
    }
    let _status = ruleset
        .restrict_self()
        .map_err(|error| SandboxError::Unavailable(error.to_string()))?;

    if !policy.network {
        install_network_filter()?;
    }
    Ok(())
}

fn install_network_filter() -> Result<(), SandboxError> {
    let denied_family = |family: u64| {
        let condition = SeccompCondition::new(0, SeccompCmpArgLen::Dword, SeccompCmpOp::Eq, family)
            .map_err(|error| SandboxError::Prepare(error.to_string()))?;
        SeccompRule::new(vec![condition]).map_err(|error| SandboxError::Prepare(error.to_string()))
    };
    let mut rules = BTreeMap::new();
    rules.insert(
        libc::SYS_socket as i64,
        vec![
            denied_family(libc::AF_INET as u64)?,
            denied_family(libc::AF_INET6 as u64)?,
        ],
    );
    let filter = SeccompFilter::new(
        rules,
        SeccompAction::Allow,
        SeccompAction::Errno(libc::EACCES as u32),
        std::env::consts::ARCH
            .try_into()
            .map_err(|error: BackendError| SandboxError::Prepare(error.to_string()))?,
    )
    .map_err(|error| SandboxError::Prepare(error.to_string()))?;
    let program: BpfProgram = filter
        .try_into()
        .map_err(|error: BackendError| SandboxError::Prepare(error.to_string()))?;
    seccompiler::apply_filter(&program).map_err(|error| SandboxError::Prepare(error.to_string()))
}
