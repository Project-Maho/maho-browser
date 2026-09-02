use crate::error::PlatformError;
#[cfg(not(target_os = "macos"))]
use crate::error::NON_MACOS_REMEDIATION;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct SendRequest {
    pub to: String,
    pub chat_guid: Option<String>,
    pub text: String,
    pub approve: bool,
    pub dry_run: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum SendOutcome {
    DryRun { script: String },
    Sent { output: String },
}

pub trait ProcessRunner: Send + Sync {
    fn run(&self, program: &str, args: &[&str]) -> std::io::Result<std::process::Output>;
}

pub struct OsRunner;

impl ProcessRunner for OsRunner {
    fn run(&self, program: &str, args: &[&str]) -> std::io::Result<std::process::Output> {
        std::process::Command::new(program).args(args).output()
    }
}

pub fn escape_applescript(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for c in s.chars() {
        match c {
            '\\' => out.push_str("\\\\"),
            '"' => out.push_str("\\\""),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            other => out.push(other),
        }
    }
    out
}

pub fn compose_send_applescript(chat_guid: Option<&str>, to: &str, text: &str) -> String {
    let escaped_text = escape_applescript(text);
    if let Some(guid) = chat_guid {
        let escaped_guid = escape_applescript(guid);
        format!(
            "tell application \"Messages\" to send \"{escaped_text}\" to chat \"{escaped_guid}\""
        )
    } else {
        // TODO: handle resolution for multi-service or participant lookup
        let escaped_to = escape_applescript(to);
        format!(
            "tell application \"Messages\"\n    set targetService to 1st account whose service type = iMessage\n    set targetBuddy to participant \"{escaped_to}\" of targetService\n    send \"{escaped_text}\" to targetBuddy\nend tell"
        )
    }
}

pub fn send_message(
    req: SendRequest,
    runner: &dyn ProcessRunner,
) -> Result<SendOutcome, PlatformError> {
    let script = compose_send_applescript(req.chat_guid.as_deref(), &req.to, &req.text);
    if req.dry_run {
        return Ok(SendOutcome::DryRun { script });
    }
    if !req.approve {
        return Err(PlatformError::ApprovalRequired {
            action: "imessage.send".to_string(),
        });
    }

    let output = runner
        .run("/usr/bin/osascript", &["-e", &script])
        .map_err(|e| PlatformError::Io(e.to_string()))?;

    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        return Err(PlatformError::Io(format!(
            "osascript exited with {}: {stderr}",
            output.status
        )));
    }

    let stdout = String::from_utf8_lossy(&output.stdout).trim().to_string();
    Ok(SendOutcome::Sent { output: stdout })
}

