// CLI Tool Executor Subprocess Adapter
use anyhow::{anyhow, Result};
use maho_types::ai::AiCliTool;
use serde_json::Value;
use std::path::PathBuf;
use std::time::Duration;
use tokio::io::AsyncReadExt;

pub struct CliToolExecutor {
    pub tool: AiCliTool,
    pub working_dir: PathBuf,
}

impl CliToolExecutor {
    pub fn new(tool: AiCliTool, working_dir: Option<PathBuf>) -> Self {
        let working_dir = working_dir.unwrap_or_else(|| std::env::temp_dir());
        Self { tool, working_dir }
    }

    pub async fn execute(&self, args: Value) -> Result<Value> {
        // 1. Validate args against parameters_schema
        self.validate_args(&args)?;

        // 2. Parse command template and perform argument-level substitution
        let mut parts: Vec<String> = if self.tool.command_template.trim().starts_with('[') {
            serde_json::from_str(&self.tool.command_template)
                .map_err(|e| anyhow!("Malformed JSON command template: {}", e))?
        } else {
            self.tool
                .command_template
                .split_whitespace()
                .map(|s| s.to_string())
                .collect()
        };

        if parts.is_empty() {
            return Err(anyhow!("Command template is empty"));
        }

        // The program is never templated: a model-supplied value must not
        // choose which binary runs.
        let exec = parts.remove(0);
        if exec.contains('{') {
            return Err(anyhow!(
                "Command template program must not contain placeholders"
            ));
        }

        let final_args = parts
            .iter()
            .map(|part| substitute_placeholders(part, &args))
            .collect::<Result<Vec<_>>>()?;

        // 3. Prepare isolation before process creation. Policy is captured here
        // and applied in pre_exec so the child and its descendants inherit it.
        #[cfg(target_os = "linux")]
        let prepared_sandbox = {
            use crate::sandbox::{linux::LinuxSandbox, SandboxBackend, SandboxPolicy};
            let mut denied_paths = Vec::new();
            if let Some(home) = std::env::var_os("HOME") {
                let home = PathBuf::from(home);
                denied_paths.push(home.join(".ssh"));
                denied_paths.push(home.join(".local/share/keyrings"));
                denied_paths.push(home.join(".config/google-chrome"));
                denied_paths.push(home.join(".config/chromium"));
            }
            let cwd = std::fs::canonicalize(&self.working_dir)
                .map_err(|error| anyhow!("SandboxUnavailable: invalid cwd: {error}"))?;
            let policy = SandboxPolicy {
                writable_roots: vec![cwd],
                readable_roots: Vec::new(),
                denied_paths,
                network: false,
            };
            Some(
                LinuxSandbox
                    .prepare(&policy)
                    .map_err(|error| anyhow!("SandboxUnavailable: {error}"))?,
            )
        };

        // Windows: the command runs inside an AppContainer + kill-on-close job
        // (sandbox::windows::run_confined). stdout/stderr are redirected by
        // cmd.exe into files in a private directory that is the only other
        // writable root, then read back and capped like the Unix path.
        #[cfg(target_os = "windows")]
        {
            self.execute_confined_windows(&exec, &final_args).await
        }

        // 4. Spawn without a shell wrapper.
        #[cfg(not(target_os = "windows"))]
        {
            let mut cmd = tokio::process::Command::new(exec);
            cmd.args(&final_args);
            cmd.current_dir(&self.working_dir);
            cmd.stdout(std::process::Stdio::piped());
            cmd.stderr(std::process::Stdio::piped());

            #[cfg(unix)]
            {
                unsafe {
                    #[cfg(target_os = "linux")]
                    let prepared_sandbox = prepared_sandbox;
                    cmd.pre_exec(move || {
                        if libc::setsid() == -1 {
                            return Err(std::io::Error::last_os_error());
                        }
                        #[cfg(target_os = "linux")]
                        if let Some(sandbox) = &prepared_sandbox {
                            sandbox.apply().map_err(|error| {
                                std::io::Error::new(std::io::ErrorKind::PermissionDenied, error)
                            })?;
                        }
                        Ok(())
                    });
                }
            }

            let mut child = cmd
                .spawn()
                .map_err(|e| anyhow!("Failed to spawn CLI subprocess: {:?}", e))?;

            let stdout = child
                .stdout
                .take()
                .ok_or_else(|| anyhow!("Failed to open stdout"))?;
            let stderr = child
                .stderr
                .take()
                .ok_or_else(|| anyhow!("Failed to open stderr"))?;

            // 5. Timeout after timeout_ms (capped at 60s) -> SIGKILL
            let timeout_ms = self.tool.timeout_ms.min(60_000);
            let timeout_duration = Duration::from_millis(timeout_ms);

            let stdout_fut = self.read_with_limit(stdout);
            let stderr_fut = self.read_with_limit(stderr);
            let wait_fut = child.wait();

            // 6. Read stdout/stderr concurrently with waiting to prevent pipe deadlock
            match tokio::time::timeout(timeout_duration, async {
                tokio::join!(stdout_fut, stderr_fut, wait_fut)
            })
            .await
            {
                Ok((stdout_res, stderr_res, status_res)) => {
                    let stdout_bytes = stdout_res?;
                    let stderr_bytes = stderr_res?;
                    let status = status_res?;

                    let stdout_str = String::from_utf8_lossy(&stdout_bytes).to_string();
                    let stderr_str = String::from_utf8_lossy(&stderr_bytes).to_string();

                    let result = serde_json::json!({
                        "stdout": stdout_str,
                        "stderr": stderr_str,
                        "exit_code": status.code(),
                    });

                    Ok(result)
                }
                Err(_) => {
                    // The child ran under setsid, so its pid is also its process
                    // group id; kill the whole group so grandchildren die too.
                    #[cfg(unix)]
                    if let Some(pid) = child.id() {
                        if unsafe { libc::killpg(pid as libc::pid_t, libc::SIGKILL) } != 0 {
                            tracing::warn!(
                                pid,
                                error = %std::io::Error::last_os_error(),
                                "failed to SIGKILL CLI tool process group"
                            );
                        }
                    }
                    if let Err(error) = child.kill().await {
                        tracing::warn!(%error, "failed to kill timed-out CLI tool process");
                    }
                    Err(anyhow!(
                        "CLI tool execution timed out after {:?}",
                        timeout_duration
                    ))
                }
            }
        }
    }

