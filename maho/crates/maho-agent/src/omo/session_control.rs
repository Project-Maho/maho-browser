//! Live control of a running Maho agent session (status / steer / queue / stop) from a
//! second process, through the omo engine's own session client (`senpi host session`,
//! shipped in the bundle beside `rpc-entry.js`). That client attaches to the shared RPC
//! host by session ref, resumes a parked session from its transcript, and classifies the
//! outcome by exit code, so Maho needs no RPC client of its own for this.

use std::path::{Path, PathBuf};
use std::process::Stdio;

use super::config::OmoLaunchConfig;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SessionVerb {
    Status,
    Steer,
    Queue,
    Stop,
}

#[derive(Debug, thiserror::Error)]
pub enum SessionControlError {
    #[error("the Maho agent runtime is not running (no omo host answers {0})")]
    NotRunning(PathBuf),
    #[error("no live or resumable agent session {0}")]
    SessionNotFound(String),
    #[error("the agent session is busy: {0}")]
    Busy(String),
    #[error("omo session client is missing at {0}")]
    ClientMissing(PathBuf),
    #[error("omo session client refused: {reason} ({detail})")]
    Refused { reason: String, detail: String },
    #[error("omo session client failed: {0}")]
    Failed(String),
}

#[derive(Debug, Clone, PartialEq)]
pub struct SessionOutcome {
    pub verb: SessionVerb,
    pub payload: serde_json::Value,
}

impl SessionOutcome {
    pub fn is_streaming(&self) -> Option<bool> {
        self.payload["state"]["isStreaming"]
            .as_bool()
            .or_else(|| self.payload["streaming"].as_bool())
    }
}

pub fn session_client_entry(rpc_entry: &Path) -> PathBuf {
    rpc_entry
        .parent()
        .map(|dist| dist.join("bundle").join("cli.js"))
        .unwrap_or_else(|| PathBuf::from("bundle/cli.js"))
}

pub fn session_file(config: &OmoLaunchConfig, maho_session_id: &str) -> PathBuf {
    config.session_dir().join(format!("{maho_session_id}.jsonl"))
}

pub fn socket_pointer_file(session_file: &Path) -> PathBuf {
    session_file.with_extension("rpc")
}

/// The socket of the host that opened the session, falling back to `config`'s own.
pub fn session_socket(config: &OmoLaunchConfig, session_file: &Path) -> PathBuf {
    std::fs::read_to_string(socket_pointer_file(session_file))
        .ok()
        .map(|text| PathBuf::from(text.trim()))
        .filter(|path| path.is_absolute())
        .unwrap_or_else(|| config.socket_path().to_path_buf())
}

pub fn session_client_argv(
    entry: &Path,
    verb: SessionVerb,
    session_ref: &Path,
    text: Option<&str>,
    socket: &Path,
) -> Vec<String> {
    let subcommand = match verb {
        SessionVerb::Status => "state",
        SessionVerb::Steer | SessionVerb::Queue => "steer",
        SessionVerb::Stop => "abort",
    };
    let mut argv = vec![
        entry.display().to_string(),
        "host".to_string(),
        "session".to_string(),
        subcommand.to_string(),
        session_ref.display().to_string(),
    ];
    if let Some(text) = text {
        argv.push(text.to_string());
    }
    argv.extend([
        "--socket".to_string(),
        socket.display().to_string(),
        "--json".to_string(),
    ]);
    argv
}

/// Maps the client's one JSON line and exit code (0 done, 1 failed, 2 usage, 3 refused).
pub fn classify(
    verb: SessionVerb,
    session_id: &str,
    socket: &Path,
    exit_code: Option<i32>,
    stdout: &str,
    stderr: &str,
) -> Result<SessionOutcome, SessionControlError> {
    let payload = stdout
        .lines()
        .find_map(|line| serde_json::from_str::<serde_json::Value>(line.trim()).ok())
        .unwrap_or(serde_json::Value::Null);
    let reason = payload["reason"].as_str().unwrap_or_default().to_string();
    let detail = payload["detail"].as_str().unwrap_or_default().to_string();
    match exit_code {
        Some(0) => Ok(SessionOutcome { verb, payload }),
        Some(3) => Err(match reason.as_str() {
            "host_unavailable" => SessionControlError::NotRunning(socket.to_path_buf()),
            "unknown_session" | "ambiguous_session" => {
                SessionControlError::SessionNotFound(session_id.to_string())
            }
            "busy" => SessionControlError::Busy(detail),
            _ => SessionControlError::Refused { reason, detail },
        }),
        _ => Err(SessionControlError::Failed(if detail.is_empty() {
            stderr.trim().to_string()
        } else {
            format!("{reason}: {detail}")
        })),
    }
}

