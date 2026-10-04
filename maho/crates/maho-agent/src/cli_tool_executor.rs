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
                .unwrap_or_else(|_| vec![self.tool.command_template.clone()])
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

        let mut exec = parts.remove(0);
        if let Value::Object(obj) = &args {
            for (key, val) in obj {
                let placeholder = format!("{{{}}}", key);
                let val_str = match val {
                    Value::String(s) => s.clone(),
                    other => other.to_string(),
                };
                exec = exec.replace(&placeholder, &val_str);
            }
        }

        let mut final_args = Vec::new();
        for mut part in parts {
            if let Value::Object(obj) = &args {
                for (key, val) in obj {
                    let placeholder = format!("{{{}}}", key);
                    let val_str = match val {
                        Value::String(s) => s.clone(),
                        other => other.to_string(),
                    };
                    part = part.replace(&placeholder, &val_str);
                }
            }
            final_args.push(part);
        }

        // 3. Spawning the process with POSIX_SPAWN_SETSID sandbox (no shell wrapper)
        let mut cmd = tokio::process::Command::new(exec);
        cmd.args(&final_args);
        cmd.current_dir(&self.working_dir);
        cmd.stdout(std::process::Stdio::piped());
        cmd.stderr(std::process::Stdio::piped());

        #[cfg(unix)]
        {
            unsafe {
                cmd.pre_exec(|| {
                    libc::setsid();
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

        // 4. Timeout after timeout_ms (capped at 60s) -> SIGKILL
        let timeout_ms = self.tool.timeout_ms.min(60_000);
        let timeout_duration = Duration::from_millis(timeout_ms);

        let stdout_fut = self.read_with_limit(stdout);
        let stderr_fut = self.read_with_limit(stderr);
        let wait_fut = child.wait();

        // 5. Read stdout/stderr concurrently with waiting to prevent pipe deadlock
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
                let _ = child.kill().await;
                Err(anyhow!(
                    "CLI tool execution timed out after {:?}",
                    timeout_duration
                ))
            }
        }
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
