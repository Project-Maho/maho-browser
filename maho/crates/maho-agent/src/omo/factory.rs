use std::path::{Path, PathBuf};
use std::sync::Arc;

#[cfg(unix)]
use crate::omo::backend::OmoBackend;
use crate::omo::config::{BundledRuntime, OmoLaunchConfig};
use crate::{AgentRuntime, AgentStorage, PermissionCallback};

/// Derived from Maho's workspace so a developer's `~/.omo` or `~/.senpi` is never used.
pub fn agent_dir_for_workspace(workspace_root: &Path) -> PathBuf {
    if workspace_root == Path::new("/") || workspace_root.as_os_str().is_empty() {
        if let Ok(home) = std::env::var("HOME") {
            return PathBuf::from(home).join(".maho").join("omo-agent");
        }
    }
    workspace_root.join(".maho").join("omo-agent")
}

/// Computes a UDS socket path that never exceeds Unix domain socket length limits
/// (SUN_LEN = 104 bytes on macOS, 108 bytes on Linux). When agent_dir is too deep
/// in the filesystem hierarchy, falls back to `/tmp`.
pub fn resolve_rpc_socket_path(agent_dir: &Path) -> PathBuf {
    let simple_str = uuid::Uuid::new_v4().simple().to_string();
    let socket_id = format!("m_rpc_{}.sock", &simple_str[..16]);
    let candidate = agent_dir.join(&socket_id);
    if candidate.as_os_str().len() < 90 {
        candidate
    } else {
        PathBuf::from("/tmp").join(&socket_id)
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum OmoUnavailable {
    ExecutableDirUnknown,
    MissingArtifacts(Vec<PathBuf>),
    Config(String),
}

#[cfg(unix)]
pub fn omo_runtime_from_bundle(
    executable_dir: &Path,
    workspace_root: &Path,
) -> Result<OmoBackend, OmoUnavailable> {
    let bundled = BundledRuntime::from_executable_dir(executable_dir);
    let missing = bundled.missing();
    if !missing.is_empty() {
        return Err(OmoUnavailable::MissingArtifacts(
            missing.into_iter().map(Path::to_path_buf).collect(),
        ));
    }

    let agent_dir = agent_dir_for_workspace(workspace_root);
    let socket_path = resolve_rpc_socket_path(&agent_dir);
    let config = OmoLaunchConfig::builder(&agent_dir)
        .runtime_binary(&bundled.runtime_binary)
        .rpc_entry(&bundled.rpc_entry)
        .socket_path(&socket_path)
        .add_cleanup_path(&socket_path)
        .build()
        .map_err(|error| OmoUnavailable::Config(error.to_string()))?;

    Ok(OmoBackend::new(
        config,
        bundled.browser_mcp_binary,
        workspace_root,
    ))
}

/// Creates the canonical Omo agent runtime instance.
pub fn create_agent_runtime(
    storage: Arc<dyn AgentStorage>,
    permission_callback: Option<PermissionCallback>,
    workspace_root: PathBuf,
    _allow_insecure_key_storage: bool,
) -> Arc<dyn AgentRuntime> {
    #[cfg(unix)]
    {
        let executable_dir = std::env::current_exe()
            .ok()
            .and_then(|exe| exe.parent().map(Path::to_path_buf))
            .unwrap_or_else(|| PathBuf::from("/Applications/Maho.app/Contents/Helpers"));

        if let Ok(backend) = omo_runtime_from_bundle(&executable_dir, &workspace_root) {
            backend.set_storage(storage.clone());
            if let Some(callback) = permission_callback {
                let _ = backend.set_permission_callback(callback);
            }
            return Arc::new(backend);
        }

        let fallback_dirs = [
            PathBuf::from("/Applications/Maho.app/Contents/Helpers"),
            PathBuf::from("/usr/local/bin"),
            workspace_root.join("target/debug"),
        ];
        for dir in &fallback_dirs {
            if let Ok(backend) = omo_runtime_from_bundle(dir, &workspace_root) {
                backend.set_storage(storage.clone());
                if let Some(callback) = permission_callback {
                    let _ = backend.set_permission_callback(callback);
                }
                return Arc::new(backend);
            }
        }
    }

    #[cfg(unix)]
    {
        let agent_dir = agent_dir_for_workspace(&workspace_root);
        let socket_path = resolve_rpc_socket_path(&agent_dir);
        let bundled = BundledRuntime::from_executable_dir(&workspace_root);
        let config = OmoLaunchConfig::builder(&agent_dir)
            .runtime_binary(&bundled.runtime_binary)
            .rpc_entry(&bundled.rpc_entry)
            .socket_path(&socket_path)
            .add_cleanup_path(&socket_path)
            .build()
            .unwrap_or_else(|_| OmoLaunchConfig::builder(&agent_dir).build().unwrap());

        let backend = OmoBackend::new(
            config,
            bundled.browser_mcp_binary,
            &workspace_root,
        );
        backend.set_storage(storage);
        Arc::new(backend)
    }

    #[cfg(not(unix))]
    {
        // The Omo backend spawns a Unix-domain-socket RPC runtime that has no Windows
        // implementation yet. Fail every turn with that reason rather than answering
        // with fabricated text, so the surface shows the real state.
        Arc::new(crate::UnavailableAgentRuntime::new(
            "The Maho agent runtime is not available on Windows yet.",
        ))
    }
}

#[cfg(all(test, unix))]
mod tests {
    use super::*;

    #[test]
    fn agent_dir_stays_inside_maho_and_never_touches_a_user_install() {
        let dir = agent_dir_for_workspace(Path::new("/Users/someone/MahoWorkspace"));

        assert_eq!(
            dir,
            PathBuf::from("/Users/someone/MahoWorkspace/.maho/omo-agent")
        );
        let text = dir.to_string_lossy();
        assert!(!text.contains("/.omo"), "{text}");
        assert!(!text.contains(".senpi"), "{text}");
    }

    #[test]
    fn agent_dir_falls_back_when_workspace_root_is_root_filesystem() {
        let dir = agent_dir_for_workspace(Path::new("/"));
        assert_ne!(dir, PathBuf::from("/.maho/omo-agent"));
        assert!(dir.ends_with(".maho/omo-agent"));
        let text = dir.to_string_lossy();
        assert!(!text.contains("/.omo"), "{text}");
        assert!(!text.contains(".senpi"), "{text}");
    }

    #[test]
    fn rpc_socket_path_never_exceeds_sun_len() {
        let long_agent_dir = PathBuf::from(
            "/Users/someone/Library/Application Support/Maho-Run/MahoCore/.maho/omo-agent",
        );
        let path = resolve_rpc_socket_path(&long_agent_dir);
        assert!(
            path.as_os_str().len() < 100,
            "socket path must be shorter than SUN_LEN (104 on macOS): {}",
            path.display()
        );
        assert!(path.starts_with("/tmp"));

        let short_agent_dir = PathBuf::from("/tmp/maho");
        let path_short = resolve_rpc_socket_path(&short_agent_dir);
        assert!(
            path_short.as_os_str().len() < 100,
            "short agent_dir socket path must also be shorter than SUN_LEN: {}",
            path_short.display()
        );
        assert!(path_short.starts_with("/tmp/maho"));
    }

    #[test]
    fn missing_staged_artifacts_are_reported_rather_than_guessed() {
        let result = omo_runtime_from_bundle(
            Path::new("/nonexistent/Maho.app/Contents/MacOS"),
            Path::new("/tmp/maho-workspace"),
        );

        match result {
            Err(OmoUnavailable::MissingArtifacts(paths)) => assert_eq!(paths.len(), 3),
            Err(other) => panic!("expected MissingArtifacts, got {other:?}"),
            Ok(_) => panic!("an unstaged build must not produce an omo runtime"),
        }
    }

    #[test]
    fn a_fully_staged_directory_builds_an_omo_runtime() {
        let staged = tempfile::tempdir().expect("scratch directory");
        let dir = staged.path();
        std::fs::create_dir_all(dir.join("omo").join("dist")).expect("dist dir");
        for file in ["bun", "maho-browser-mcp"] {
            std::fs::write(dir.join(file), b"#!/bin/sh\n").expect("stage binary");
        }
        std::fs::write(dir.join("omo").join("dist").join("rpc-entry.js"), b"//\n")
            .expect("stage rpc entry");

        let workspace = tempfile::tempdir().expect("workspace");
        match omo_runtime_from_bundle(dir, workspace.path()) {
            Ok(_) => {}
            Err(error) => panic!("a fully staged directory must build a runtime: {error:?}"),
        }
    }
    #[test]
    fn the_omo_branch_installs_mahos_permission_callback() {
        let staged = tempfile::tempdir().expect("scratch directory");
        let dir = staged.path();
        std::fs::create_dir_all(dir.join("omo").join("dist")).expect("dist dir");
        for file in ["bun", "maho-browser-mcp"] {
            std::fs::write(dir.join(file), b"#!/bin/sh\n").expect("stage binary");
        }
        std::fs::write(dir.join("omo").join("dist").join("rpc-entry.js"), b"//\n")
            .expect("stage rpc entry");
        let workspace = tempfile::tempdir().expect("workspace");

        let backend = match omo_runtime_from_bundle(dir, workspace.path()) {
            Ok(backend) => backend,
            Err(error) => panic!("staged directory must build a runtime: {error:?}"),
        };

        let invoked = Arc::new(std::sync::atomic::AtomicBool::new(false));
        let invoked_clone = Arc::clone(&invoked);
        let installed = backend.set_permission_callback(Box::new(move |_request| {
            invoked_clone.store(true, std::sync::atomic::Ordering::SeqCst);
            Box::pin(async { crate::PermissionDecision::Deny })
        }));

        assert!(
            installed.is_ok(),
            "Maho's approval gate must reach the omo runtime, not be dropped on the floor"
        );

        let cb_guard = backend.permission_callback.lock().unwrap();
        let cb = cb_guard.as_ref().expect("permission callback must be stored");
        let req = crate::PermissionRequest::new(
            "mail_send",
            "{}",
            crate::ToolSensitivity::Sensitive,
        );
        let decision = futures::executor::block_on(cb(req));
        assert_eq!(decision, crate::PermissionDecision::Deny);
        assert!(invoked.load(std::sync::atomic::Ordering::SeqCst));
    }
}