    #[cfg(target_os = "windows")]
    async fn execute_confined_windows(&self, exec: &str, args: &[String]) -> Result<Value> {
        use crate::sandbox::{windows::run_confined, SandboxPolicy};

        let cwd = std::fs::canonicalize(&self.working_dir)
            .map_err(|error| anyhow!("SandboxUnavailable: invalid cwd: {error}"))?;
        struct CaptureDir(PathBuf);
        impl Drop for CaptureDir {
            fn drop(&mut self) {
                let _ = std::fs::remove_dir_all(&self.0);
            }
        }
        let capture = CaptureDir(std::env::temp_dir().join(format!(
            "maho-agent-shell-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|d| d.as_nanos())
                .unwrap_or_default()
        )));
        std::fs::create_dir(&capture.0)
            .map_err(|error| anyhow!("SandboxUnavailable: capture dir: {error}"))?;
        let stdout_path = capture.0.join("stdout.txt");
        let stderr_path = capture.0.join("stderr.txt");
        let command_line = windows_confined_command_line(exec, args, &stdout_path, &stderr_path)?;
        let policy = SandboxPolicy {
            writable_roots: vec![cwd.clone(), capture.0.clone()],
            readable_roots: Vec::new(),
            denied_paths: Vec::new(),
            network: false,
        };
        let timeout = Duration::from_millis(self.tool.timeout_ms.min(60_000));
        let exit = tokio::task::spawn_blocking(move || {
            run_confined(&command_line, &cwd, &policy, timeout)
        })
        .await
        .map_err(|error| anyhow!("SandboxUnavailable: {error}"))?
        .map_err(|error| anyhow!("SandboxUnavailable: {error}"))?;
        if exit.timed_out {
            return Err(anyhow!("CLI tool execution timed out after {:?}", timeout));
        }
        let cap = self.tool.output_cap_bytes;
        let read_capped = |path: &std::path::Path| -> Result<String> {
            let bytes = std::fs::read(path).unwrap_or_default();
            if bytes.len() > cap {
                return Err(anyhow!("Output exceeded output_cap_bytes ({cap})"));
            }
            Ok(String::from_utf8_lossy(&bytes).to_string())
        };
        Ok(serde_json::json!({
            "stdout": read_capped(&stdout_path)?,
            "stderr": read_capped(&stderr_path)?,
            "exit_code": exit.exit_code,
        }))
    }

