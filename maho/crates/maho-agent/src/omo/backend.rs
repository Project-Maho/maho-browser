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
use tokio::sync::{broadcast, oneshot, watch};

use crate::omo::config::OmoLaunchConfig;
use crate::omo::process::OmoProcess;
use crate::omo::proto::{
    AssistantMessageEvent, FramingCodec, ImageContent, RpcAuthProvider, RpcCommand, RpcEvent,
    RpcInboundMessage, RpcResponse,
};
use crate::run_journal::{AgentRunId, RunCheckpointKind, RunJournal};
use crate::{
    AgentError, AgentRuntime, AgentStorage, AgentStreamEvent,
    PermissionCallback, SecureKeyBundle, ToolDefinition,
};

const RESPONSE_TIMEOUT: Duration = Duration::from_secs(30);
const TURN_TIMEOUT: Duration = Duration::from_secs(300);
const MCP_LIST_TIMEOUT: Duration = Duration::from_secs(5);
/// Largest RPC frame accepted from omo. A longer line is discarded rather than buffered.
const MAX_RPC_FRAME_BYTES: usize = 16 * 1024 * 1024;

type EventCallback = Arc<dyn Fn(AgentStreamEvent) + Send + Sync + 'static>;
type SecureStorageCallback =
    Box<dyn Fn(&str) -> Result<SecureKeyBundle, AgentError> + Send + Sync + 'static>;

/// Marks the connection closed when dropped while armed. A request future dropped in the
/// middle of `write_all` leaves a partial frame on the socket; the next request must use a
/// fresh connection instead of appending to it.
struct ClosedUnlessDisarmed<'a> {
    closed: &'a watch::Sender<bool>,
    armed: bool,
}

impl Drop for ClosedUnlessDisarmed<'_> {
    fn drop(&mut self) {
        if self.armed {
            self.closed.send_replace(true);
        }
    }
}

struct RpcConnection {
    writer: tokio::sync::Mutex<OwnedWriteHalf>,
    pending: Arc<Mutex<HashMap<String, oneshot::Sender<RpcResponse>>>>,
    events: broadcast::Sender<RpcEvent>,
    next_id: AtomicU64,
    /// Maho session id -> omo session handle. Handles are scoped to this connection's omo
    /// process epoch, so the map lives and dies with the connection, and the reader drops an
    /// entry the moment omo announces the handle ended (`session_closed` / `session_parked`,
    /// e.g. its 30-minute idle eviction). A cached handle is therefore always one omo still
    /// serves.
    sessions: Arc<Mutex<HashMap<String, String>>>,
    /// Handles omo has announced as ended on this connection. Handles are unique per omo
    /// process epoch, so a handle seen here is dead forever; `rpc_session` consults it so an
    /// `open_session` reply the reader already saw closed is never cached.
    ended_handles: Arc<Mutex<std::collections::HashSet<String>>>,
    /// Flips to `true` once the socket is gone (reader hit EOF/error or a write failed).
    /// A closed connection is never reused: every omo session handle it carried is dead.
    closed: watch::Sender<bool>,
}

impl RpcConnection {
    fn new(stream: UnixStream) -> Arc<Self> {
        let (reader, writer) = stream.into_split();
        let (events, _) = broadcast::channel(1024);
        let (closed, _) = watch::channel(false);
        let connection = Arc::new(Self {
            writer: tokio::sync::Mutex::new(writer),
            pending: Arc::new(Mutex::new(HashMap::new())),
            events,
            next_id: AtomicU64::new(1),
            sessions: Arc::new(Mutex::new(HashMap::new())),
            ended_handles: Arc::new(Mutex::new(std::collections::HashSet::new())),
            closed,
        });
        tokio::spawn(read_rpc_messages(
            reader,
            Arc::clone(&connection.pending),
            connection.events.clone(),
            Arc::clone(&connection.sessions),
            Arc::clone(&connection.ended_handles),
            connection.closed.clone(),
        ));
        connection
    }

    fn is_closed(&self) -> bool {
        *self.closed.borrow()
    }

    async fn request(&self, command: RpcCommand) -> Result<RpcResponse, AgentError> {
        let id = self.next_id.fetch_add(1, Ordering::Relaxed).to_string();
        let bytes = FramingCodec::encode(&command.with_id(id.clone()))
            .map_err(|error| AgentError::ExecutionError(error.to_string()))?;
        let (sender, receiver) = oneshot::channel();
        lock_unpoison(&self.pending).insert(id.clone(), sender);

        let mut writer = self.writer.lock().await;
        if self.is_closed() {
            lock_unpoison(&self.pending).remove(&id);
            return Err(AgentError::ExecutionError(
                "omo RPC connection is closed".to_string(),
            ));
        }
        let mut write_guard = ClosedUnlessDisarmed {
            closed: &self.closed,
            armed: true,
        };
        if let Err(error) = writer.write_all(&bytes).await {
            lock_unpoison(&self.pending).remove(&id);
            return Err(AgentError::ExecutionError(format!(
                "failed to write omo RPC request: {error}"
            )));
        }
        write_guard.armed = false;
        drop(write_guard);
        drop(writer);

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
    sessions: Arc<Mutex<HashMap<String, String>>>,
    ended_handles: Arc<Mutex<std::collections::HashSet<String>>>,
    closed: watch::Sender<bool>,
) {
    let mut reader = BufReader::new(reader);
    loop {
        let line = match read_frame(&mut reader, MAX_RPC_FRAME_BYTES).await {
            Ok(Some(Frame::Line(line))) => line,
            Ok(Some(Frame::Skipped(reason))) => {
                tracing::warn!(target: "maho_agent::omo", reason, "discarding unreadable omo RPC frame");
                continue;
            }
            Ok(None) | Err(_) => break,
        };
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
                if let RpcEvent::SessionClosed { session_id, .. }
                | RpcEvent::SessionParked { session_id, .. } = &event
                {
                    // Evict before publishing, so anyone woken by this event already sees
                    // the handle gone.
                    lock_unpoison(&ended_handles).insert(session_id.clone());
                    lock_unpoison(&sessions).retain(|_, handle| handle != session_id);
                }
                let _ = events.send(event);
            }
            Err(error) => {
                tracing::debug!(target = "maho_agent::omo", %error, "ignoring unsupported omo RPC message");
            }
        }
    }
    closed.send_replace(true);
    lock_unpoison(&pending).clear();
}

enum Frame {
    Line(String),
    Skipped(&'static str),
}

/// Reads one newline-delimited frame without buffering more than `max` bytes of it.
/// Oversize and non-UTF-8 frames are consumed and reported as skipped so one bad frame
/// never tears down the connection. `Ok(None)` is end of stream.
async fn read_frame<R>(reader: &mut R, max: usize) -> std::io::Result<Option<Frame>>
where
    R: tokio::io::AsyncBufRead + Unpin,
{
    use tokio::io::AsyncReadExt;
    let mut buf = Vec::new();
    let limit = u64::try_from(max).unwrap_or(u64::MAX).saturating_add(1);
    let read = (&mut *reader).take(limit).read_until(b'\n', &mut buf).await?;
    if read == 0 {
        return Ok(None);
    }
    if buf.last() != Some(&b'\n') && buf.len() > max {
        // Drain the rest of the oversize line in bounded chunks.
        loop {
            let mut rest = Vec::new();
            let n = (&mut *reader).take(limit).read_until(b'\n', &mut rest).await?;
            if n == 0 || rest.last() == Some(&b'\n') {
                break;
            }
        }
        return Ok(Some(Frame::Skipped("frame exceeds size cap")));
    }
    if buf.last() == Some(&b'\n') {
        buf.pop();
        if buf.last() == Some(&b'\r') {
            buf.pop();
        }
    }
    match String::from_utf8(buf) {
        Ok(line) => Ok(Some(Frame::Line(line))),
        Err(_) => Ok(Some(Frame::Skipped("frame is not valid UTF-8"))),
    }
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
    const OFF_BROWSER_PREFIXES: [&str; 12] = [
        "mail.", "mail_",
        "vault.", "vault_",
        "routines.", "routines_",
        "control.", "control_",
        "policy.", "policy_",
        "import.", "browser_import_",
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
        || canonical_id == "browser.native_type"
        || canonical_id == "browser_native_type"
        || canonical_id == "browser.native_key"
        || canonical_id == "browser_native_key"
        || canonical_id == "page.download"
        || canonical_id == "browser_download"
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
        "MAHO_MCP_APPROVAL".to_string(),
        serde_json::Value::String("browser".to_string()),
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
                "lifecycle": "keep-alive",
                "startupTimeoutMs": MAHO_MCP_STARTUP_TIMEOUT_MS
            }
        }
    })
}

