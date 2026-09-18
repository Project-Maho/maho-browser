#![cfg(unix)]

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use async_trait::async_trait;
use base64::Engine;
use maho_types::chat::{ChatContent, ChatContentPart, ChatMessage};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::unix::{OwnedReadHalf, OwnedWriteHalf};
use tokio::net::UnixStream;
use tokio::sync::{broadcast, oneshot};

use crate::omo::config::OmoLaunchConfig;
use crate::omo::process::OmoProcess;
use crate::omo::proto::{
    AssistantMessageEvent, FramingCodec, ImageContent, RpcAuthProvider, RpcCommand, RpcEvent,
    RpcInboundMessage, RpcResponse,
};
use crate::run_journal::{AgentRunId, RunCheckpointKind, RunJournal};
use crate::{
    AgentError, AgentRuntime, AgentStorage, AgentStreamEvent, BrowserActionToolsState,
    PermissionCallback, SecureKeyBundle, ToolDefinition,
};

const RESPONSE_TIMEOUT: Duration = Duration::from_secs(30);
const TURN_TIMEOUT: Duration = Duration::from_secs(300);
const MCP_LIST_TIMEOUT: Duration = Duration::from_secs(5);

type EventCallback = Arc<dyn Fn(AgentStreamEvent) + Send + Sync + 'static>;
type SecureStorageCallback =
    Box<dyn Fn(&str) -> Result<SecureKeyBundle, AgentError> + Send + Sync + 'static>;

struct RpcConnection {
    writer: tokio::sync::Mutex<OwnedWriteHalf>,
    pending: Arc<Mutex<HashMap<String, oneshot::Sender<RpcResponse>>>>,
    events: broadcast::Sender<RpcEvent>,
    next_id: AtomicU64,
}

impl RpcConnection {
    fn new(stream: UnixStream) -> Arc<Self> {
        let (reader, writer) = stream.into_split();
        let (events, _) = broadcast::channel(1024);
        let connection = Arc::new(Self {
            writer: tokio::sync::Mutex::new(writer),
            pending: Arc::new(Mutex::new(HashMap::new())),
            events,
            next_id: AtomicU64::new(1),
        });
        tokio::spawn(read_rpc_messages(
            reader,
            Arc::clone(&connection.pending),
            connection.events.clone(),
        ));
        connection
    }

    async fn request(&self, command: RpcCommand) -> Result<RpcResponse, AgentError> {
        let id = self.next_id.fetch_add(1, Ordering::Relaxed).to_string();
        let bytes = FramingCodec::encode(&command.with_id(id.clone()))
            .map_err(|error| AgentError::ExecutionError(error.to_string()))?;
        let (sender, receiver) = oneshot::channel();
        lock_unpoison(&self.pending).insert(id.clone(), sender);

        if let Err(error) = self.writer.lock().await.write_all(&bytes).await {
            lock_unpoison(&self.pending).remove(&id);
            return Err(AgentError::ExecutionError(format!(
                "failed to write omo RPC request: {error}"
            )));
        }

        match tokio::time::timeout(RESPONSE_TIMEOUT, receiver).await {
            Ok(Ok(response)) => Ok(response),
            Ok(Err(_)) => Err(AgentError::ExecutionError(
                "omo RPC connection closed before responding".to_string(),
            )),
            Err(_) => {
                lock_unpoison(&self.pending).remove(&id);
                Err(AgentError::ExecutionError(format!(
                    "omo RPC request timed out after {RESPONSE_TIMEOUT:?}"
                )))
            }
        }
    }
}

async fn read_rpc_messages(
    reader: OwnedReadHalf,
    pending: Arc<Mutex<HashMap<String, oneshot::Sender<RpcResponse>>>>,
    events: broadcast::Sender<RpcEvent>,
) {
    let mut lines = BufReader::new(reader).lines();
    while let Ok(Some(line)) = lines.next_line().await {
        let value = match serde_json::from_str::<serde_json::Value>(&line) {
            Ok(value) => value,
            Err(error) => {
                tracing::warn!(target = "maho_agent::omo", %error, "discarding malformed omo RPC message");
                continue;
            }
        };
        if is_ui_chrome_request(&value) {
            continue;
        }
        match serde_json::from_value::<RpcInboundMessage>(value) {
            Ok(RpcInboundMessage::Response(response)) => {
                if let Some(id) = response.id.as_deref() {
                    if let Some(sender) = lock_unpoison(&pending).remove(id) {
                        let _ = sender.send(response);
                    }
                }
            }
            Ok(RpcInboundMessage::Event(event)) => {
                let _ = events.send(event);
            }
            Err(error) => {
                tracing::debug!(target = "maho_agent::omo", %error, "ignoring unsupported omo RPC message");
            }
        }
    }
    lock_unpoison(&pending).clear();
}

fn lock_unpoison<T>(mutex: &Mutex<T>) -> std::sync::MutexGuard<'_, T> {
    mutex
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
}

fn is_ui_chrome_request(value: &serde_json::Value) -> bool {
    value.get("type").and_then(serde_json::Value::as_str) == Some("extension_ui_request")
        && matches!(
            value.get("method").and_then(serde_json::Value::as_str),
            Some("setWidget" | "setStatus")
        )
}