    fn validate_args(&self, args: &Value) -> Result<()> {
        let schema = &self.tool.parameters_schema;

        // Lightweight schema validator
        if let Some(obj) = schema.as_object() {
            if let Some(req_val) = obj.get("required") {
                if let Some(req_arr) = req_val.as_array() {
                    for key_val in req_arr {
                        if let Some(key) = key_val.as_str() {
                            if args.get(key).is_none() || args.get(key) == Some(&Value::Null) {
                                return Err(anyhow!("Missing required parameter: {}", key));
                            }
                        }
                    }
                }
            }
            if let Some(props_val) = obj.get("properties") {
                if let Some(props) = props_val.as_object() {
                    for (key, val) in args.as_object().unwrap_or(&serde_json::Map::new()) {
                        if let Some(prop_schema_val) = props.get(key) {
                            if let Some(prop_schema) = prop_schema_val.as_object() {
                                if let Some(expected_type) =
                                    prop_schema.get("type").and_then(|t| t.as_str())
                                {
                                    match expected_type {
                                        "string" => {
                                            if !val.is_string() {
                                                return Err(anyhow!(
                                                    "Parameter {} must be a string",
                                                    key
                                                ));
                                            }
                                        }
                                        "number" | "integer" => {
                                            if !val.is_number() {
                                                return Err(anyhow!(
                                                    "Parameter {} must be a number",
                                                    key
                                                ));
                                            }
                                        }
                                        "boolean" => {
                                            if !val.is_boolean() {
                                                return Err(anyhow!(
                                                    "Parameter {} must be a boolean",
                                                    key
                                                ));
                                            }
                                        }
                                        "array" => {
                                            if !val.is_array() {
                                                return Err(anyhow!(
                                                    "Parameter {} must be an array",
                                                    key
                                                ));
                                            }
                                        }
                                        "object" => {
                                            if !val.is_object() {
                                                return Err(anyhow!(
                                                    "Parameter {} must be an object",
                                                    key
                                                ));
                                            }
                                        }
                                        _ => {}
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        Ok(())
    }

    async fn read_with_limit<R: tokio::io::AsyncRead + Unpin>(
        &self,
        mut reader: R,
    ) -> Result<Vec<u8>> {
        let limit = self.tool.output_cap_bytes;
        let mut buf = vec![0; 4096];
        let mut output = Vec::new();
        loop {
            if output.len() >= limit {
                break;
            }
            let max_to_read = std::cmp::min(buf.len(), limit - output.len());
            let n = reader.read(&mut buf[..max_to_read]).await?;
            if n == 0 {
                break;
            }
            output.extend_from_slice(&buf[..n]);
        }
        Ok(output)
    }
}

/// Replaces `{key}` placeholders in one template argument in a single pass,
/// so substituted values are never re-scanned for placeholders. Values that
/// start with '-' are rejected because they would be parsed as options by the
/// target program (e.g. `rg --pre=/bin/sh`).
fn substitute_placeholders(part: &str, args: &Value) -> Result<String> {
    let mut out = String::with_capacity(part.len());
    let mut rest = part;
    while let Some(open) = rest.find('{') {
        out.push_str(&rest[..open]);
        let after = &rest[open + 1..];
        let Some(close) = after.find('}') else {
            out.push_str(&rest[open..]);
            return Ok(out);
        };
        let key = &after[..close];
        match args.get(key) {
            Some(val) => {
                let val_str = match val {
                    Value::String(s) => s.clone(),
                    other => other.to_string(),
                };
                if val_str.starts_with('-') {
                    return Err(anyhow!(
                        "Parameter {} must not start with '-' (option injection)",
                        key
                    ));
                }
                out.push_str(&val_str);
            }
            None => {
                out.push('{');
                out.push_str(key);
                out.push('}');
            }
        }
        rest = &after[close + 1..];
    }
    out.push_str(rest);
    Ok(out)
}

/// Builds `cmd.exe /d /s /c ""prog" "arg" ... 1>"out" 2>"err""` for the
/// Windows confined runner. Arguments are quoted, and characters cmd.exe
/// treats specially are rejected instead of escaped: a model-supplied value
/// must never be able to chain another command.
#[cfg(any(target_os = "windows", test))]
pub(crate) fn windows_confined_command_line(
    exec: &str,
    args: &[String],
    stdout_path: &std::path::Path,
    stderr_path: &std::path::Path,
) -> Result<String> {
    fn check(value: &str) -> Result<()> {
        if value.chars().any(|c| matches!(c, '"' | '%' | '^' | '&' | '|' | '<' | '>' | '\n' | '\r' | '!')) {
            return Err(anyhow!(
                "Argument contains characters that cannot be passed safely to cmd.exe"
            ));
        }
        Ok(())
    }
    // Quote only when needed: cmd.exe treats a quoted first token as a path,
    // so built-ins (echo, dir, copy) must stay bare.
    fn quote(value: &str) -> Result<String> {
        check(value)?;
        if value.is_empty() || value.chars().any(|c| c.is_whitespace()) {
            Ok(format!("\"{value}\""))
        } else {
            Ok(value.to_string())
        }
    }
    if exec.is_empty() || exec.chars().any(|c| c.is_whitespace()) {
        return Err(anyhow!("Program name must be a single token"));
    }
    check(exec)?;
    let mut parts = vec![exec.to_string()];
    for arg in args {
        parts.push(quote(arg)?);
    }
    check(&stdout_path.to_string_lossy())?;
    check(&stderr_path.to_string_lossy())?;
    Ok(format!(
        "cmd.exe /d /s /c \"{} 1>\"{}\" 2>\"{}\"\"",
        parts.join(" "),
        stdout_path.display(),
        stderr_path.display()
    ))
}

#[cfg(test)]
mod tests {
    #[test]
    fn windows_command_line_quotes_args_and_rejects_cmd_metacharacters() {
        let out = std::path::Path::new(r"C:\cap\stdout.txt");
        let err = std::path::Path::new(r"C:\cap\stderr.txt");
        let line = super::windows_confined_command_line(
            "git",
            &["status".to_string(), "--short".to_string()],
            out,
            err,
        )
        .unwrap();
        assert_eq!(
            line,
            r#"cmd.exe /d /s /c "git status --short 1>"C:\cap\stdout.txt" 2>"C:\cap\stderr.txt"""#
        );
        let spaced = super::windows_confined_command_line(
            "cmd.exe",
            &["/c".to_string(), "echo hi there".to_string()],
            out,
            err,
        )
        .unwrap();
        assert!(spaced.contains(r#"cmd.exe /c "echo hi there" 1>"#), "{spaced}");
        assert!(super::windows_confined_command_line("a b", &[], out, err).is_err());
        for bad in ["a&b", "a|b", "a>b", "a<b", "a^b", "%PATH%", "a\"b", "!x!", "a\nb"] {
            assert!(
                super::windows_confined_command_line("git", &[bad.to_string()], out, err).is_err(),
                "{bad:?} must be rejected"
            );
        }
    }

    use super::*;

    fn tool(command_template: &str, timeout_ms: u64) -> AiCliTool {
        AiCliTool {
            id: "t".into(),
            workspace_id: "w".into(),
            name: "t".into(),
            description: String::new(),
            command_template: command_template.into(),
            parameters_schema: serde_json::json!({"type": "object"}),
            sensitive: true,
            timeout_ms,
            output_cap_bytes: 4096,
            working_directory: None,
            created_at: String::new(),
            updated_at: String::new(),
        }
    }

    #[test]
    fn substitution_rejects_option_values_and_is_single_pass() {
        let args = serde_json::json!({"q": "{p}", "p": "/etc/passwd", "bad": "--pre=/bin/sh"});
        assert_eq!(substitute_placeholders("{q}", &args).unwrap(), "{p}");
        assert!(substitute_placeholders("{bad}", &args).is_err());
        assert_eq!(substitute_placeholders("x={p}", &args).unwrap(), "x=/etc/passwd");
    }

    #[tokio::test]
    async fn cli_tool_rejects_templated_program_and_malformed_json() {
        let exec = CliToolExecutor::new(tool("{bin} hello", 1000), None);
        let err = exec
            .execute(serde_json::json!({"bin": "/bin/sh"}))
            .await
            .unwrap_err();
        assert!(err.to_string().contains("placeholders"), "{err}");

        let exec = CliToolExecutor::new(tool("[\"echo\", ", 1000), None);
        let err = exec.execute(serde_json::json!({})).await.unwrap_err();
        assert!(err.to_string().contains("Malformed JSON"), "{err}");
    }

    #[cfg(unix)]
    #[tokio::test]
    async fn cli_tool_timeout_kills_grandchildren() {
        let dir = tempfile::tempdir().unwrap();
        let pid_file = dir.path().join("pid");
        let template = serde_json::to_string(&vec![
            "/bin/sh",
            "-c",
            "sleep 30 & echo $! > \"$0\"; wait",
            "{pid_file}",
        ])
        .unwrap();
        let exec = CliToolExecutor::new(tool(&template, 500), None);
        let err = exec
            .execute(serde_json::json!({"pid_file": pid_file.to_string_lossy()}))
            .await
            .unwrap_err();
        assert!(err.to_string().contains("timed out"), "{err}");

        let pid: libc::pid_t = std::fs::read_to_string(&pid_file)
            .unwrap()
            .trim()
            .parse()
            .unwrap();
        // The grandchild was SIGKILLed synchronously; give init a bounded
        // window to reap it rather than relying on a fixed sleep.
        let deadline = std::time::Instant::now() + Duration::from_secs(5);
        while unsafe { libc::kill(pid, 0) } == 0 {
            assert!(
                std::time::Instant::now() < deadline,
                "grandchild {pid} survived the timeout kill"
            );
            tokio::task::yield_now().await;
        }
    }
}