pub fn send(req: SendRequest, runner: &dyn ProcessRunner) -> Result<SendOutcome, PlatformError> {
    #[cfg(target_os = "macos")]
    {
        send_message(req, runner)
    }
    #[cfg(not(target_os = "macos"))]
    {
        // osascript and Messages.app do not exist off macOS; be honest about it.
        let _ = (req, runner);
        Err(PlatformError::Unsupported {
            platform: std::env::consts::OS.to_string(),
            capability: "imessage.send".to_string(),
            remediation: NON_MACOS_REMEDIATION.to_string(),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Mutex;

    #[derive(Default)]
    struct RecordingRunner {
        invocations: Mutex<Vec<(String, Vec<String>)>>,
        canned_output: Mutex<Option<std::process::Output>>,
    }

    impl RecordingRunner {
        fn new() -> Self {
            Self::default()
        }

        #[allow(dead_code)]
        fn with_canned_output(output: std::process::Output) -> Self {
            Self {
                invocations: Mutex::new(Vec::new()),
                canned_output: Mutex::new(Some(output)),
            }
        }
    }

    impl ProcessRunner for RecordingRunner {
        fn run(&self, program: &str, args: &[&str]) -> std::io::Result<std::process::Output> {
            let args_vec: Vec<String> = args.iter().map(|s| s.to_string()).collect();
            self.invocations
                .lock()
                .expect("lock")
                .push((program.to_string(), args_vec));

            if let Some(output) = self.canned_output.lock().expect("lock").clone() {
                Ok(output)
            } else {
                #[cfg(unix)]
                use std::os::unix::process::ExitStatusExt;
                #[cfg(unix)]
                let status = std::process::ExitStatus::from_raw(0);
                #[cfg(not(unix))]
                let status = std::process::ExitStatus::default();

                Ok(std::process::Output {
                    status,
                    stdout: b"ok\n".to_vec(),
                    stderr: Vec::new(),
                })
            }
        }
    }

    #[test]
    fn test_escape_applescript_table() {
        let cases = vec![
            (r#"He said "hi" \ done"#, r#"He said \"hi\" \\ done"#),
            ("hello\nworld", "hello\\nworld"),
            ("tab\there", "tab\\there"),
            (
                "mixed \"quotes\" and \\backslashes\\ and \nnewlines\t!",
                "mixed \\\"quotes\\\" and \\\\backslashes\\\\ and \\nnewlines\\t!",
            ),
            ("plain text", "plain text"),
            ("", ""),
        ];

        for (input, expected) in cases {
            assert_eq!(
                escape_applescript(input),
                expected,
                "Failed escaping for input: {:?}",
                input
            );
        }
    }

    #[test]
    fn test_compose_send_applescript_chat_guid() {
        let script = compose_send_applescript(
            Some("iMessage;-;+1234567890"),
            "+1234567890",
            "He said \"hi\" \n done",
        );
        assert_eq!(
            script,
            "tell application \"Messages\" to send \"He said \\\"hi\\\" \\n done\" to chat \"iMessage;-;+1234567890\""
        );
    }

    #[test]
    fn test_compose_send_applescript_fallback_to() {
        let script = compose_send_applescript(None, "+1234567890", "Hello there");
        assert!(script.contains("tell application \"Messages\""));
        assert!(script.contains("+1234567890"));
        assert!(script.contains("Hello there"));
    }

    #[test]
    fn test_send_dry_run_never_calls_runner() {
        let runner = RecordingRunner::new();
        let req = SendRequest {
            to: "+1234567890".to_string(),
            chat_guid: Some("iMessage;-;+1234567890".to_string()),
            text: "test message".to_string(),
            approve: false,
            dry_run: true,
        };

        let res = send_message(req, &runner).expect("dry_run should succeed");
        match res {
            SendOutcome::DryRun { script } => {
                assert!(script.contains("test message"));
            }
            SendOutcome::Sent { .. } => panic!("Expected DryRun, got Sent"),
        }

        let invocations = runner.invocations.lock().expect("lock");
        assert_eq!(invocations.len(), 0, "dry_run must not call runner");
    }

    #[test]
    fn test_send_unapproved_never_calls_runner() {
        let runner = RecordingRunner::new();
        let req = SendRequest {
            to: "+1234567890".to_string(),
            chat_guid: None,
            text: "hello".to_string(),
            approve: false,
            dry_run: false,
        };

        let res = send_message(req, &runner);
        match res {
            Err(PlatformError::ApprovalRequired { action }) => {
                assert_eq!(action, "imessage.send");
            }
            other => panic!("Expected ApprovalRequired, got {:?}", other),
        }

        let invocations = runner.invocations.lock().expect("lock");
        assert_eq!(invocations.len(), 0, "unapproved send must not call runner");
    }

    #[test]
    fn test_send_approved_calls_runner_once() {
        let runner = RecordingRunner::new();
        let req = SendRequest {
            to: "+1234567890".to_string(),
            chat_guid: Some("iMessage;-;+1234567890".to_string()),
            text: "hello".to_string(),
            approve: true,
            dry_run: false,
        };

        let res = send_message(req, &runner).expect("approved send should succeed");
        match res {
            SendOutcome::Sent { output } => {
                assert_eq!(output, "ok");
            }
            SendOutcome::DryRun { .. } => panic!("Expected Sent, got DryRun"),
        }

        let invocations = runner.invocations.lock().expect("lock");
        assert_eq!(
            invocations.len(),
            1,
            "approved send must call runner exactly once"
        );
        assert_eq!(invocations[0].0, "/usr/bin/osascript");
        assert_eq!(invocations[0].1.len(), 2);
        assert_eq!(invocations[0].1[0], "-e");
        assert!(invocations[0].1[1].contains(
            "tell application \"Messages\" to send \"hello\" to chat \"iMessage;-;+1234567890\""
        ));
    }

    #[test]
    fn test_send_approved_runner_error() {
        #[cfg(unix)]
        use std::os::unix::process::ExitStatusExt;
        #[cfg(unix)]
        let status = std::process::ExitStatus::from_raw(1 << 8);
        #[cfg(not(unix))]
        let status = std::process::ExitStatus::default();

        let runner = RecordingRunner::with_canned_output(std::process::Output {
            status,
            stdout: Vec::new(),
            stderr: b"execution error".to_vec(),
        });
        let req = SendRequest {
            to: "+1234567890".to_string(),
            chat_guid: None,
            text: "hello".to_string(),
            approve: true,
            dry_run: false,
        };

        let res = send_message(req, &runner);
        match res {
            Err(PlatformError::Io(msg)) => {
                assert!(msg.contains("osascript exited with"));
                assert!(msg.contains("execution error"));
            }
            other => panic!("Expected Io error, got {:?}", other),
        }
    }
}