/// Asks the browser MCP bridge for its tool list over stdio.
///
/// Returns `None` when the bridge cannot be reached at all, which is the normal state
/// whenever no Maho browser is running: the bridge connects to the browser's socket and
/// exits when that socket is absent.
async fn browser_mcp_tools(
    binary: &Path,
    socket_path: Option<&Path>,
    timeout: Duration,
) -> Option<Vec<ToolDefinition>> {
    let mut cmd = tokio::process::Command::new(binary);
    cmd.stdin(std::process::Stdio::piped())
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::null())
        .kill_on_drop(true);
    cmd.env("MAHO_MCP_PUBLIC_ENABLED", "true");
    if let Some(sp) = socket_path {
        cmd.env("MAHO_MCP_SOCKET_PATH", sp);
    }
    let mut child = cmd.spawn().ok()?;

    let mut stdin = child.stdin.take()?;
    let stdout = child.stdout.take()?;

    let handshake = serde_json::json!({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "protocolVersion": "2025-06-18",
            "capabilities": {},
            "clientInfo": { "name": "maho-agent", "version": "0" }
        }
    });
    let listing = serde_json::json!({
        "jsonrpc": "2.0",
        "id": 2,
        "method": "tools/list",
        "params": {}
    });

    let pump = async {
        let mut request = serde_json::to_vec(&handshake).ok()?;
        request.push(b'\n');
        stdin.write_all(&request).await.ok()?;
        stdin.flush().await.ok()?;

        let mut lines = BufReader::new(stdout).lines();
        let mut sent_listing = false;
        while let Ok(Some(line)) = lines.next_line().await {
            let Ok(message) = serde_json::from_str::<serde_json::Value>(&line) else {
                continue;
            };
            match message.get("id").and_then(serde_json::Value::as_u64) {
                Some(1) if !sent_listing => {
                    sent_listing = true;
                    let mut request = serde_json::to_vec(&listing).ok()?;
                    request.push(b'\n');
                    stdin.write_all(&request).await.ok()?;
                    stdin.flush().await.ok()?;
                }
                Some(2) => return parse_mcp_tools(&message),
                _ => {}
            }
        }
        None
    };

    let tools = tokio::time::timeout(timeout, pump).await.ok().flatten();
    let _ = child.start_kill();
    tools
}

/// Mirrors `CapabilityLeavesBrowserBoundary` in
/// `maho-chromium/browser/ai/maho_browser_action_contract.h`.
///
/// Browser manipulation is full access by policy; only capabilities that leave the
/// browser boundary are approval-gated. Keep both copies in step.
fn capability_leaves_browser_boundary(canonical_id: &str) -> bool {
    const OFF_BROWSER_PREFIXES: [&str; 10] = [
        "mail.", "mail_",
        "vault.", "vault_",
        "routines.", "routines_",
        "control.", "control_",
        "policy.", "policy_",
    ];

    OFF_BROWSER_PREFIXES
        .iter()
        .any(|prefix| canonical_id.starts_with(prefix))
        || canonical_id == "artifact.export"
        || canonical_id == "artifact_export"
        || canonical_id == "browser_file_upload_select"
        || canonical_id == "browser.file_upload_select"
        || canonical_id == "input.file_upload_select"
        || canonical_id == "input_file_upload_select"
        || canonical_id == "browser.visual_click"
        || canonical_id == "browser_visual_click"
}

fn parse_mcp_tools(message: &serde_json::Value) -> Option<Vec<ToolDefinition>> {
    let listed = message.get("result")?.get("tools")?.as_array()?;
    Some(
        listed
            .iter()
            .filter_map(|tool| {
                let name = tool.get("name")?.as_str()?.to_string();
                Some(ToolDefinition {
                    description: tool
                        .get("description")
                        .and_then(serde_json::Value::as_str)
                        .unwrap_or_default()
                        .to_string(),
                    parameters_schema: tool
                        .get("inputSchema")
                        .cloned()
                        .unwrap_or_else(|| serde_json::json!({ "type": "object" })),
                    provenance: maho_types::tool::ToolProvenance::BuiltinBrowser,
                    sensitive: capability_leaves_browser_boundary(&name),
                    permission: if capability_leaves_browser_boundary(&name) {
                        maho_types::tool::ToolPermission::AlwaysAsk
                    } else {
                        maho_types::tool::ToolPermission::AutoApprove
                    },
                    name,
                })
            })
            .collect(),
    )
}

pub fn resolve_browser_socket_path(workspace_root: &Path) -> Option<PathBuf> {
    if let Ok(env_path) = std::env::var("MAHO_MCP_SOCKET_PATH") {
        if !env_path.trim().is_empty() {
            return Some(PathBuf::from(env_path));
        }
    }
    if let Some(parent) = workspace_root.parent() {
        let candidate = parent.join("maho.sock");
        if candidate.exists() {
            return Some(candidate);
        }
    }
    let candidate = workspace_root.join("maho.sock");
    if candidate.exists() {
        return Some(candidate);
    }
    if let Some(parent) = workspace_root.parent() {
        if parent != Path::new("/") && !parent.as_os_str().is_empty() {
            return Some(parent.join("maho.sock"));
        }
    }
    if let Ok(home) = std::env::var("HOME") {
        #[cfg(target_os = "macos")]
        let default_sock = PathBuf::from(&home).join("Library/Application Support/Maho/maho.sock");
        #[cfg(not(target_os = "macos"))]
        let default_sock = PathBuf::from(&home).join(".config/maho/maho.sock");
        return Some(default_sock);
    }
    workspace_root.parent().map(|p| p.join("maho.sock"))
}

pub fn browser_mcp_config(binary: &Path, socket_path: Option<&Path>) -> serde_json::Value {
    let mut env = serde_json::Map::new();
    env.insert(
        "MAHO_MCP_PUBLIC_ENABLED".to_string(),
        serde_json::Value::String("true".to_string()),
    );
    env.insert(
        "MAHO_MCP_AUTO_APPROVE".to_string(),
        serde_json::Value::String("true".to_string()),
    );
    if let Some(sp) = socket_path {
        env.insert(
            "MAHO_MCP_SOCKET_PATH".to_string(),
            serde_json::Value::String(sp.to_string_lossy().to_string()),
        );
    }
    serde_json::json!({
        "mcpServers": {
            "maho-browser": {
                "type": "stdio",
                "command": binary.to_string_lossy(),
                "env": env,
                "enabled": true,
                "directTools": true,
                "exposure": "direct",
                "lifecycle": "keep-alive"
            }
        }
    })
}