/// Binary name of Maho's engine tool server (js_eval, artifact_create, browser_script).
pub const AGENT_TOOLS_BINARY: &str = "maho-agent-tools";

// omo races a server's connect for only 250 ms before backgrounding it, so a cold Maho
// server would miss the first turn's tool list; Maho's own servers are local and fast,
// so waiting for them keeps their tools present from the first turn.
pub const MAHO_MCP_STARTUP_TIMEOUT_MS: u64 = 5_000;

/// Registers `maho-agent-tools` beside `maho-browser` when the build staged it next to
/// the browser MCP binary. Absent binary: the server is simply not offered.
pub fn add_agent_tools_server(
    config: &mut serde_json::Value,
    browser_mcp_binary: &Path,
    artifact_dir: &Path,
) {
    let Some(binary) = browser_mcp_binary
        .parent()
        .map(|dir| dir.join(AGENT_TOOLS_BINARY))
        .filter(|path| path.is_file())
    else {
        return;
    };
    let Some(servers) = config
        .get_mut("mcpServers")
        .and_then(serde_json::Value::as_object_mut)
    else {
        return;
    };
    // browser_script spawns its own maho-browser-mcp child, which inherits this env and so
    // needs the same dispatch/socket settings as the maho-browser server.
    let mut env = servers
        .get("maho-browser")
        .and_then(|server| server.get("env"))
        .and_then(serde_json::Value::as_object)
        .cloned()
        .unwrap_or_default();
    env.insert(
        "MAHO_AGENT_TOOLS_ARTIFACT_DIR".to_string(),
        artifact_dir.to_string_lossy().into(),
    );
    env.insert(
        "MAHO_BROWSER_MCP_BINARY".to_string(),
        browser_mcp_binary.to_string_lossy().into(),
    );
    if let Some(agent_dir) = artifact_dir.parent() {
        env.insert(
            crate::agent_tools::AUTOMATION_STORE_ENV.to_string(),
            maho_core::automations::AutomationStore::in_agent_dir(agent_dir)
                .path()
                .to_string_lossy()
                .into(),
        );
        // Search uses the Maho proxy token from models.json, read at call time.
        env.insert(
            crate::agent_tools::MODELS_FILE_ENV.to_string(),
            agent_dir.join("models.json").to_string_lossy().into(),
        );
        env.insert(
            crate::agent_tools::SEARCH_BASE_URL_ENV.to_string(),
            crate::proxy_url().into(),
        );
    }
    servers.insert(
        AGENT_TOOLS_BINARY.to_string(),
        serde_json::json!({
            "type": "stdio",
            "command": binary.to_string_lossy(),
            "args": [],
            "env": env,
            "enabled": true,
            "directTools": true,
            "exposure": "direct",
            "lifecycle": "keep-alive",
            "startupTimeoutMs": MAHO_MCP_STARTUP_TIMEOUT_MS
        }),
    );
}

