use std::path::{Path, PathBuf};

use anyhow::{Context, Result};
use maho_agent::omo::config::{BundledRuntime, OmoLaunchConfig};
use maho_agent::omo::factory::{agent_dir_for_workspace, resolve_rpc_socket_path};
use maho_agent::omo::session_control::{self, SessionControlError, SessionVerb};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SessionAction {
    Status,
    Steer,
    Queue,
    Stop,
}

impl SessionAction {
    fn verb(self) -> SessionVerb {
        match self {
            SessionAction::Status => SessionVerb::Status,
            SessionAction::Steer => SessionVerb::Steer,
            SessionAction::Queue => SessionVerb::Queue,
            SessionAction::Stop => SessionVerb::Stop,
        }
    }

    fn name(self) -> &'static str {
        match self {
            SessionAction::Status => "status",
            SessionAction::Steer => "steer",
            SessionAction::Queue => "queue",
            SessionAction::Stop => "stop",
        }
    }
}

pub async fn resolve_workspace_root(workspace: Option<&str>) -> Result<PathBuf> {
    let db_path = maho_cli::workspace::default_db_path();
    let resolved = maho_cli::workspace::resolve_workspace_id(&db_path, workspace).await?;
    let storage = maho_cli::workspace::open_storage(&db_path)
        .map_err(|e| anyhow::anyhow!("Failed to open database: {e}"))?;
    let root = storage
        .list_workspaces()
        .map_err(|e| anyhow::anyhow!("Failed to list workspaces: {e}"))?
        .into_iter()
        .find(|w| w.id == resolved)
        .and_then(|w| w.workspace_root.map(PathBuf::from));
    Ok(root.unwrap_or_else(|| std::env::current_dir().unwrap_or_else(|_| PathBuf::from("."))))
}

fn bundled_runtime() -> Result<BundledRuntime> {
    let exe_dir = std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(Path::to_path_buf))
        .unwrap_or_else(|| PathBuf::from("/Applications/Maho.app/Contents/Helpers"));
    let mut bundled = BundledRuntime::from_executable_dir(&exe_dir);
    if !bundled.missing().is_empty() {
        let app_helpers = PathBuf::from("/Applications/Maho.app/Contents/Helpers");
        if app_helpers.exists() {
            bundled = BundledRuntime::from_executable_dir(app_helpers);
        }
    }
    if !bundled.runtime_binary.exists() || !bundled.rpc_entry.exists() {
        anyhow::bail!(
            "Omo bundled runtime missing (bun {} / rpc entry {})",
            bundled.runtime_binary.display(),
            bundled.rpc_entry.display()
        );
    }
    Ok(bundled)
}

pub fn launch_config(workspace_root: &Path, bundled: &BundledRuntime) -> Result<OmoLaunchConfig> {
    let agent_dir = agent_dir_for_workspace(workspace_root);
    let socket = resolve_rpc_socket_path(&agent_dir);
    OmoLaunchConfig::builder(&agent_dir)
        .runtime_binary(&bundled.runtime_binary)
        .rpc_entry(&bundled.rpc_entry)
        .maybe_plugin_dir(bundled.plugin_dir.as_deref())
        .socket_path(&socket)
        .build()
        .context("invalid omo launch config")
}

pub async fn run(
    action: SessionAction,
    session_id: &str,
    text: Option<String>,
    workspace: Option<&str>,
    root: Option<PathBuf>,
    json: bool,
) -> Result<()> {
    if matches!(action, SessionAction::Steer | SessionAction::Queue)
        && text.as_deref().is_none_or(|t| t.trim().is_empty())
    {
        anyhow::bail!("{} needs a non-empty message", action.name());
    }
    let workspace_root = match root {
        Some(root) => root,
        None => resolve_workspace_root(workspace).await?,
    };
    let config = launch_config(&workspace_root, &bundled_runtime()?)?;
    let outcome = session_control::control(&config, session_id, action.verb(), text.as_deref()).await;
    match outcome {
        Ok(outcome) => {
            if json {
                println!(
                    "{}",
                    serde_json::json!({
                        "action": action.name(),
                        "sessionId": session_id,
                        "ok": true,
                        "result": outcome.payload,
                    })
                );
            } else {
                print_human(action, session_id, &outcome.payload);
            }
            Ok(())
        }
        Err(error) => {
            if json {
                println!(
                    "{}",
                    serde_json::json!({
                        "action": action.name(),
                        "sessionId": session_id,
                        "ok": false,
                        "error": error_code(&error),
                        "message": error.to_string(),
                    })
                );
            }
            Err(error.into())
        }
    }
}

fn error_code(error: &SessionControlError) -> &'static str {
    match error {
        SessionControlError::NotRunning(_) => "not_running",
        SessionControlError::SessionNotFound(_) => "session_not_found",
        SessionControlError::Busy(_) => "busy",
        SessionControlError::ClientMissing(_) => "client_missing",
        SessionControlError::Refused { .. } => "refused",
        SessionControlError::Failed(_) => "failed",
    }
}

fn print_human(action: SessionAction, session_id: &str, payload: &serde_json::Value) {
    match action {
        SessionAction::Status => {
            let state = &payload["state"];
            println!(
                "{session_id}: {} (pending {}, model {})",
                if state["isStreaming"].as_bool() == Some(true) {
                    "running"
                } else {
                    "idle"
                },
                state["pendingMessageCount"].as_u64().unwrap_or(0),
                state["model"]["id"].as_str().unwrap_or("?")
            );
        }
        SessionAction::Steer | SessionAction::Queue => {
            if payload["streaming"].as_bool() == Some(true) {
                println!("{session_id}: message delivered to the running turn");
            } else {
                println!("{session_id}: session idle; message queued for the next turn");
            }
        }
        SessionAction::Stop => {
            if payload["aborted"].as_bool() == Some(true) {
                println!("{session_id}: running turn stopped");
            } else {
                println!("{session_id}: no turn was running");
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn launch_config_targets_the_workspace_agent_socket() {
        let bundled = BundledRuntime {
            runtime_binary: PathBuf::from("/b/bun"),
            rpc_entry: PathBuf::from("/b/omo/dist/rpc-entry.js"),
            browser_mcp_binary: PathBuf::from("/b/maho-browser-mcp"),
            plugin_dir: None,
        };
        let root = Path::new("/tmp/maho-ws-session");
        let config = launch_config(root, &bundled).unwrap();
        let agent_dir = agent_dir_for_workspace(root);
        assert!(config.socket_path().starts_with(&agent_dir) || config.socket_path().starts_with("/tmp"));
        assert_eq!(
            session_control::session_file(&config, "task-1"),
            agent_dir.join("sessions").join("task-1.jsonl")
        );
    }

    #[test]
    fn error_codes_are_stable() {
        assert_eq!(
            error_code(&SessionControlError::SessionNotFound("x".into())),
            "session_not_found"
        );
        assert_eq!(
            error_code(&SessionControlError::NotRunning(PathBuf::from("/s"))),
            "not_running"
        );
    }
}