fn map_rpc_event(event: &RpcEvent) -> Option<AgentStreamEvent> {
    match event {
        RpcEvent::MessageUpdate {
            assistant_message_event: AssistantMessageEvent::TextDelta { delta, .. },
            ..
        }
        | RpcEvent::Token { token: delta, .. } => Some(AgentStreamEvent::Token(delta.clone())),
        RpcEvent::Assistant {
            delta: Some(delta), ..
        } => Some(AgentStreamEvent::Token(delta.clone())),
        RpcEvent::Assistant {
            delta: None,
            text: Some(text),
            ..
        } => Some(AgentStreamEvent::Token(text.clone())),
        RpcEvent::MessageUpdate {
            assistant_message_event: AssistantMessageEvent::ThinkingDelta { delta, .. },
            ..
        } => Some(AgentStreamEvent::Thinking(delta.clone())),
        RpcEvent::MessageUpdate {
            assistant_message_event:
                AssistantMessageEvent::ToolcallEnd {
                    tool_call: Some(tool_call),
                    ..
                },
            ..
        } => map_tool_call(tool_call),
        RpcEvent::ToolExecutionStart {
            tool_call_id,
            tool_name,
            args,
            ..
        } => {
            let args_str = args
                .as_ref()
                .map(serde_json::Value::to_string)
                .unwrap_or_else(|| "{}".to_string());
            Some(AgentStreamEvent::ToolCall {
                id: tool_call_id.clone(),
                name: tool_name.clone(),
                args: args_str,
            })
        }
        RpcEvent::ToolExecutionEnd {
            tool_call_id,
            tool_name,
            result,
            is_error,
            ..
        } => {
            let result_str = match result {
                Some(serde_json::Value::String(s)) => s.clone(),
                Some(val) => val.to_string(),
                None => String::new(),
            };
            Some(AgentStreamEvent::ToolResult {
                id: tool_call_id.clone(),
                name: tool_name.clone(),
                result: result_str,
                succeeded: !*is_error,
            })
        }
        _ => None,
    }
}

fn map_tool_call(tool_call: &serde_json::Value) -> Option<AgentStreamEvent> {
    let id = tool_call.get("id")?.as_str()?.to_string();
    let name = tool_call
        .get("name")
        .or_else(|| tool_call.get("toolName"))?
        .as_str()?
        .to_string();
    let args = tool_call
        .get("arguments")
        .or_else(|| tool_call.get("args"))
        .map(serde_json::Value::to_string)
        .unwrap_or_else(|| "{}".to_string());
    Some(AgentStreamEvent::ToolCall { id, name, args })
}

pub struct OmoBackend {
    config: OmoLaunchConfig,
    browser_mcp_binary: PathBuf,
    cwd: PathBuf,
    process: tokio::sync::Mutex<Option<OmoProcess>>,
    connection: tokio::sync::Mutex<Option<Arc<RpcConnection>>>,
    sessions: tokio::sync::Mutex<HashMap<String, String>>,
    turn_locks: tokio::sync::Mutex<HashMap<String, Arc<tokio::sync::Mutex<()>>>>,
    pub(crate) permission_callback: Mutex<Option<PermissionCallback>>,
    pub(crate) secure_storage_callback: Mutex<Option<SecureStorageCallback>>,
    pub(crate) browser_tool_bridge: Mutex<Option<Arc<dyn crate::BrowserToolBridge>>>,
    run_journal: Arc<Mutex<RunJournal>>,
    preferred_provider: Mutex<Option<String>>,
    runtime_tier: Mutex<Option<String>>,
    active_space_id: Mutex<Option<String>>,
    system_prompt: Mutex<String>,
    fs_whitelist_roots: Mutex<Vec<String>>,
    artifact_root: Mutex<Option<PathBuf>>,
    composite_cfg: Mutex<crate::composite::CompositeExecutionConfig>,
}

impl OmoBackend {
    pub fn new(
        config: OmoLaunchConfig,
        browser_mcp_binary: impl Into<PathBuf>,
        cwd: impl Into<PathBuf>,
    ) -> Self {
        Self {
            config,
            browser_mcp_binary: browser_mcp_binary.into(),
            cwd: cwd.into(),
            process: tokio::sync::Mutex::new(None),
            connection: tokio::sync::Mutex::new(None),
            sessions: tokio::sync::Mutex::new(HashMap::new()),
            turn_locks: tokio::sync::Mutex::new(HashMap::new()),
            permission_callback: Mutex::new(None),
            secure_storage_callback: Mutex::new(None),
            browser_tool_bridge: Mutex::new(None),
            run_journal: Arc::new(Mutex::new(RunJournal::new())),
            preferred_provider: Mutex::new(None),
            runtime_tier: Mutex::new(None),
            active_space_id: Mutex::new(None),
            system_prompt: Mutex::new(crate::system_prompt::MAHO_BROWSER_SYSTEM_PROMPT.to_string()),
            fs_whitelist_roots: Mutex::new(Vec::new()),
            artifact_root: Mutex::new(None),
            composite_cfg: Mutex::new(crate::composite::CompositeExecutionConfig::default()),
        }
    }

    pub fn composite_config(&self) -> crate::composite::CompositeExecutionConfig {
        lock_unpoison(&self.composite_cfg).clone()
    }

    pub fn set_composite_config(&self, config: crate::composite::CompositeExecutionConfig) {
        *lock_unpoison(&self.composite_cfg) = config;
    }

    pub fn system_prompt(&self) -> Option<String> {
        Some(lock_unpoison(&self.system_prompt).clone())
    }

    pub fn runtime_tier(&self) -> Option<String> {
        lock_unpoison(&self.runtime_tier).clone()
    }

    pub fn fs_whitelist_roots(&self) -> Vec<String> {
        lock_unpoison(&self.fs_whitelist_roots).clone()
    }

    pub fn artifact_root(&self) -> Option<PathBuf> {
        lock_unpoison(&self.artifact_root).clone()
    }

    pub fn active_space_id(&self) -> Option<String> {
        lock_unpoison(&self.active_space_id).clone()
    }