/// `settings.json` for the isolated omo agent dir: loads the omo-ai plugin package when
/// present, with its skills/prompts/themes filtered out so only extensions (memory engine,
/// task, workpool) load.
pub fn omo_settings_json(plugin_dir: Option<&Path>) -> serde_json::Value {
    match plugin_dir {
        Some(dir) => serde_json::json!({
            "packages": [{
                "source": dir.to_string_lossy(),
                "skills": [],
                "prompts": [],
                "themes": []
            }]
        }),
        None => serde_json::json!({}),
    }
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
    pub(crate) config: OmoLaunchConfig,
    browser_mcp_binary: PathBuf,
    cwd: PathBuf,
    process: tokio::sync::Mutex<Option<OmoProcess>>,
    connection: tokio::sync::Mutex<Option<Arc<RpcConnection>>>,
    turn_locks: tokio::sync::Mutex<HashMap<String, Arc<tokio::sync::Mutex<()>>>>,
    pub(crate) permission_callback: Mutex<Option<PermissionCallback>>,
    approval_policy: Mutex<String>,
    pub(crate) secure_storage_callback: Mutex<Option<Arc<SecureStorageCallback>>>,
    pub(crate) browser_tool_bridge: Mutex<Option<Arc<dyn crate::BrowserToolBridge>>>,
    run_journal: Arc<Mutex<RunJournal>>,
    turn_timeout: Duration,
    preferred_provider: Mutex<Option<String>>,
    runtime_tier: Mutex<Option<String>>,
    active_space_id: Mutex<Option<String>>,
    system_prompt: Mutex<String>,
    fs_whitelist_roots: Mutex<Vec<String>>,
    artifact_root: Mutex<Option<PathBuf>>,
    storage: Mutex<Option<Arc<dyn AgentStorage>>>,
    composite_cfg: Mutex<crate::composite::CompositeExecutionConfig>,
    /// This backend's pending user interactions. Per backend, so one session's answer can
    /// never resolve another session's question.
    interaction_broker: Arc<Mutex<crate::interaction::InteractionBroker>>,
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
            turn_locks: tokio::sync::Mutex::new(HashMap::new()),
            permission_callback: Mutex::new(None),
            approval_policy: Mutex::new("prompt".to_string()),
            secure_storage_callback: Mutex::new(None),
            browser_tool_bridge: Mutex::new(None),
            run_journal: Arc::new(Mutex::new(RunJournal::new())),
            turn_timeout: TURN_TIMEOUT,
            preferred_provider: Mutex::new(None),
            runtime_tier: Mutex::new(None),
            active_space_id: Mutex::new(None),
            system_prompt: Mutex::new(crate::system_prompt::MAHO_BROWSER_SYSTEM_PROMPT.to_string()),
            fs_whitelist_roots: Mutex::new(Vec::new()),
            artifact_root: Mutex::new(None),
            storage: Mutex::new(None),
            composite_cfg: Mutex::new(crate::composite::CompositeExecutionConfig::default()),
            interaction_broker: Arc::new(Mutex::new(crate::interaction::InteractionBroker::new())),
        }
    }

    /// Attach backing agent storage for artifact and setting queries.
    pub fn set_storage(&self, storage: Arc<dyn AgentStorage>) {
        *lock_unpoison(&self.storage) = Some(storage);
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
        let mut config = browser_mcp_config(&self.browser_mcp_binary, socket_path.as_deref());
        add_agent_tools_server(
            &mut config,
            &self.browser_mcp_binary,
            &self.config.agent_dir().join("artifacts"),
        );
        config
    }

    async fn connection(&self) -> Result<Arc<RpcConnection>, AgentError> {
        let mut connection_guard = self.connection.lock().await;
        if let Some(connection) = connection_guard.as_ref() {
            if !connection.is_closed() {
                return Ok(Arc::clone(connection));
            }
            // The socket died (omo exited or dropped us); its session handles died with it.
            *connection_guard = None;
        }

        let mut process_guard = self.process.lock().await;
        if process_guard.as_mut().is_some_and(OmoProcess::has_exited) {
            *process_guard = None;
        }
        if process_guard.is_none() {
            self.write_mcp_config()?;
            self.write_models_config()?;
            self.write_settings_config()?;
            let prompt_text = lock_unpoison(&self.system_prompt).clone();
            if let Err(error) = self.write_system_prompt(&prompt_text) {
                tracing::warn!(target: "maho_agent::omo", %error, "omo will start without Maho's system prompt");
            }
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

    /// Writes the isolated agent dir's `settings.json`.
    ///
    /// omo's file/shell built-ins stay off through `--no-builtin-tools` (process.rs); this
    /// file only decides which packages load. With the omo-ai plugin staged it loads the
    /// plugin's extensions (memory engine, task, workpool); otherwise it is `{}`.
    fn write_settings_config(&self) -> Result<(), AgentError> {
        std::fs::create_dir_all(self.config.agent_dir()).map_err(|error| {
            AgentError::ExecutionError(format!("failed to create omo agent directory: {error}"))
        })?;
        let path = self.config.agent_dir().join("settings.json");
        let bytes = serde_json::to_vec_pretty(&omo_settings_json(self.config.plugin_dir()))
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
        let secure_storage_callback = lock_unpoison(&self.secure_storage_callback).clone();
        let provider_opt = if let Some(provider) = self.config.custom_provider() {
            Some(provider.clone())
        } else if let Some(cb) = secure_storage_callback {
            // Invoked outside the mutex: the callback reaches the platform keychain.
            let preferred = lock_unpoison(&self.preferred_provider).clone();
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
        if let Some(session_id) = lock_unpoison(&connection.sessions).get(maho_session_id).cloned() {
            return Ok(session_id);
        }

        // The id becomes a file name under the session directory.
        if maho_session_id.is_empty()
            || !maho_session_id
                .bytes()
                .all(|b| b.is_ascii_alphanumeric() || b == b'_' || b == b'-')
        {
            return Err(AgentError::ExecutionError(format!(
                "invalid session id {maho_session_id:?}: only [A-Za-z0-9_-] is allowed"
            )));
        }
        let params = self.config.open_session_params();
        let preferred = lock_unpoison(&self.preferred_provider).clone();
        let provider = preferred.or_else(|| params.provider);
        let session_file = self.config.session_dir().join(format!("{maho_session_id}.jsonl"));
        if !session_file.exists() {
            let write_error = |error: std::io::Error| {
                AgentError::ExecutionError(format!(
                    "failed to write omo session file at {}: {error}",
                    session_file.display()
                ))
            };
            if let Some(parent) = session_file.parent() {
                std::fs::create_dir_all(parent).map_err(write_error)?;
            }
            let header = serde_json::json!({
                "type": "session",
                "version": 3,
                "id": maho_session_id,
                "timestamp": chrono::Utc::now().to_rfc3339_opts(chrono::SecondsFormat::Millis, true),
                "cwd": self.cwd.to_string_lossy(),
            });
            std::fs::write(&session_file, format!("{header}\n")).map_err(write_error)?;
        }
        // The RPC socket name is random per host process, so a second process (`maho agent
        // session ...`) finds this session's host through this pointer.
        let pointer = super::session_control::socket_pointer_file(&session_file);
        if let Err(error) = std::fs::write(&pointer, self.config.socket_path().to_string_lossy().as_bytes()) {
            tracing::warn!(target: "maho_agent::omo", %error, path = %pointer.display(), "session control pointer not written; live steering of this session is unavailable");
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
        // omo can announce a freshly opened handle as closed before its open_session reply
        // is read. Such a handle is dead on arrival, so open again rather than hand it out.
        const MAX_OPEN_ATTEMPTS: usize = 3;
        for _ in 0..MAX_OPEN_ATTEMPTS {
            let response = require_success(connection.request(command.clone()).await?)?;
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
            // Check and insert under the ended-handles lock: the reader marks a handle
            // ended before evicting it, so either it sees our entry and removes it, or we
            // see its mark here and reopen.
            let ended = lock_unpoison(&connection.ended_handles);
            if ended.contains(&session_id) {
                continue;
            }
            lock_unpoison(&connection.sessions)
                .insert(maho_session_id.to_string(), session_id.clone());
            return Ok(session_id);
        }
        Err(AgentError::ExecutionError(format!(
            "omo closed every session it opened for {maho_session_id} before replying \
             ({MAX_OPEN_ATTEMPTS} attempts)"
        )))
    }

    /// Sends a command addressed to `maho_session_id`'s omo handle. omo's idle sweep can
    /// retire a cached handle after lookup but before dispatch. omo then refuses the command
    /// in `getForCommand`, before `binding.handle` accepts it, with `unknown_session` (gone)
    /// or `session_closing` (teardown in flight). The command never ran, so forget the
    /// handle, reopen (omo's open waits out an in-flight teardown), and resend. Every other
    /// failure is returned unchanged, and exhaustion is reported as a lifecycle error rather
    /// than omo's raw refusal.
    async fn session_request(
        &self,
        maho_session_id: &str,
        connection: &RpcConnection,
        build: impl Fn(String) -> RpcCommand,
    ) -> Result<(String, RpcResponse), AgentError> {
        const MAX_SEND_ATTEMPTS: usize = 3;
        for _ in 0..MAX_SEND_ATTEMPTS {
            let handle = self.rpc_session(maho_session_id, connection).await?;
            let response = connection.request(build(handle.clone())).await?;
            if !response.success && is_retired_handle_refusal(response.error.as_deref()) {
                forget_handle(connection, &handle);
                continue;
            }
            return Ok((handle, require_success(response)?));
        }
        Err(AgentError::ExecutionError(format!(
            "omo kept retiring the session for {maho_session_id} before it could take the \
             request ({MAX_SEND_ATTEMPTS} attempts); please retry"
        )))
    }

    pub async fn get_auth_providers(&self) -> Result<Vec<RpcAuthProvider>, AgentError> {
        let connection = self.connection().await?;
        let (_, response) = self
            .session_request("auth-session", &connection, RpcCommand::get_auth_providers)
            .await?;
        let data = response.data.unwrap_or(serde_json::Value::Null);
        let providers = data.get("providers").cloned().unwrap_or(data);
        serde_json::from_value(providers)
            .map_err(|error| AgentError::ExecutionError(error.to_string()))
    }

    /// Subscribe before starting a turn to observe runtime session lifecycle events.
    pub async fn subscribe_rpc_events(&self) -> Result<broadcast::Receiver<RpcEvent>, AgentError> {
        Ok(self.connection().await?.events.subscribe())
    }

    pub async fn login_start(&self, provider: &str) -> Result<serde_json::Value, AgentError> {
        let connection = self.connection().await?;
        let (_, response) = self
            .session_request("auth-session", &connection, |handle| {
                RpcCommand::login_start(handle, provider)
            })
            .await?;
        Ok(response.data.unwrap_or(serde_json::Value::Null))
    }

    pub async fn login_cancel(&self, provider: &str) -> Result<serde_json::Value, AgentError> {
        let connection = self.connection().await?;
        let (_, response) = self
            .session_request("auth-session", &connection, |handle| {
                RpcCommand::login_cancel(handle, provider)
            })
            .await?;
        Ok(response.data.unwrap_or(serde_json::Value::Null))
    }

    pub async fn login_api_key(
        &self,
        provider: &str,
        key: &str,
    ) -> Result<serde_json::Value, AgentError> {
        let connection = self.connection().await?;
        let (_, response) = self
            .session_request("auth-session", &connection, |handle| {
                RpcCommand::login_api_key(handle, provider, key)
            })
            .await?;
        Ok(response.data.unwrap_or(serde_json::Value::Null))
    }

    async fn turn_lock(&self, session_id: &str) -> Arc<tokio::sync::Mutex<()>> {
        let mut locks = self.turn_locks.lock().await;
        // Drop locks no turn holds or waits on (only the map references them), so the map
        // tracks in-flight sessions instead of every session ever seen.
        locks.retain(|_, lock| Arc::strong_count(lock) > 1);
        locks
            .entry(session_id.to_string())
            .or_insert_with(|| Arc::new(tokio::sync::Mutex::new(())))
            .clone()
    }
}

/// omo's pre-acceptance refusals for a command addressed to a handle it has retired
/// (session-registry getForCommand): gone, or mid-teardown.
fn is_retired_handle_refusal(error: Option<&str>) -> bool {
    matches!(error, Some("unknown_session" | "session_closing"))
}

/// Drops a handle omo has stopped hosting, in the same order the reader uses (mark ended,
/// then evict) so a concurrent `rpc_session` never re-caches it.
fn forget_handle(connection: &RpcConnection, handle: &str) {
    lock_unpoison(&connection.ended_handles).insert(handle.to_string());
    lock_unpoison(&connection.sessions).retain(|_, cached| cached != handle);
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
            assistant_message_event: AssistantMessageEvent::Error { reason, error },
            ..
        } => Some(
            error
                .as_ref()
                .and_then(failed_message_error)
                .unwrap_or_else(|| match reason.as_deref() {
                    Some("aborted") => "omo model turn was aborted".to_string(),
                    _ => "omo model turn failed".to_string(),
                }),
        ),
        // omo reports a failed provider call (HTTP 4xx/5xx, quota, invalid request) only on
        // the finished assistant message: `stopReason: "error" | "aborted"` plus
        // `errorMessage`. No `error` event accompanies it, so without this arm a failed turn
        // ends as an empty success.
        RpcEvent::MessageEnd {
            message: Some(message),
            ..
        }
        | RpcEvent::TurnEnd {
            message: Some(message),
            ..
        } => failed_message_error(message),
        RpcEvent::SessionAbort { .. } => Some("omo session turn was aborted".to_string()),
        _ => None,
    }
}

fn failed_message_error(message: &serde_json::Value) -> Option<String> {
    let stop_reason = message.get("stopReason").and_then(serde_json::Value::as_str)?;
    if stop_reason != "error" && stop_reason != "aborted" {
        return None;
    }
    let detail = message
        .get("errorMessage")
        .and_then(serde_json::Value::as_str)
        .filter(|text| !text.trim().is_empty());
    Some(match (stop_reason, detail) {
        (_, Some(detail)) => detail.to_string(),
        ("aborted", None) => "omo model turn was aborted".to_string(),
        _ => "omo model turn failed".to_string(),
    })
}

/// Next event from the broadcast stream. Falling behind (`Lagged`) only drops old events
/// for this receiver, so it is logged and reading continues; only a closed channel ends.
async fn recv_event(events: &mut broadcast::Receiver<RpcEvent>) -> Result<RpcEvent, AgentError> {
    loop {
        match events.recv().await {
            Ok(event) => return Ok(event),
            Err(broadcast::error::RecvError::Lagged(skipped)) => {
                tracing::warn!(target: "maho_agent::omo", skipped, "omo event receiver lagged; continuing");
            }
            Err(broadcast::error::RecvError::Closed) => {
                return Err(AgentError::ExecutionError(
                    "omo RPC event stream ended".to_string(),
                ));
            }
        }
    }
}

fn event_is_terminal(event: &RpcEvent) -> bool {
    // `agent_settled` is omo's end of a prompt (its own rpc-client waits for it).
    // `agent_end` is not: it also fires before an automatic retry (`willRetry: true`), and
    // the session stays busy until the retries finish.
    matches!(
        event,
        RpcEvent::AgentSettled { .. } | RpcEvent::SessionAbort { .. }
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
        // Subscribe before prompting so no event of this turn can be missed.
        let mut events = connection.events.subscribe();
        let mut closed = connection.closed.subscribe();
        let (prompt, images) = prompt_parts(message.content);
        let run_id = AgentRunId::new(format!("{session_id}:{}", uuid::Uuid::new_v4()));
        if let Err(error) = lock_unpoison(&self.run_journal).start_run(run_id.clone(), session_id) {
            tracing::warn!(target: "maho_agent::omo", %error, "failed to journal omo run start");
        }
        let mut output = String::new();
        let mut turn_error = None;
        let mut aborted = false;
        let mut rpc_session_id = None;
        let wait = async {
            let (handle, _) = self
                .session_request(session_id, &connection, |handle| RpcCommand::Prompt {
                    id: None,
                    session_id: Some(handle),
                    message: prompt.clone(),
                    images: images.clone(),
                    streaming_behavior: None,
                    thinking_level: None,
                    session_title_prompt: None,
                    expand_prompt_templates: None,
                })
                .await?;
            rpc_session_id = Some(handle.clone());
            loop {
                let event = tokio::select! {
                    biased;
                    received = recv_event(&mut events) => received?,
                    _ = closed.wait_for(|closed| *closed) => {
                        return Err(AgentError::ExecutionError(
                            "omo RPC connection closed during the turn".to_string(),
                        ));
                    }
                };
                if event.session_id() != Some(handle.as_str()) {
                    continue;
                }
                if matches!(event, RpcEvent::SessionClosed { .. } | RpcEvent::SessionParked { .. }) {
                    return Err(AgentError::ExecutionError("omo session closed during the turn".to_string()));
                }
                if let Some(error) = event_error(&event) {
                    turn_error = Some(error);
                }
                if matches!(event, RpcEvent::TurnStart { .. }) {
                    // A (re)started turn supersedes any failure an earlier attempt reported.
                    turn_error = None;
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
                    aborted = matches!(event, RpcEvent::SessionAbort { .. });
                    return Ok::<(), AgentError>(());
                }
            }
        };
        let turn_timeout = self.turn_timeout;
        // Every exit funnels through this one outcome so the run is always journaled and an
        // unsettled omo turn is always aborted and its handle retired.
        let (outcome, settled) = match tokio::time::timeout(turn_timeout, wait).await {
            Ok(Ok(())) => match turn_error {
                Some(error) => (Err(AgentError::ExecutionError(error)), true),
                None => (Ok(()), true),
            },
            Ok(Err(error)) => (Err(error), false),
            Err(_) => (
                Err(AgentError::ExecutionError(format!(
                    "omo turn timed out after {turn_timeout:?}"
                ))),
                false,
            ),
        };

        if outcome.is_err() {
            let handle = rpc_session_id.or_else(|| {
                lock_unpoison(&connection.sessions).get(session_id).cloned()
            });
            if let Some(rpc_session_id) = handle {
                if let Err(error) = connection
                    .request(RpcCommand::abort(&rpc_session_id))
                    .await
                    .and_then(require_success)
                {
                    tracing::warn!(target: "maho_agent::omo", %error, "failed to abort unsettled omo turn");
                }
                if !settled && !connection.is_closed() {
                    let _ = tokio::time::timeout(Duration::from_secs(2), async {
                        while let Ok(ev) = recv_event(&mut events).await {
                            if ev.session_id() == Some(rpc_session_id.as_str()) && event_is_terminal(&ev) {
                                return;
                            }
                        }
                    })
                    .await;
                }
                // The handle may still be mid-turn; later turns must open a fresh session.
                forget_handle(&connection, &rpc_session_id);
            }
        }

        let (kind, payload) = match &outcome {
            Ok(()) => (RunCheckpointKind::Completed, None),
            Err(error) => (
                if aborted { RunCheckpointKind::Cancelled } else { RunCheckpointKind::Failed },
                Some(serde_json::json!({ "error": error.to_string() })),
            ),
        };
        if let Err(error) = lock_unpoison(&self.run_journal).record_terminal(&run_id, kind, payload) {
            tracing::warn!(target: "maho_agent::omo", %error, "failed to journal omo run end");
        }
        drop(_turn_guard);
        let mut locks = self.turn_locks.lock().await;
        if Arc::strong_count(&turn_lock) == 2 {
            locks.remove(session_id);
        }
        drop(locks);
        outcome?;
        Ok(ChatMessage::assistant(ChatContent::text(output)))
    }

    fn set_browser_tool_bridge(
        &self,
        bridge: Arc<dyn crate::BrowserToolBridge>,
    ) -> Result<(), AgentError> {
        *lock_unpoison(&self.browser_tool_bridge) = Some(bridge);
        Ok(())
    }

    async fn cancel(&self, session_id: &str) -> Result<(), AgentError> {
        // Cancelling never spawns omo: without a live connection there is no turn to stop.
        let connection = self
            .connection
            .lock()
            .await
            .clone()
            .filter(|connection| !connection.is_closed())
            .ok_or_else(|| AgentError::SessionNotFound(session_id.to_string()))?;
        let rpc_session_id = lock_unpoison(&connection.sessions)
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

    fn set_approval_policy(&self, policy: String) {
        // Omo RPC has no permission-request event. Chromium's browser gate is
        // the enforcement point; this value retains the session policy.
        *lock_unpoison(&self.approval_policy) = policy;
    }

    fn set_secure_storage_callback(
        &self,
        callback: SecureStorageCallback,
    ) -> Result<(), AgentError> {
        *lock_unpoison(&self.secure_storage_callback) = Some(Arc::new(callback));
        Ok(())
    }

    fn run_journal(&self) -> Arc<Mutex<RunJournal>> {
        Arc::clone(&self.run_journal)
    }

    fn interaction_broker(&self) -> Arc<Mutex<crate::interaction::InteractionBroker>> {
        Arc::clone(&self.interaction_broker)
    }

    fn resolve_interaction(
        &self,
        req_id: &str,
        answer: crate::interaction::InteractionAnswer,
    ) -> Result<(), crate::interaction::InteractionError> {
        lock_unpoison(&self.interaction_broker)
            .resolve(&crate::interaction::InteractionRequestId::new(req_id), answer)
    }

    fn timeout_interaction(&self, req_id: &str) -> Result<(), crate::interaction::InteractionError> {
        lock_unpoison(&self.interaction_broker)
            .timeout(&crate::interaction::InteractionRequestId::new(req_id))
    }

    fn cancel_interaction(&self, req_id: &str) -> Result<(), crate::interaction::InteractionError> {
        lock_unpoison(&self.interaction_broker)
            .cancel(&crate::interaction::InteractionRequestId::new(req_id))
    }

    fn set_preferred_provider(&self, provider: Option<String>) {
        *lock_unpoison(&self.preferred_provider) = provider;
    }

    fn set_runtime_tier(&self, tier: Option<String>) {
        *lock_unpoison(&self.runtime_tier) = tier;
    }

    fn runtime_tier(&self) -> Option<String> {
        lock_unpoison(&self.runtime_tier).clone()
    }

    fn set_active_space_id(&self, space_id: Option<String>) {
        *lock_unpoison(&self.active_space_id) = space_id;
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
        *lock_unpoison(&self.system_prompt) = effective_prompt.to_string();
        // omo reads the prompt file only at spawn; a failed write must not pass silently.
        if let Err(error) = self.write_system_prompt(effective_prompt) {
            tracing::warn!(target: "maho_agent::omo", %error, "failed to persist omo system prompt");
        }
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

    async fn list_artifacts(
        &self,
        session_id: &str,
    ) -> Result<Vec<maho_types::artifact::ArtifactInfo>, AgentError> {
        let storage = lock_unpoison(&self.storage).clone();
        if let Some(storage) = storage {
            return storage.list_artifacts(session_id).await;
        }
        Ok(Vec::new())
    }

    async fn artifact_storage_rel_path(
        &self,
        session_id: &str,
        artifact_id: &str,
    ) -> Result<Option<String>, AgentError> {
        let storage = lock_unpoison(&self.storage).clone();
        if let Some(storage) = storage {
            let list = storage.list_artifacts(session_id).await?;
            return Ok(list
                .into_iter()
                .find(|a| a.artifact_id == artifact_id)
                .map(|a| a.storage_rel_path));
        }
        Ok(None)
    }

    fn artifact_root(&self) -> Option<PathBuf> {
        self.artifact_root()
    }
}

#[cfg(test)]
mod tests {
    use std::path::Path;

    use super::*;

    /// Minimal stand-in for the omo RPC server. Like omo it hands out `rpc-N` session
    /// handles, answers `unknown_session` for a handle it no longer hosts, broadcasts
    /// `session_closed { reason: "idle_evicted" }` to every connection when it evicts a
    /// session, and forgets a connection's sessions once that connection ends.
    struct FakeOmo {
        live: Mutex<std::collections::HashSet<String>>,
        opened: AtomicU64,
        stale_hits: AtomicU64,
        drop_after_turn: std::sync::atomic::AtomicBool,
        /// When set, the next prompt fails the way omo reports a provider failure:
        /// `(stopReason, errorMessage)` on message_end/turn_end, no `error` event.
        fail_next_turn: Mutex<Option<(String, String)>>,
        fail_then_retry: std::sync::atomic::AtomicBool,
        /// When set, the next opened session is evicted and announced BEFORE its
        /// open_session reply is written: the reader sees the close first.
        close_before_open_reply: std::sync::atomic::AtomicBool,
        /// Number of upcoming session commands whose handle omo retires just before
        /// dispatching them: the retirement lands after the backend's handle lookup.
        retire_before_dispatch: std::sync::atomic::AtomicUsize,
        /// Refusal omo gives a retired handle: "unknown_session" (gone) or
        /// "session_closing" (teardown still in flight).
        retire_refusal: Mutex<&'static str>,
        /// When set, the next prompt is accepted but never settles (no events at all).
        hang_next_turn: std::sync::atomic::AtomicBool,
        evictions: broadcast::Sender<String>,
    }

    impl FakeOmo {
        fn new() -> Arc<Self> {
            Arc::new(Self {
                live: Mutex::new(std::collections::HashSet::new()),
                opened: AtomicU64::new(0),
                stale_hits: AtomicU64::new(0),
                drop_after_turn: std::sync::atomic::AtomicBool::new(false),
                fail_next_turn: Mutex::new(None),
                fail_then_retry: std::sync::atomic::AtomicBool::new(false),
                close_before_open_reply: std::sync::atomic::AtomicBool::new(false),
                retire_before_dispatch: std::sync::atomic::AtomicUsize::new(0),
                retire_refusal: Mutex::new("unknown_session"),
                hang_next_turn: std::sync::atomic::AtomicBool::new(false),
                evictions: broadcast::channel(16).0,
            })
        }

        /// omo's idle sweep: every live session is evicted and announced.
        fn idle_evict_all(&self) {
            let evicted: Vec<String> = lock_unpoison(&self.live).drain().collect();
            for handle in evicted {
                let _ = self.evictions.send(handle);
            }
        }
    }

    async fn write_json(writer: &mut OwnedWriteHalf, message: &serde_json::Value) {
        let mut bytes = serde_json::to_vec(message).unwrap();
        bytes.push(b'\n');
        writer.write_all(&bytes).await.unwrap();
    }

    async fn serve_fake_omo(listener: tokio::net::UnixListener, fake: Arc<FakeOmo>) {
        while let Ok((stream, _)) = listener.accept().await {
            let fake = Arc::clone(&fake);
            let mut evictions = fake.evictions.subscribe();
            tokio::spawn(async move {
                let (reader, mut writer) = stream.into_split();
                let mut lines = BufReader::new(reader).lines();
                let mut owned = Vec::new();
                loop {
                    let line = tokio::select! {
                        line = lines.next_line() => match line {
                            Ok(Some(line)) => line,
                            _ => break,
                        },
                        Ok(handle) = evictions.recv() => {
                            write_json(&mut writer, &serde_json::json!({
                                "type": "session_closed", "sessionId": handle,
                                "reason": "idle_evicted"
                            })).await;
                            continue;
                        }
                    };
                    let request: serde_json::Value = serde_json::from_str(&line).unwrap();
                    let id = request["id"].clone();
                    let command = request["type"].as_str().unwrap().to_string();
                    let mut out = Vec::new();
                    let mut drop_now = false;
                    if command == "open_session" {
                        let n = fake.opened.fetch_add(1, Ordering::SeqCst) + 1;
                        let handle = format!("rpc-{n}");
                        if fake.close_before_open_reply.swap(false, Ordering::SeqCst) {
                            write_json(&mut writer, &serde_json::json!({
                                "type": "session_closed", "sessionId": handle,
                                "reason": "idle_evicted"
                            })).await;
                        } else {
                            lock_unpoison(&fake.live).insert(handle.clone());
                        }
                        owned.push(handle.clone());
                        out.push(serde_json::json!({
                            "type": "response", "id": id, "command": command,
                            "success": true, "data": { "sessionId": handle }
                        }));
                    } else {
                        let handle = request["sessionId"].as_str().unwrap_or_default().to_string();
                        let retire = fake
                            .retire_before_dispatch
                            .fetch_update(Ordering::SeqCst, Ordering::SeqCst, |n| n.checked_sub(1))
                            .is_ok();
                        if retire {
                            lock_unpoison(&fake.live).remove(&handle);
                        }
                        if !lock_unpoison(&fake.live).contains(&handle) {
                            fake.stale_hits.fetch_add(1, Ordering::SeqCst);
                            let refusal =
                                if retire { *lock_unpoison(&fake.retire_refusal) } else { "unknown_session" };
                            out.push(serde_json::json!({
                                "type": "response", "id": id, "command": command,
                                "success": false, "error": refusal
                            }));
                        } else {
                            out.push(serde_json::json!({
                                "type": "response", "id": id, "command": command, "success": true
                            }));
                            if command == "prompt" {
                                if fake.hang_next_turn.swap(false, Ordering::SeqCst) {
                                    for message in &out {
                                        write_json(&mut writer, message).await;
                                    }
                                    continue;
                                }
                                let failure = lock_unpoison(&fake.fail_next_turn).take();
                                let retry_first = fake.fail_then_retry.swap(false, Ordering::SeqCst);
                                if retry_first {
                                    // omo's auto-retry: a failed attempt, agent_end{willRetry},
                                    // then a fresh turn that succeeds before agent_settled.
                                    let failed = serde_json::json!({
                                        "role": "assistant", "content": [],
                                        "stopReason": "error", "errorMessage": "503: overloaded"
                                    });
                                    out.push(serde_json::json!({ "type": "turn_start", "sessionId": handle }));
                                    out.push(serde_json::json!({
                                        "type": "message_end", "sessionId": handle, "message": failed
                                    }));
                                    out.push(serde_json::json!({
                                        "type": "agent_end", "sessionId": handle, "willRetry": true
                                    }));
                                    out.push(serde_json::json!({ "type": "turn_start", "sessionId": handle }));
                                }
                                if let Some((stop_reason, error_message)) = failure {
                                    let failed = serde_json::json!({
                                        "role": "assistant", "content": [],
                                        "stopReason": stop_reason, "errorMessage": error_message
                                    });
                                    out.push(serde_json::json!({
                                        "type": "message_end", "sessionId": handle, "message": failed
                                    }));
                                    out.push(serde_json::json!({
                                        "type": "turn_end", "sessionId": handle,
                                        "message": failed, "toolResults": []
                                    }));
                                    out.push(serde_json::json!({ "type": "agent_end", "sessionId": handle }));
                                    out.push(serde_json::json!({ "type": "agent_settled", "sessionId": handle }));
                                    for message in &out {
                                        write_json(&mut writer, message).await;
                                    }
                                    continue;
                                }
                                out.push(serde_json::json!({
                                    "type": "message_update", "sessionId": handle,
                                    "assistantMessageEvent": {
                                        "type": "text_delta", "contentIndex": 0,
                                        "delta": format!("reply from {handle}")
                                    }
                                }));
                                out.push(serde_json::json!({ "type": "agent_end", "sessionId": handle }));
                                out.push(serde_json::json!({ "type": "agent_settled", "sessionId": handle }));
                                drop_now = fake.drop_after_turn.swap(false, Ordering::SeqCst);
                            }
                        }
                    }
                    for message in &out {
                        write_json(&mut writer, message).await;
                    }
                    if drop_now {
                        break;
                    }
                }
                let mut live = lock_unpoison(&fake.live);
                for handle in owned {
                    live.remove(&handle);
                }
            });
        }
    }

    /// Builds a backend whose "runtime" is a shell script that only prints omo's ready
    /// marker, while the test itself serves the RPC socket.
    fn fake_omo_backend(dir: &Path, fake: Arc<FakeOmo>) -> OmoBackend {
        let socket_path = dir.join("rpc.sock");
        let listener = tokio::net::UnixListener::bind(&socket_path).unwrap();
        tokio::spawn(serve_fake_omo(listener, fake));
        let script = dir.join("fake-omo.sh");
        std::fs::write(&script, "echo 'rpc listening on fake' >&2\nexec sleep 300\n").unwrap();
        let config = OmoLaunchConfig::builder(dir.join("agent"))
            .runtime_binary("/bin/sh")
            .rpc_entry(&script)
            .socket_path(&socket_path)
            .build()
            .unwrap();
        OmoBackend::new(config, dir.join("maho-browser-mcp"), dir)
    }

    async fn turn(backend: &OmoBackend, text: &str) -> Result<String, AgentError> {
        let message = ChatMessage::user(ChatContent::text(text));
        let reply = tokio::time::timeout(
            Duration::from_secs(10),
            backend.run_turn("panel-session", message, None, None),
        )
        .await
        .expect("turn must settle instead of hanging")?;
        match reply.content {
            ChatContent::Text(text) => Ok(text),
            other => panic!("unexpected reply content: {other:?}"),
        }
    }

    async fn current_connection(backend: &OmoBackend) -> Arc<RpcConnection> {
        backend.connection.lock().await.clone().unwrap()
    }

    fn cached_handle(connection: &RpcConnection) -> Option<String> {
        lock_unpoison(&connection.sessions).get("panel-session").cloned()
    }

    #[test]
    fn approval_policy_is_stored_by_omo_backend() {
        let dir = tempfile::tempdir().unwrap();
        let config = OmoLaunchConfig::builder(dir.path()).build().unwrap();
        let backend = OmoBackend::new(config, dir.path().join("mcp"), dir.path());
        backend.set_approval_policy("prompt".to_string());
        assert_eq!(*lock_unpoison(&backend.approval_policy), "prompt");
        backend.set_approval_policy("deny".to_string());
        assert_eq!(*lock_unpoison(&backend.approval_policy), "deny");
    }

    #[tokio::test]
    async fn idle_eviction_announcement_drops_the_cached_handle() {
        let dir = tempfile::tempdir().unwrap();
        let fake = FakeOmo::new();
        let backend = fake_omo_backend(dir.path(), Arc::clone(&fake));

        assert_eq!(turn(&backend, "first").await.unwrap(), "reply from rpc-1");
        let connection = current_connection(&backend).await;
        assert_eq!(cached_handle(&connection).as_deref(), Some("rpc-1"));

        // Wait for the announcement itself, subscribed before omo's sweep fires.
        let mut events = connection.events.subscribe();
        fake.idle_evict_all();
        tokio::time::timeout(Duration::from_secs(5), async {
            loop {
                if let Ok(RpcEvent::SessionClosed { session_id, .. }) = events.recv().await {
                    if session_id == "rpc-1" {
                        return;
                    }
                }
            }
        })
        .await
        .expect("session_closed must reach the client");
        assert_eq!(cached_handle(&connection), None);

        // The next turn opens a fresh handle up front: omo never sees the evicted one,
        // so it never has a reason to answer unknown_session.
        assert_eq!(turn(&backend, "second").await.unwrap(), "reply from rpc-2");
        assert_eq!(fake.stale_hits.load(Ordering::SeqCst), 0);
    }

    #[tokio::test]
    async fn a_handle_closed_before_its_open_reply_is_reopened_within_the_turn() {
        let dir = tempfile::tempdir().unwrap();
        let fake = FakeOmo::new();
        let backend = fake_omo_backend(dir.path(), Arc::clone(&fake));

        fake.close_before_open_reply.store(true, Ordering::SeqCst);
        // The dead-on-arrival handle is reopened inside the same turn: no unknown_session.
        assert_eq!(turn(&backend, "first").await.unwrap(), "reply from rpc-2");
        let connection = current_connection(&backend).await;
        assert_eq!(cached_handle(&connection).as_deref(), Some("rpc-2"));
        assert_eq!(fake.stale_hits.load(Ordering::SeqCst), 0);
    }

    #[tokio::test]
    async fn a_handle_evicted_after_lookup_is_reopened_and_the_prompt_resubmitted() {
        let dir = tempfile::tempdir().unwrap();
        let fake = FakeOmo::new();
        let backend = fake_omo_backend(dir.path(), Arc::clone(&fake));
        assert_eq!(turn(&backend, "first").await.unwrap(), "reply from rpc-1");

        // omo evicts rpc-1 between our cache lookup and its prompt dispatch.
        fake.retire_before_dispatch.store(1, Ordering::SeqCst);
        assert_eq!(turn(&backend, "second").await.unwrap(), "reply from rpc-2");
        assert_eq!(fake.stale_hits.load(Ordering::SeqCst), 1);
        let connection = current_connection(&backend).await;
        assert_eq!(cached_handle(&connection).as_deref(), Some("rpc-2"));
    }

    #[tokio::test]
    async fn auth_commands_recover_from_an_eviction_after_lookup() {
        let dir = tempfile::tempdir().unwrap();
        let fake = FakeOmo::new();
        let backend = fake_omo_backend(dir.path(), Arc::clone(&fake));
        backend.login_start("anthropic").await.unwrap();

        fake.retire_before_dispatch.store(1, Ordering::SeqCst);
        backend.login_cancel("anthropic").await.unwrap();
        assert_eq!(fake.stale_hits.load(Ordering::SeqCst), 1);
        assert_eq!(fake.opened.load(Ordering::SeqCst), 2);
    }

    #[tokio::test]
    async fn a_prompt_racing_idle_teardown_is_resubmitted_to_a_fresh_session() {
        let dir = tempfile::tempdir().unwrap();
        let fake = FakeOmo::new();
        let backend = fake_omo_backend(dir.path(), Arc::clone(&fake));
        assert_eq!(turn(&backend, "first").await.unwrap(), "reply from rpc-1");

        // omo has begun tearing rpc-1 down when the prompt lands: session_closing.
        *lock_unpoison(&fake.retire_refusal) = "session_closing";
        fake.retire_before_dispatch.store(1, Ordering::SeqCst);
        assert_eq!(turn(&backend, "second").await.unwrap(), "reply from rpc-2");
    }

    #[tokio::test]
    async fn repeated_retirements_end_in_a_lifecycle_error_not_a_raw_refusal() {
        let dir = tempfile::tempdir().unwrap();
        let fake = FakeOmo::new();
        let backend = fake_omo_backend(dir.path(), Arc::clone(&fake));

        fake.retire_before_dispatch.store(usize::MAX, Ordering::SeqCst);
        let error = turn(&backend, "first").await.unwrap_err().to_string();
        assert!(!error.contains("unknown_session"), "{error}");
        assert!(error.contains("kept retiring the session"), "{error}");
        // Every refused handle was forgotten, including the last one.
        let connection = current_connection(&backend).await;
        assert_eq!(cached_handle(&connection), None);

        fake.retire_before_dispatch.store(0, Ordering::SeqCst);
        assert!(turn(&backend, "second").await.is_ok());
    }

    #[tokio::test]
    async fn run_turn_reconnects_after_the_rpc_connection_drops() {
        let dir = tempfile::tempdir().unwrap();
        let fake = FakeOmo::new();
        let backend = fake_omo_backend(dir.path(), Arc::clone(&fake));

        fake.drop_after_turn.store(true, Ordering::SeqCst);
        assert_eq!(turn(&backend, "first").await.unwrap(), "reply from rpc-1");
        let first = current_connection(&backend).await;
        tokio::time::timeout(Duration::from_secs(5), first.closed.subscribe().wait_for(|c| *c))
            .await
            .expect("client must observe the dropped connection")
            .unwrap();

        assert_eq!(turn(&backend, "second").await.unwrap(), "reply from rpc-2");
        let second = current_connection(&backend).await;
        assert!(!Arc::ptr_eq(&first, &second), "a closed connection must not be reused");
        assert_eq!(fake.stale_hits.load(Ordering::SeqCst), 0);
    }

    #[tokio::test]
    async fn provider_failure_on_the_finished_message_fails_the_turn_with_its_text() {
        let dir = tempfile::tempdir().unwrap();
        let fake = FakeOmo::new();
        let backend = fake_omo_backend(dir.path(), Arc::clone(&fake));
        let quota = r#"429: {"code":429,"message":"Individual quota reached."}"#;

        *lock_unpoison(&fake.fail_next_turn) = Some(("error".to_string(), quota.to_string()));
        let error = turn(&backend, "first").await.unwrap_err().to_string();
        assert!(error.contains(quota), "provider text must reach the caller: {error}");

        *lock_unpoison(&fake.fail_next_turn) = Some(("aborted".to_string(), String::new()));
        let error = turn(&backend, "second").await.unwrap_err().to_string();
        assert!(error.contains("aborted"), "{error}");

        assert_eq!(turn(&backend, "third").await.unwrap(), "reply from rpc-3");
    }

    #[tokio::test]
    async fn a_turn_omo_retries_waits_for_settlement_and_reports_the_final_attempt() {
        let dir = tempfile::tempdir().unwrap();
        let fake = FakeOmo::new();
        let backend = fake_omo_backend(dir.path(), Arc::clone(&fake));

        fake.fail_then_retry.store(true, Ordering::SeqCst);
        assert_eq!(turn(&backend, "first").await.unwrap(), "reply from rpc-1");

        fake.fail_then_retry.store(true, Ordering::SeqCst);
        *lock_unpoison(&fake.fail_next_turn) =
            Some(("error".to_string(), "429: quota after retries".to_string()));
        let error = turn(&backend, "second").await.unwrap_err().to_string();
        assert!(error.contains("429: quota after retries"), "{error}");
    }

    #[tokio::test]
    async fn a_timed_out_turn_is_journaled_aborted_and_its_handle_retired() {
        let dir = tempfile::tempdir().unwrap();
        let fake = FakeOmo::new();
        let mut backend = fake_omo_backend(dir.path(), Arc::clone(&fake));
        backend.turn_timeout = Duration::from_millis(300);

        fake.hang_next_turn.store(true, Ordering::SeqCst);
        let error = turn(&backend, "first").await.unwrap_err().to_string();
        assert!(error.contains("timed out"), "{error}");

        let journal = backend.run_journal();
        let journal = lock_unpoison(&journal);
        let runs = journal.list_runs();
        assert_eq!(runs.len(), 1);
        assert_eq!(runs[0].terminal_kind, Some(RunCheckpointKind::Failed));
        drop(journal);

        let connection = current_connection(&backend).await;
        assert_eq!(cached_handle(&connection), None);
        assert!(lock_unpoison(&connection.ended_handles).contains("rpc-1"));
        assert_eq!(turn(&backend, "second").await.unwrap(), "reply from rpc-2");
    }

    #[tokio::test]
    async fn a_lagged_event_receiver_keeps_reading() {
        let (sender, mut receiver) = broadcast::channel(1);
        sender.send(RpcEvent::Other).unwrap();
        sender
            .send(RpcEvent::AgentSettled { session_id: Some("rpc-1".to_string()) })
            .unwrap();
        let event = recv_event(&mut receiver).await.expect("lag is not fatal");
        assert!(event_is_terminal(&event));
        drop(sender);
        assert!(recv_event(&mut receiver).await.is_err());
    }

    #[tokio::test]
    async fn bad_frames_are_skipped_without_ending_the_stream() {
        let mut input: Vec<u8> = Vec::new();
        input.extend_from_slice(&[b'x'; 40]);
        input.push(b'\n');
        input.extend_from_slice(&[0xff, 0xfe, b'\n']);
        input.extend_from_slice(b"{\"ok\":1}\r\n");
        let mut reader = BufReader::with_capacity(8, &input[..]);

        assert!(matches!(read_frame(&mut reader, 16).await.unwrap(), Some(Frame::Skipped(_))));
        assert!(matches!(read_frame(&mut reader, 16).await.unwrap(), Some(Frame::Skipped(_))));
        match read_frame(&mut reader, 16).await.unwrap() {
            Some(Frame::Line(line)) => assert_eq!(line, "{\"ok\":1}"),
            _ => panic!("a valid frame after bad ones must still be read"),
        }
        assert!(read_frame(&mut reader, 16).await.unwrap().is_none());
    }

    #[tokio::test]
    async fn an_interrupted_write_retires_the_connection() {
        let (ours, _peer) = UnixStream::pair().unwrap();
        ours.writable().await.unwrap();
        let bytes = [b'x'; 4096];
        loop {
            match ours.try_write(&bytes) {
                Ok(_) => {}
                Err(error) if error.kind() == std::io::ErrorKind::WouldBlock => break,
                Err(error) => panic!("failed to fill socket: {error}"),
            }
        }
        let connection = RpcConnection::new(ours);
        {
            let mut request = Box::pin(connection.request(RpcCommand::prompt("rpc-1", "hi")));
            assert!(matches!(futures::poll!(&mut request), std::task::Poll::Pending));
        }
        assert!(
            connection.is_closed(),
            "a partial frame is on the socket; the connection must not be reused"
        );
    }

    #[tokio::test]
    async fn session_ids_that_are_not_safe_file_names_are_rejected() {
        let dir = tempfile::tempdir().unwrap();
        let backend = fake_omo_backend(dir.path(), FakeOmo::new());
        for bad in ["../escape", "a/b", "", "x.y"] {
            let error = backend
                .run_turn(bad, ChatMessage::user(ChatContent::text("hi")), None, None)
                .await
                .unwrap_err()
                .to_string();
            assert!(error.contains("invalid session id"), "{bad}: {error}");
        }
        assert!(!dir.path().join("agent").join("escape.jsonl").exists());
    }

    #[tokio::test]
    async fn cancel_without_a_connection_does_not_spawn_omo() {
        let dir = tempfile::tempdir().unwrap();
        let config = OmoLaunchConfig::builder(dir.path().join("agent"))
            .runtime_binary(dir.path().join("no-such-runtime"))
            .rpc_entry(dir.path().join("entry.js"))
            .socket_path(dir.path().join("rpc.sock"))
            .build()
            .unwrap();
        let backend = OmoBackend::new(config, dir.path().join("mcp"), dir.path());
        assert!(matches!(
            backend.cancel("panel-session").await,
            Err(AgentError::SessionNotFound(_))
        ));
        assert!(backend.process.lock().await.is_none());
    }

    #[tokio::test]
    async fn idle_turn_locks_are_pruned() {
        let dir = tempfile::tempdir().unwrap();
        let backend = fake_omo_backend(dir.path(), FakeOmo::new());
        assert!(turn(&backend, "first").await.is_ok());
        assert!(backend.turn_locks.lock().await.is_empty());
        let held = backend.turn_lock("other-session").await;
        let locks = backend.turn_locks.lock().await;
        assert_eq!(locks.len(), 1, "the finished session's lock must be dropped");
        assert!(locks.contains_key("other-session"));
        drop(locks);
        drop(held);
    }

    #[test]
    fn secure_storage_callback_runs_without_holding_its_mutex() {
        let dir = tempfile::tempdir().unwrap();
        let config = OmoLaunchConfig::builder(dir.path()).build().unwrap();
        let backend = Arc::new(OmoBackend::new(config, dir.path().join("mcp"), dir.path()));
        let weak = Arc::downgrade(&backend);
        backend.set_secure_storage_callback(Box::new(move |_| {
            let backend = weak.upgrade().unwrap();
            assert!(backend.secure_storage_callback.try_lock().is_ok());
            Err(AgentError::ExecutionError("fixture has no credentials".to_string()))
        })).unwrap();
        backend.write_models_config().unwrap();
    }

    #[tokio::test]
    async fn a_failed_settled_turn_retires_its_handle() {
        let dir = tempfile::tempdir().unwrap();
        let fake = FakeOmo::new();
        let backend = fake_omo_backend(dir.path(), Arc::clone(&fake));
        *lock_unpoison(&fake.fail_next_turn) = Some(("error".to_string(), "fixture failure".to_string()));
        assert!(turn(&backend, "first").await.is_err());
        let connection = current_connection(&backend).await;
        assert!(cached_handle(&connection).is_none());
        assert!(lock_unpoison(&connection.ended_handles).contains("rpc-1"));
        assert_eq!(lock_unpoison(&backend.run_journal).list_runs()[0].terminal_kind, Some(RunCheckpointKind::Failed));
    }

    #[tokio::test]
    async fn session_file_write_failures_reach_the_caller() {
        let dir = tempfile::tempdir().unwrap();
        let backend = fake_omo_backend(dir.path(), FakeOmo::new());
        std::fs::create_dir_all(backend.config.agent_dir()).unwrap();
        std::fs::write(backend.config.session_dir(), b"not a directory").unwrap();
        let error = turn(&backend, "first").await.unwrap_err().to_string();
        assert!(error.contains("failed to write omo session file"), "{error}");
    }

    #[test]
    fn a_poisoned_setting_mutex_does_not_panic_setters() {
        let dir = tempfile::tempdir().unwrap();
        let config = OmoLaunchConfig::builder(dir.path()).build().unwrap();
        let backend = Arc::new(OmoBackend::new(config, dir.path().join("mcp"), dir.path()));
        let poisoner = Arc::clone(&backend);
        let _ = std::thread::spawn(move || {
            let _guard = poisoner.preferred_provider.lock().unwrap();
            panic!("poison the mutex");
        })
        .join();
        assert!(backend.preferred_provider.is_poisoned());
        backend.set_preferred_provider(Some("anthropic".to_string()));
        assert_eq!(
            lock_unpoison(&backend.preferred_provider).as_deref(),
            Some("anthropic")
        );
    }

    #[test]
    fn only_failed_stop_reasons_count_as_turn_errors() {
        for (stop_reason, expected) in [
            ("stop", None),
            ("toolUse", None),
            ("length", None),
            ("error", Some("boom")),
            ("aborted", Some("boom")),
        ] {
            let message = serde_json::json!({ "stopReason": stop_reason, "errorMessage": "boom" });
            assert_eq!(failed_message_error(&message).as_deref(), expected, "{stop_reason}");
        }
        assert_eq!(
            failed_message_error(&serde_json::json!({ "stopReason": "error" })).as_deref(),
            Some("omo model turn failed")
        );
        assert_eq!(failed_message_error(&serde_json::json!({})), None);

        let update: RpcEvent = serde_json::from_value(serde_json::json!({
            "type": "message_update", "sessionId": "rpc-1",
            "assistantMessageEvent": {
                "type": "error", "reason": "error",
                "error": { "stopReason": "error", "errorMessage": "400: bad schema" }
            }
        }))
        .unwrap();
        assert_eq!(event_error(&update).as_deref(), Some("400: bad schema"));
    }

    #[test]
    fn interactions_resolve_through_the_backends_own_broker() {
        use crate::interaction::{InteractionAnswer, InteractionError, InteractionOption};
        let dir = tempfile::tempdir().unwrap();
        let config = OmoLaunchConfig::builder(dir.path()).build().unwrap();
        let backend = OmoBackend::new(config.clone(), dir.path().join("mcp"), dir.path());
        let other = OmoBackend::new(config, dir.path().join("mcp"), dir.path());

        let asked = lock_unpoison(&backend.interaction_broker())
            .ask_user_question("Pick one", vec![InteractionOption::new("a", "A")])
            .unwrap();

        // Another session's backend cannot see, let alone answer, this question.
        assert!(matches!(
            other.resolve_interaction(
                asked.as_str(),
                InteractionAnswer::SelectedOption("a".to_string())
            ),
            Err(InteractionError::NotFound(_))
        ));
        backend
            .resolve_interaction(
                asked.as_str(),
                InteractionAnswer::SelectedOption("a".to_string()),
            )
            .unwrap();
        assert!(lock_unpoison(&backend.interaction_broker())
            .get(&asked)
            .unwrap()
            .state
            .is_resolved());
        assert!(matches!(
            backend.cancel_interaction("missing"),
            Err(InteractionError::NotFound(_))
        ));
    }

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
                { "name": "browser_visual_click" },
                { "name": "browser_native_type" },
                { "name": "browser_native_key" },
                { "name": "browser_download" },
                { "name": "browser_import_list" },
                { "name": "browser_import_run" },
                { "name": "browser_task_space" },
                { "name": "browser_dialog_respond" },
                { "name": "browser_insert_text" }
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
            "browser_task_space",
            "browser_dialog_respond",
            "browser_insert_text",
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
            "browser_native_type",
            "browser_native_key",
            "browser_download",
            "browser_import_list",
            "browser_import_run",
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
    fn settings_json_loads_plugin_extensions_only_when_present() {
        assert_eq!(omo_settings_json(None), serde_json::json!({}));
        let with = omo_settings_json(Some(Path::new("/opt/maho/omo-plugin")));
        let pkg = &with["packages"][0];
        assert_eq!(pkg["source"], "/opt/maho/omo-plugin");
        for key in ["skills", "prompts", "themes"] {
            assert_eq!(pkg[key], serde_json::json!([]), "{key} must be filtered out");
        }
    }

    #[test]
    fn agent_tools_server_registered_only_when_binary_is_staged() {
        let temp = tempfile::tempdir().unwrap();
        let mcp = temp.path().join("maho-browser-mcp");
        std::fs::write(&mcp, b"").unwrap();
        let mut config = browser_mcp_config(&mcp, Some(Path::new("/tmp/maho.sock")));
        add_agent_tools_server(&mut config, &mcp, Path::new("/tmp/artifacts"));
        assert!(config["mcpServers"].get(AGENT_TOOLS_BINARY).is_none());

        let tools = temp.path().join(AGENT_TOOLS_BINARY);
        std::fs::write(&tools, b"").unwrap();
        add_agent_tools_server(&mut config, &mcp, Path::new("/tmp/artifacts"));
        let server = &config["mcpServers"][AGENT_TOOLS_BINARY];
        assert_eq!(server["type"], "stdio");
        assert_eq!(server["command"], tools.to_string_lossy().as_ref());
        assert_eq!(server["env"]["MAHO_AGENT_TOOLS_ARTIFACT_DIR"], "/tmp/artifacts");
        assert_eq!(
            server["env"]["MAHO_AGENT_AUTOMATIONS_FILE"],
            "/tmp/automations.json"
        );
        assert_eq!(server["env"]["MAHO_AGENT_MODELS_FILE"], "/tmp/models.json");
        assert!(server["env"]["MAHO_AGENT_SEARCH_BASE_URL"].as_str().unwrap().starts_with("http"));
        assert_eq!(server["env"]["MAHO_MCP_SOCKET_PATH"], "/tmp/maho.sock");
        assert_eq!(server["env"]["MAHO_MCP_PUBLIC_ENABLED"], "true");
        assert_eq!(server["startupTimeoutMs"], MAHO_MCP_STARTUP_TIMEOUT_MS);
        assert_eq!(
            config["mcpServers"]["maho-browser"]["startupTimeoutMs"],
            MAHO_MCP_STARTUP_TIMEOUT_MS
        );
        assert_eq!(config["mcpServers"]["maho-browser"]["type"], "stdio");
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
            config["mcpServers"]["maho-browser"]["env"]["MAHO_MCP_APPROVAL"],
            "browser"
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