/// Runs one verb against the session `maho_session_id` of the host `config` describes.
/// `Queue` and `Steer` both use omo's steer: mid-turn it joins the running turn, on an
/// idle session it is parked and delivered with the next prompt.
pub async fn control(
    config: &OmoLaunchConfig,
    maho_session_id: &str,
    verb: SessionVerb,
    text: Option<&str>,
) -> Result<SessionOutcome, SessionControlError> {
    let entry = session_client_entry(config.rpc_entry());
    if !entry.is_file() {
        return Err(SessionControlError::ClientMissing(entry));
    }
    let transcript = session_file(config, maho_session_id);
    if !transcript.is_file() {
        return Err(SessionControlError::SessionNotFound(maho_session_id.to_string()));
    }
    let socket_path = session_socket(config, &transcript);
    let socket = socket_path.as_path();
    if !socket.exists() {
        return Err(SessionControlError::NotRunning(socket.to_path_buf()));
    }
    let argv = session_client_argv(&entry, verb, &transcript, text, socket);
    let mut command = tokio::process::Command::new(config.runtime_binary());
    command
        .args(&argv)
        .envs(config.env_vars())
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    for (key, _) in std::env::vars_os() {
        if let Some(key) = key.to_str() {
            let upper = key.to_ascii_uppercase();
            if (upper.starts_with("OMO_") || upper.starts_with("SENPI_") || upper.starts_with("PI_"))
                && !config.env_vars().contains_key(key)
            {
                command.env_remove(key);
            }
        }
    }
    if let Some(cwd) = config.agent_dir().parent() {
        command.current_dir(cwd);
    }
    let output = command
        .output()
        .await
        .map_err(|error| SessionControlError::Failed(error.to_string()))?;
    classify(
        verb,
        maho_session_id,
        socket,
        output.status.code(),
        &String::from_utf8_lossy(&output.stdout),
        &String::from_utf8_lossy(&output.stderr),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn argv_maps_verbs_to_engine_subcommands() {
        let entry = Path::new("/b/dist/bundle/cli.js");
        let sock = Path::new("/a/rpc.sock");
        let file = Path::new("/a/sessions/task-1.jsonl");
        let state = session_client_argv(entry, SessionVerb::Status, file, None, sock);
        assert_eq!(&state[1..5], ["host", "session", "state", "/a/sessions/task-1.jsonl"]);
        assert_eq!(&state[5..], ["--socket", "/a/rpc.sock", "--json"]);
        let steer = session_client_argv(entry, SessionVerb::Queue, file, Some("go"), sock);
        assert_eq!(steer[3], "steer");
        assert_eq!(steer[5], "go");
        assert_eq!(session_client_argv(entry, SessionVerb::Stop, file, None, sock)[3], "abort");
    }

    #[test]
    fn session_socket_prefers_the_pointer_written_by_the_host() {
        let dir = tempfile::tempdir().unwrap();
        let config = OmoLaunchConfig::builder(dir.path())
            .socket_path(dir.path().join("own.sock"))
            .build()
            .unwrap();
        let transcript = session_file(&config, "task-1");
        assert_eq!(session_socket(&config, &transcript), dir.path().join("own.sock"));
        std::fs::create_dir_all(transcript.parent().unwrap()).unwrap();
        std::fs::write(socket_pointer_file(&transcript), "/tmp/m_rpc_host.sock\n").unwrap();
        assert_eq!(session_socket(&config, &transcript), PathBuf::from("/tmp/m_rpc_host.sock"));
        std::fs::write(socket_pointer_file(&transcript), "relative.sock").unwrap();
        assert_eq!(session_socket(&config, &transcript), dir.path().join("own.sock"));
    }

    #[test]
    fn client_entry_sits_in_dist_bundle() {
        assert_eq!(
            session_client_entry(Path::new("/r/omo/dist/rpc-entry.js")),
            PathBuf::from("/r/omo/dist/bundle/cli.js")
        );
    }

    #[test]
    fn classify_maps_exit_codes_and_refusals() {
        let sock = Path::new("/a/rpc.sock");
        let ok = classify(
            SessionVerb::Steer,
            "s",
            sock,
            Some(0),
            r#"{"action":"steer","disposition":"queued","streaming":true}"#,
            "",
        )
        .unwrap();
        assert_eq!(ok.is_streaming(), Some(true));
        assert!(matches!(
            classify(SessionVerb::Status, "s", sock, Some(3), r#"{"action":"refuse","reason":"host_unavailable"}"#, ""),
            Err(SessionControlError::NotRunning(_))
        ));
        assert!(matches!(
            classify(SessionVerb::Steer, "s", sock, Some(3), r#"{"action":"refuse","reason":"unknown_session"}"#, ""),
            Err(SessionControlError::SessionNotFound(_))
        ));
        assert!(matches!(
            classify(SessionVerb::Status, "s", sock, Some(1), "", "boom"),
            Err(SessionControlError::Failed(m)) if m == "boom"
        ));
    }
}