    pub fn browser_socket_path(&self) -> Option<PathBuf> {
        resolve_browser_socket_path(&self.cwd)
    }

    pub fn mcp_config(&self) -> serde_json::Value {
        let socket_path = self.browser_socket_path();
        browser_mcp_config(&self.browser_mcp_binary, socket_path.as_deref())
    }

    async fn connection(&self) -> Result<Arc<RpcConnection>, AgentError> {
        let mut connection_guard = self.connection.lock().await;
        if let Some(connection) = connection_guard.as_ref().cloned() {
            return Ok(connection);
        }

        let mut process_guard = self.process.lock().await;
        if process_guard.is_none() {
            self.write_mcp_config()?;
            self.write_models_config()?;
            self.write_settings_config()?;
            let prompt_text = self.system_prompt.lock().unwrap().clone();
            let _ = self.write_system_prompt(&prompt_text);
            let process = OmoProcess::spawn(
                self.config.runtime_binary(),
                self.config.rpc_entry(),
                self.config.socket_path(),
                &self.config,
            )
            .await?;
            *process_guard = Some(process);
        }

        let stream = UnixStream::connect(self.config.socket_path())
            .await
            .map_err(|error| {
                AgentError::ExecutionError(format!("failed to connect to omo RPC socket: {error}"))
            })?;
        let connection = RpcConnection::new(stream);
        *connection_guard = Some(Arc::clone(&connection));
        Ok(connection)
    }

    fn write_mcp_config(&self) -> Result<(), AgentError> {
        std::fs::create_dir_all(self.config.agent_dir()).map_err(|error| {
            AgentError::ExecutionError(format!("failed to create omo agent directory: {error}"))
        })?;
        let path = self.config.agent_dir().join("mcp.json");
        let bytes = serde_json::to_vec_pretty(&self.mcp_config())
            .map_err(|error| AgentError::ExecutionError(error.to_string()))?;
        std::fs::write(&path, bytes).map_err(|error| {
            AgentError::ExecutionError(format!(
                "failed to write omo MCP config at {}: {error}",
                path.display()
            ))
        })
    }

    pub fn write_system_prompt(&self, prompt: &str) -> Result<(), AgentError> {
        std::fs::create_dir_all(self.config.agent_dir()).map_err(|error| {
            AgentError::ExecutionError(format!("failed to create omo agent directory: {error}"))
        })?;
        let path = self.config.agent_dir().join("system_prompt.txt");
        let agents_md_path = self.config.agent_dir().join("AGENTS.md");
        std::fs::write(&path, prompt).map_err(|error| {
            AgentError::ExecutionError(format!(
                "failed to write system prompt at {}: {error}",
                path.display()
            ))
        })?;
        std::fs::write(&agents_md_path, prompt).map_err(|error| {
            AgentError::ExecutionError(format!(
                "failed to write AGENTS.md at {}: {error}",
                agents_md_path.display()
            ))
        })?;
        Ok(())
    }

    /// Disables omo's built-in tools.
    ///
    /// Maho's agent reaches the browser through MCP, so omo's filesystem and shell
    /// built-ins are both unwanted capability and an outright failure: omo's `eval`
    /// schema is rejected by Anthropic-backed endpoints (`tools.N.custom.input_schema:
    /// JSON schema is invalid`), which omo then reports as an empty assistant message.
    fn write_settings_config(&self) -> Result<(), AgentError> {
        std::fs::create_dir_all(self.config.agent_dir()).map_err(|error| {
            AgentError::ExecutionError(format!("failed to create omo agent directory: {error}"))
        })?;
        let path = self.config.agent_dir().join("settings.json");
        let bytes = serde_json::to_vec_pretty(&serde_json::json!({}))
            .map_err(|error| AgentError::ExecutionError(error.to_string()))?;
        std::fs::write(&path, bytes).map_err(|error| {
            AgentError::ExecutionError(format!(
                "failed to write omo settings at {}: {error}",
                path.display()
            ))
        })
    }

    fn write_models_config(&self) -> Result<(), AgentError> {
        let path = self.config.agent_dir().join("models.json");
        let provider_opt = if let Some(provider) = self.config.custom_provider() {
            Some(provider.clone())
        } else if let Some(ref cb) = *self.secure_storage_callback.lock().unwrap() {
            let preferred = self.preferred_provider.lock().unwrap().clone();
            let provider_name = preferred.as_deref().unwrap_or("openai-compatible");
            match cb(provider_name) {
                Ok(bundle) => {
                    if let Some(base_url) = bundle.base_url {
                        let model = bundle.model.unwrap_or_else(|| "default".to_string());
                        Some(crate::omo::config::CustomProvider {
                            name: provider_name.to_string(),
                            base_url,
                            api_key: bundle.key.as_str().to_string(),
                            models: vec![model],
                        })
                    } else {
                        None
                    }
                }
                Err(error) => {
                    tracing::warn!("failed to resolve secure credentials for {provider_name}: {error}");
                    None
                }
            }
        } else {
            None
        };

        let Some(provider) = provider_opt else {
            if path.exists() {
                std::fs::remove_file(&path).map_err(|error| {
                    AgentError::ExecutionError(format!(
                        "failed to remove stale omo models config at {}: {error}",
                        path.display()
                    ))
                })?;
            }
            return Ok(());
        };
        std::fs::create_dir_all(self.config.agent_dir()).map_err(|error| {
            AgentError::ExecutionError(format!("failed to create omo agent directory: {error}"))
        })?;
        let bytes = serde_json::to_vec_pretty(&provider.models_json())
            .map_err(|error| AgentError::ExecutionError(error.to_string()))?;

        #[cfg(unix)]
        {
            use std::io::Write;
            use std::os::unix::fs::OpenOptionsExt;
            use std::os::unix::fs::PermissionsExt;
            if path.exists() {
                std::fs::remove_file(&path).map_err(|error| {
                    AgentError::ExecutionError(format!(
                        "failed to remove pre-existing omo models config at {}: {error}",
                        path.display()
                    ))
                })?;
            }
            let mut file = std::fs::OpenOptions::new()
                .write(true)
                .create_new(true)
                .mode(0o600)
                .open(&path)
                .map_err(|error| {
                    AgentError::ExecutionError(format!(
                        "failed to create omo models config with 0600 permissions at {}: {error}",
                        path.display()
                    ))
                })?;
            file.write_all(&bytes).map_err(|error| {
                AgentError::ExecutionError(format!(
                    "failed to write omo models config at {}: {error}",
                    path.display()
                ))
            })?;
            let _ = std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o600));
        }
        #[cfg(not(unix))]
        {
            std::fs::write(&path, bytes).map_err(|error| {
                AgentError::ExecutionError(format!(
                    "failed to write omo models config at {}: {error}",
                    path.display()
                ))
            })?;
        }
        Ok(())
    }

    async fn rpc_session(
        &self,
        maho_session_id: &str,
        connection: &RpcConnection,
    ) -> Result<String, AgentError> {
        if let Some(session_id) = self.sessions.lock().await.get(maho_session_id).cloned() {
            return Ok(session_id);
        }

        let params = self.config.open_session_params();
        let preferred = self.preferred_provider.lock().unwrap().clone();
        let provider = preferred.or_else(|| params.provider);
        let session_file = self.config.session_dir().join(format!("{maho_session_id}.jsonl"));
        if !session_file.exists() {
            if let Some(parent) = session_file.parent() {
                let _ = std::fs::create_dir_all(parent);
            }
            let header = serde_json::json!({
                "type": "session",
                "version": 3,
                "id": maho_session_id,
                "timestamp": chrono::Utc::now().to_rfc3339_opts(chrono::SecondsFormat::Millis, true),
                "cwd": self.cwd.to_string_lossy(),
            });
            let _ = std::fs::write(&session_file, format!("{header}\n"));
        }
        let session_path = Some(session_file.to_string_lossy().to_string());

        let command = RpcCommand::OpenSession {
            id: None,
            session_path,
            cwd: Some(self.cwd.to_string_lossy().to_string()),
            provider,
            model_id: params.model_id,
            thinking_level: params.thinking_level.and_then(|s| s.parse().ok()),
            permission_preset: Some(params.permission_preset),
        };
        let response = require_success(connection.request(command).await?)?;
        let session_id = response
            .data
            .as_ref()
            .and_then(|data| data.get("sessionId"))
            .and_then(serde_json::Value::as_str)
            .ok_or_else(|| {
                AgentError::ExecutionError(
                    "omo open_session response omitted data.sessionId".to_string(),
                )
            })?
            .to_string();
        self.sessions
            .lock()
            .await
            .insert(maho_session_id.to_string(), session_id.clone());
        Ok(session_id)
    }

    pub async fn get_auth_providers(&self) -> Result<Vec<RpcAuthProvider>, AgentError> {
        let connection = self.connection().await?;
        let rpc_session_id = self.rpc_session("auth-session", &connection).await?;
        let response = require_success(
            connection
                .request(RpcCommand::get_auth_providers(rpc_session_id))
                .await?,
        )?;
        let data = response.data.unwrap_or(serde_json::Value::Null);
        let providers = data.get("providers").cloned().unwrap_or(data);
        serde_json::from_value(providers)
            .map_err(|error| AgentError::ExecutionError(error.to_string()))
    }

    pub async fn login_start(&self, provider: &str) -> Result<serde_json::Value, AgentError> {
        let connection = self.connection().await?;
        let rpc_session_id = self.rpc_session("auth-session", &connection).await?;
        let response = require_success(
            connection
                .request(RpcCommand::login_start(rpc_session_id, provider))
                .await?,
        )?;
        Ok(response.data.unwrap_or(serde_json::Value::Null))
    }

    pub async fn login_cancel(&self, provider: &str) -> Result<serde_json::Value, AgentError> {
        let connection = self.connection().await?;
        let rpc_session_id = self.rpc_session("auth-session", &connection).await?;
        let response = require_success(
            connection
                .request(RpcCommand::login_cancel(rpc_session_id, provider))
                .await?,
        )?;
        Ok(response.data.unwrap_or(serde_json::Value::Null))
    }

    pub async fn login_api_key(
        &self,
        provider: &str,
        key: &str,
    ) -> Result<serde_json::Value, AgentError> {
        let connection = self.connection().await?;
        let rpc_session_id = self.rpc_session("auth-session", &connection).await?;
        let response = require_success(
            connection
                .request(RpcCommand::login_api_key(rpc_session_id, provider, key))
                .await?,
        )?;
        Ok(response.data.unwrap_or(serde_json::Value::Null))
    }

    async fn turn_lock(&self, session_id: &str) -> Arc<tokio::sync::Mutex<()>> {
        self.turn_locks
            .lock()
            .await
            .entry(session_id.to_string())
            .or_insert_with(|| Arc::new(tokio::sync::Mutex::new(())))
            .clone()
    }
}

fn require_success(response: RpcResponse) -> Result<RpcResponse, AgentError> {
    if response.success {
        Ok(response)
    } else {
        Err(AgentError::ExecutionError(response.error.unwrap_or_else(
            || format!("omo RPC command {} failed", response.command),
        )))
    }
}

fn prompt_parts(content: ChatContent) -> (String, Option<Vec<ImageContent>>) {
    match content {
        ChatContent::Text(text) => (text, None),
        ChatContent::Image { mime, data } => (String::new(), Some(vec![rpc_image(mime, data)])),
        ChatContent::Mixed(parts) => {
            let mut text = String::new();
            let mut images = Vec::new();
            for part in parts {
                match part {
                    ChatContentPart::Text(part) => text.push_str(&part),
                    ChatContentPart::Image { mime, data } => images.push(rpc_image(mime, data)),
                }
            }
            (text, (!images.is_empty()).then_some(images))
        }
    }
}

fn rpc_image(mime: String, data: Vec<u8>) -> ImageContent {
    ImageContent {
        content_type: "image".to_string(),
        data: base64::engine::general_purpose::STANDARD.encode(data),
        mime_type: mime,
    }
}

fn event_error(event: &RpcEvent) -> Option<String> {
    match event {
        RpcEvent::Error { error, .. } => Some(error.clone()),
        RpcEvent::ContinuationError { error_message, .. } => Some(error_message.clone()),
        RpcEvent::MessageUpdate {
            assistant_message_event: AssistantMessageEvent::Error { reason },
            ..
        } => Some(
            reason
                .clone()
                .unwrap_or_else(|| "omo model turn failed".to_string()),
        ),
        RpcEvent::SessionAbort { .. } => Some("omo session turn was aborted".to_string()),
        _ => None,
    }
}

fn event_is_terminal(event: &RpcEvent) -> bool {
    matches!(
        event,
        RpcEvent::AgentEnd { .. } | RpcEvent::SessionAbort { .. }
    )
}

#[async_trait]
impl AgentRuntime for OmoBackend {
    async fn run_turn(
        &self,
        session_id: &str,
        message: ChatMessage,
        on_token: Option<Box<dyn for<'a> Fn(&'a str) + Send + Sync + 'static>>,
        on_event: Option<EventCallback>,
    ) -> Result<ChatMessage, AgentError> {
        let turn_lock = self.turn_lock(session_id).await;
        let _turn_guard = turn_lock.lock().await;
        let connection = self.connection().await?;
        let rpc_session_id = self.rpc_session(session_id, &connection).await?;
        let mut events = connection.events.subscribe();
        let (prompt, images) = prompt_parts(message.content);
        let command = RpcCommand::Prompt {
            id: None,
            session_id: Some(rpc_session_id.clone()),
            message: prompt,
            images,
            streaming_behavior: None,
            thinking_level: None,
            session_title_prompt: None,
            expand_prompt_templates: None,
        };
        require_success(connection.request(command).await?)?;

        let run_id = AgentRunId::new(format!("{session_id}:{}", uuid::Uuid::new_v4()));
        let _ = lock_unpoison(&self.run_journal).start_run(run_id.clone(), session_id);
        let mut output = String::new();
        let mut turn_error = None;
        let wait = async {
            loop {
                let event = events.recv().await.map_err(|error| {
                    AgentError::ExecutionError(format!("omo RPC event stream ended: {error}"))
                })?;
                if event.session_id() != Some(rpc_session_id.as_str()) {
                    continue;
                }
                if let Some(error) = event_error(&event) {
                    turn_error = Some(error);
                }
                if let Some(mapped) = map_rpc_event(&event) {
                    if let AgentStreamEvent::Token(ref token) = mapped {
                        output.push_str(token);
                        if let Some(ref callback) = on_token {
                            callback(token);
                        }
                    }
                    if let Some(ref callback) = on_event {
                        callback(mapped);
                    }
                }
                if event_is_terminal(&event) {
                    return Ok::<(), AgentError>(());
                }
            }
        };
        match tokio::time::timeout(TURN_TIMEOUT, wait).await {
            Ok(result) => result?,
            Err(_) => {
                let _ = connection.request(RpcCommand::abort(&rpc_session_id)).await;
                let _ = tokio::time::timeout(Duration::from_secs(2), async {
                    while let Ok(ev) = events.recv().await {
                        if ev.session_id() == Some(rpc_session_id.as_str()) && event_is_terminal(&ev) {
                            return true;
                        }
                    }
                    false
                })
                .await;

                // Always invalidate the session mapping on timeout so that subsequent turns
                // start with a fresh, isolated RPC session.
                self.sessions.lock().await.remove(session_id);

                return Err(AgentError::ExecutionError(format!(
                    "omo turn timed out after {TURN_TIMEOUT:?}"
                )));
            }
        }

        if let Some(error) = turn_error {
            let _ = lock_unpoison(&self.run_journal).record_terminal(
                &run_id,
                RunCheckpointKind::Failed,
                Some(serde_json::json!({ "error": error })),
            );
            return Err(AgentError::ExecutionError(error));
        }
        let _ = lock_unpoison(&self.run_journal).record_terminal(
            &run_id,
            RunCheckpointKind::Completed,
            None,
        );
        Ok(ChatMessage::assistant(ChatContent::text(output)))
    }

    fn set_browser_tool_bridge(
        &self,
        bridge: Arc<dyn crate::BrowserToolBridge>,
    ) -> Result<(), AgentError> {
        let mut guard = self.browser_tool_bridge.lock().unwrap();
        *guard = Some(bridge);
        Ok(())
    }

    async fn cancel(&self, session_id: &str) -> Result<(), AgentError> {
        let connection = self.connection().await?;
        let rpc_session_id = self
            .sessions
            .lock()
            .await
            .get(session_id)
            .cloned()
            .ok_or_else(|| AgentError::SessionNotFound(session_id.to_string()))?;
        require_success(
            connection
                .request(RpcCommand::abort(rpc_session_id))
                .await?,
        )?;
        Ok(())
    }

    async fn list_tools(&self) -> Result<Vec<ToolDefinition>, AgentError> {
        // omo's RPC protocol exposes no tool-list command, so the only truthful source for
        // the browser tools this agent can reach is the MCP bridge itself. An unreachable
        // browser yields platform fallback tools rather than failing the session.
        let socket_path = self.browser_socket_path();
        let tools = browser_mcp_tools(
            &self.browser_mcp_binary,
            socket_path.as_deref(),
            MCP_LIST_TIMEOUT,
        )
        .await
        .unwrap_or_default();
        if !tools.is_empty() {
            Ok(tools)
        } else {
            Ok(crate::tool_registry::get_platform_tools())
        }
    }

    fn set_permission_callback(&self, callback: PermissionCallback) -> Result<(), AgentError> {
        *lock_unpoison(&self.permission_callback) = Some(callback);
        Ok(())
    }

    fn set_secure_storage_callback(
        &self,
        callback: SecureStorageCallback,
    ) -> Result<(), AgentError> {
        *lock_unpoison(&self.secure_storage_callback) = Some(callback);
        Ok(())
    }

    fn run_journal(&self) -> Arc<Mutex<RunJournal>> {
        Arc::clone(&self.run_journal)
    }

    fn set_preferred_provider(&self, provider: Option<String>) {
        let mut guard = self.preferred_provider.lock().unwrap();
        *guard = provider;
    }

    fn set_runtime_tier(&self, tier: Option<String>) {
        let mut guard = self.runtime_tier.lock().unwrap();
        *guard = tier;
    }

    fn runtime_tier(&self) -> Option<String> {
        self.runtime_tier.lock().unwrap().clone()
    }

    fn set_active_space_id(&self, space_id: Option<String>) {
        let mut guard = self.active_space_id.lock().unwrap();
        *guard = space_id;
    }

    fn set_fs_whitelist_roots(&self, roots: Vec<String>) {
        *lock_unpoison(&self.fs_whitelist_roots) = roots;
    }

    fn fs_whitelist_roots(&self) -> Vec<String> {
        lock_unpoison(&self.fs_whitelist_roots).clone()
    }

    fn set_system_prompt(&self, prompt: &str) {
        let effective_prompt = if prompt.trim().is_empty() {
            crate::system_prompt::MAHO_BROWSER_SYSTEM_PROMPT
        } else {
            prompt
        };
        let mut guard = self.system_prompt.lock().unwrap();
        *guard = effective_prompt.to_string();
        let _ = self.write_system_prompt(effective_prompt);
    }

    fn system_prompt(&self) -> Option<String> {
        Some(lock_unpoison(&self.system_prompt).clone())
    }

    fn set_composite_config(&self, config: crate::composite::CompositeExecutionConfig) {
        self.set_composite_config(config);
    }

    fn composite_config(&self) -> crate::composite::CompositeExecutionConfig {
        self.composite_config()
    }

    fn set_artifact_root(&self, artifact_root: Option<PathBuf>) {
        *lock_unpoison(&self.artifact_root) = artifact_root;
    }

    fn artifact_root(&self) -> Option<PathBuf> {
        self.artifact_root()
    }
}

#[cfg(test)]
mod tests {
    use std::path::Path;

    use super::*;

    #[test]
    fn maps_rpc_text_and_thinking_deltas() {
        let token = map_rpc_event(&RpcEvent::MessageUpdate {
            session_id: Some("rpc-1".to_string()),
            assistant_message_event: AssistantMessageEvent::TextDelta {
                content_index: 0,
                delta: "hello".to_string(),
            },
            usage: None,
        });
        assert_eq!(token, Some(AgentStreamEvent::Token("hello".to_string())));

        let thinking = map_rpc_event(&RpcEvent::MessageUpdate {
            session_id: Some("rpc-1".to_string()),
            assistant_message_event: AssistantMessageEvent::ThinkingDelta {
                content_index: 0,
                delta: "checking".to_string(),
            },
            usage: None,
        });
        assert_eq!(
            thinking,
            Some(AgentStreamEvent::Thinking("checking".to_string()))
        );
    }

    #[test]
    fn extension_ui_widget_and_status_requests_are_ignored() {
        for method in ["setWidget", "setStatus"] {
            let raw = format!(
                r#"{{"type":"extension_ui_request","sessionId":"rpc-1","method":"{method}","widgetKey":"todo-sidebar"}}"#
            );
            let value: serde_json::Value = serde_json::from_str(&raw).expect("valid JSON");
            assert!(is_ui_chrome_request(&value));
            let event: RpcEvent = serde_json::from_value(value).expect("valid RPC event");
            assert_eq!(event, RpcEvent::Other);
            assert_eq!(map_rpc_event(&event), None);
        }
    }

    #[test]
    fn parse_mcp_tools_filters_and_marks_permissions_accurately() {
        let message = serde_json::json!({
            "id": 1,
            "result": {
                "tools": [
                    { "name": "browser_navigate", "description": "Navigate a tab" },
                    { "name": "browser_tab_list", "description": "List open tabs" },
                    { "name": "browser_file_upload_select", "description": "Upload file" },
                    { "name": "mail_send", "description": "Send email" },
                    { "name": "vault_get_password", "description": "Access vault" }
                ]
            }
        });
        let tools = parse_mcp_tools(&message).expect("listing parses");
        assert_eq!(tools.len(), 5);

        let nav = tools.iter().find(|t| t.name == "browser_navigate").unwrap();
        assert_eq!(nav.permission, maho_types::tool::ToolPermission::AutoApprove);
        assert!(!nav.sensitive);

        let upload = tools.iter().find(|t| t.name == "browser_file_upload_select").unwrap();
        assert_eq!(upload.permission, maho_types::tool::ToolPermission::AlwaysAsk);
        assert!(upload.sensitive);

        let mail = tools.iter().find(|t| t.name == "mail_send").unwrap();
        assert_eq!(mail.permission, maho_types::tool::ToolPermission::AlwaysAsk);
        assert!(mail.sensitive);

        let vault = tools.iter().find(|t| t.name == "vault_get_password").unwrap();
        assert_eq!(vault.permission, maho_types::tool::ToolPermission::AlwaysAsk);
        assert!(vault.sensitive);
    }

    #[tokio::test]
    async fn list_tools_degrades_to_empty_when_bridge_is_unreachable() {
        let started = std::time::Instant::now();
        let tools = browser_mcp_tools(
            Path::new("/nonexistent/maho-browser-mcp"),
            None,
            Duration::from_secs(5),
        )
        .await;

        assert!(tools.is_none(), "an unreachable bridge must not yield tools");
        assert!(
            started.elapsed() < Duration::from_secs(5),
            "must fail fast rather than burn the whole timeout"
        );
    }

    #[test]
    fn browser_manipulation_never_asks_but_boundary_capabilities_do() {
        let message = serde_json::json!({
            "id": 2,
            "result": { "tools": [
                { "name": "input.locator_click" },
                { "name": "browser.navigate" },
                { "name": "browser.snapshot" },
                { "name": "browser_tab_list" },
                { "name": "browser_page_content" },
                { "name": "mail.send" },
                { "name": "mail_send" },
                { "name": "vault.read" },
                { "name": "vault_get_password" },
                { "name": "artifact.export" },
                { "name": "artifact_export" },
                { "name": "browser_file_upload_select" },
                { "name": "browser.file_upload_select" },
                { "name": "input.file_upload_select" },
                { "name": "input_file_upload_select" },
                { "name": "browser.visual_click" },
                { "name": "browser_visual_click" }
            ]}
        });

        let tools = parse_mcp_tools(&message).expect("listing parses");
        let permission = |name: &str| {
            tools
                .iter()
                .find(|t| t.name == name)
                .map(|t| t.permission.clone())
                .expect("tool present")
        };

        for inside in [
            "input.locator_click",
            "browser.navigate",
            "browser.snapshot",
            "browser_tab_list",
            "browser_page_content",
        ] {
            assert_eq!(
                permission(inside),
                maho_types::tool::ToolPermission::AutoApprove,
                "browser manipulation must never ask for approval: {inside}"
            );
        }

        for boundary in [
            "mail.send",
            "mail_send",
            "vault.read",
            "vault_get_password",
            "artifact.export",
            "artifact_export",
            "browser_file_upload_select",
            "browser.file_upload_select",
            "input.file_upload_select",
            "input_file_upload_select",
            "browser.visual_click",
            "browser_visual_click",
        ] {
            assert_eq!(
                permission(boundary),
                maho_types::tool::ToolPermission::AlwaysAsk,
                "boundary capability must ask for approval: {boundary}"
            );
        }
    }

    #[test]
    fn parse_mcp_tools_maps_listing_into_tool_definitions() {
        let message = serde_json::json!({
            "id": 2,
            "result": { "tools": [
                { "name": "browser_navigate", "description": "Navigate a tab",
                  "inputSchema": { "type": "object", "properties": { "url": { "type": "string" } } } },
                { "name": "browser_tab_list" }
            ]}
        });

        let tools = parse_mcp_tools(&message).expect("listing parses");
        assert_eq!(tools.len(), 2);
        assert_eq!(tools[0].name, "browser_navigate");
        assert_eq!(tools[0].description, "Navigate a tab");
        assert_eq!(tools[1].name, "browser_tab_list");
        assert_eq!(tools[1].parameters_schema["type"], "object");
    }

    #[test]
    fn mcp_config_contains_injected_browser_server_binary() {
        let config = browser_mcp_config(Path::new("/opt/maho/bin/maho-browser-mcp"), None);
        assert_eq!(config["mcpServers"]["maho-browser"]["type"], "stdio");
        assert_eq!(
            config["mcpServers"]["maho-browser"]["command"],
            "/opt/maho/bin/maho-browser-mcp"
        );
        assert_eq!(
            config["mcpServers"]["maho-browser"]["env"]["MAHO_MCP_PUBLIC_ENABLED"],
            "true"
        );
        assert_eq!(
            config["mcpServers"]["maho-browser"]["env"]["MAHO_MCP_AUTO_APPROVE"],
            "true"
        );
        assert!(config["mcpServers"]["maho-browser"]["env"]
            .get("MAHO_MCP_SOCKET_PATH")
            .is_none());

        let config_with_sock = browser_mcp_config(
            Path::new("/opt/maho/bin/maho-browser-mcp"),
            Some(Path::new("/tmp/test_maho.sock")),
        );
        assert_eq!(
            config_with_sock["mcpServers"]["maho-browser"]["env"]["MAHO_MCP_SOCKET_PATH"],
            "/tmp/test_maho.sock"
        );
    }

    #[test]
    fn maps_tool_execution_events() {
        let start_event = RpcEvent::ToolExecutionStart {
            session_id: Some("session-1".to_string()),
            tool_call_id: "call-1".to_string(),
            tool_name: "browser_tab_list".to_string(),
            args: Some(serde_json::json!({})),
        };
        assert_eq!(
            map_rpc_event(&start_event),
            Some(AgentStreamEvent::ToolCall {
                id: "call-1".to_string(),
                name: "browser_tab_list".to_string(),
                args: "{}".to_string(),
            })
        );

        let end_event = RpcEvent::ToolExecutionEnd {
            session_id: Some("session-1".to_string()),
            tool_call_id: "call-1".to_string(),
            tool_name: "browser_tab_list".to_string(),
            result: Some(serde_json::json!({"status": "ok"})),
            is_error: false,
        };
        assert_eq!(
            map_rpc_event(&end_event),
            Some(AgentStreamEvent::ToolResult {
                id: "call-1".to_string(),
                name: "browser_tab_list".to_string(),
                result: "{\"status\":\"ok\"}".to_string(),
                succeeded: true,
            })
        );
    }

    #[test]
    fn set_system_prompt_empty_falls_back_to_default() {
        let temp_dir = tempfile::tempdir().unwrap();
        let config = OmoLaunchConfig::builder(temp_dir.path())
            .build()
            .unwrap();
        let backend = OmoBackend::new(
            config,
            temp_dir.path().join("bin/maho-mcp"),
            temp_dir.path().to_path_buf(),
        );
        backend.set_system_prompt("");
        let prompt_file = temp_dir.path().join("system_prompt.txt");
        assert!(prompt_file.exists());
        let content = std::fs::read_to_string(&prompt_file).unwrap();
        assert!(!content.is_empty());
        assert_eq!(content, crate::system_prompt::MAHO_BROWSER_SYSTEM_PROMPT);
    }
}
