use std::io::{BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, ChildStdout, Command, Stdio};
use std::sync::{Arc, Mutex, PoisonError};
use std::time::{Duration, Instant};

use rquickjs::{Context, Function, Runtime};
use serde_json::{json, Value};

pub const SERVER_NAME: &str = "maho-agent-tools";
pub const ARTIFACT_DIR_ENV: &str = "MAHO_AGENT_TOOLS_ARTIFACT_DIR";
pub const BROWSER_MCP_BINARY_ENV: &str = "MAHO_BROWSER_MCP_BINARY";
pub const AUTOMATION_STORE_ENV: &str = "MAHO_AGENT_AUTOMATIONS_FILE";
pub const TASK_ID_ENV: &str = "MAHO_AGENT_TASK_ID";
const PROTOCOL_VERSION: &str = "2025-03-26";
const SCRIPT_TIME_LIMIT: Duration = Duration::from_secs(120);
const SCRIPT_MEMORY_LIMIT: usize = 64 * 1024 * 1024;
const SCRIPT_SOURCE_LIMIT: usize = 256 * 1024;
const DEFAULT_BROWSER_CALL_BUDGET: u64 = 50;
const MAX_BROWSER_CALL_BUDGET: u64 = 500;
const MAX_RESULT_BYTES: usize = 1024 * 1024;

#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum ToolError {
    #[error("invalid_arguments: {0}")]
    InvalidArguments(String),
    #[error("unknown_tool: {0}")]
    UnknownTool(String),
    #[error("js_error: {0}")]
    Js(String),
    #[error("timeout")]
    Timeout,
    #[error("browser_call_budget_exceeded: {0} calls allowed")]
    BrowserBudgetExceeded(u64),
    #[error("browser_unavailable: {0}")]
    BrowserUnavailable(String),
    #[error("browser_call_failed: {0}")]
    BrowserCallFailed(String),
    #[error("artifact_failed: {0}")]
    Artifact(String),
    #[error("search_unavailable: {0}")]
    SearchUnavailable(String),
    #[error("automation_failed: {0}")]
    Automation(String),
}

impl ToolError {
    pub fn code(&self) -> &'static str {
        match self {
            ToolError::InvalidArguments(_) => "invalid_arguments",
            ToolError::UnknownTool(_) => "unknown_tool",
            ToolError::Js(_) => "js_error",
            ToolError::Timeout => "timeout",
            ToolError::BrowserBudgetExceeded(_) => "browser_call_budget_exceeded",
            ToolError::BrowserUnavailable(_) => "browser_unavailable",
            ToolError::BrowserCallFailed(_) => "browser_call_failed",
            ToolError::Artifact(_) => "artifact_failed",
            ToolError::SearchUnavailable(_) => "search_unavailable",
            ToolError::Automation(_) => "automation_failed",
        }
    }
}

pub trait BrowserPeer: Send {
    fn call(&mut self, tool: &str, arguments: Value) -> Result<Value, ToolError>;
}

pub trait SearchBackend: Send + Sync {
    fn search(&self, kind: SearchKind, arguments: &Value) -> Result<Value, ToolError>;
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SearchKind {
    Web,
    Images,
}

pub struct NoSearchBackend;

impl SearchBackend for NoSearchBackend {
    fn search(&self, _kind: SearchKind, _arguments: &Value) -> Result<Value, ToolError> {
        Err(ToolError::SearchUnavailable(
            "search_not_entitled: sign in to Maho AI (Pro, Max, or pay-as-you-go) to use search".to_string(),
        ))
    }
}

pub const SEARCH_BASE_URL_ENV: &str = "MAHO_AGENT_SEARCH_BASE_URL";
pub const MODELS_FILE_ENV: &str = "MAHO_AGENT_MODELS_FILE";

/// Web/image search through Maho's AI proxy (`POST {base}/search`). The search
/// provider key lives on the proxy; the client only sends the user's Maho token.
pub struct HttpSearchBackend {
    endpoint: String,
    token: String,
    client: reqwest::Client,
}

impl HttpSearchBackend {
    pub fn new(base_url: &str, token: impl Into<String>) -> Self {
        Self {
            endpoint: format!("{}/search", base_url.trim_end_matches('/')),
            token: token.into(),
            client: reqwest::Client::new(),
        }
    }

    /// Picks the Maho token from the agent's `models.json` provider whose
    /// `baseUrl` is the proxy, so the secret is never copied into another file.
    pub fn from_models_file(base_url: &str, models_file: &Path) -> Option<Self> {
        let models: Value = serde_json::from_slice(&std::fs::read(models_file).ok()?).ok()?;
        let wanted = base_url.trim_end_matches('/');
        models.get("providers")?.as_object()?.values().find_map(|provider| {
            let url = provider.get("baseUrl")?.as_str()?.trim_end_matches('/');
            let key = provider.get("apiKey")?.as_str()?;
            (url == wanted && !key.is_empty()).then(|| Self::new(base_url, key))
        })
    }

    async fn post(&self, body: Value) -> Result<Value, ToolError> {
        let response = self
            .client
            .post(&self.endpoint)
            .bearer_auth(&self.token)
            .json(&body)
            .send()
            .await
            .map_err(|error| ToolError::SearchUnavailable(format!("search request failed: {error}")))?;
        let status = response.status();
        let payload: Value = response.json().await.unwrap_or(Value::Null);
        if status.is_success() {
            return Ok(payload);
        }
        let code = payload["error"]["code"].as_str().unwrap_or("search_unavailable");
        let message = payload["error"]["message"].as_str().unwrap_or("search failed");
        match status.as_u16() {
            400 => Err(ToolError::InvalidArguments(message.to_string())),
            _ => Err(ToolError::SearchUnavailable(format!("{code} (HTTP {}): {message}", status.as_u16()))),
        }
    }
}

impl SearchBackend for HttpSearchBackend {
    fn search(&self, kind: SearchKind, arguments: &Value) -> Result<Value, ToolError> {
        let mut body = json!({
            "kind": match kind { SearchKind::Web => "web", SearchKind::Images => "images" },
            "query": string_arg(arguments, "query")?,
        });
        if let Some(max) = arguments.get("max_results") {
            body["max_results"] = max.clone();
        }
        let runtime = tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
            .map_err(|error| ToolError::SearchUnavailable(error.to_string()))?;
        runtime.block_on(self.post(body))
    }
}

pub type PeerFactory = Box<dyn Fn() -> Result<Box<dyn BrowserPeer>, ToolError> + Send + Sync>;

pub struct AgentToolsServer {
    artifact_dir: PathBuf,
    peer_factory: Arc<PeerFactory>,
    search: Box<dyn SearchBackend>,
    automations: Option<maho_core::automations::AutomationStore>,
    task_id: String,
}

impl AgentToolsServer {
    pub fn new(artifact_dir: PathBuf, peer_factory: PeerFactory, search: Box<dyn SearchBackend>) -> Self {
        Self {
            artifact_dir,
            peer_factory: Arc::new(peer_factory),
            search,
            automations: None,
            task_id: "default".to_string(),
        }
    }

    pub fn with_automations(
        mut self,
        store: maho_core::automations::AutomationStore,
        task_id: impl Into<String>,
    ) -> Self {
        self.automations = Some(store);
        self.task_id = task_id.into();
        self
    }

    pub fn from_env() -> Self {
        let artifact_dir = std::env::var_os(ARTIFACT_DIR_ENV)
            .map(PathBuf::from)
            .unwrap_or_else(|| std::env::temp_dir().join("maho-agent-artifacts"));
        let browser_binary = std::env::var_os(BROWSER_MCP_BINARY_ENV).map(PathBuf::from);
        let peer_factory: PeerFactory = Box::new(move || match &browser_binary {
            Some(binary) => StdioBrowserPeer::spawn(binary).map(|peer| Box::new(peer) as Box<dyn BrowserPeer>),
            None => Err(ToolError::BrowserUnavailable(format!(
                "{BROWSER_MCP_BINARY_ENV} is not set"
            ))),
        });
        let search: Box<dyn SearchBackend> = match (
            std::env::var(SEARCH_BASE_URL_ENV),
            std::env::var_os(MODELS_FILE_ENV),
        ) {
            (Ok(base_url), Some(models_file)) => {
                match HttpSearchBackend::from_models_file(&base_url, Path::new(&models_file)) {
                    Some(backend) => Box::new(backend),
                    None => Box::new(NoSearchBackend),
                }
            }
            _ => Box::new(NoSearchBackend),
        };
        let server = Self::new(artifact_dir, peer_factory, search);
        match std::env::var_os(AUTOMATION_STORE_ENV) {
            Some(path) => server.with_automations(
                maho_core::automations::AutomationStore::new(PathBuf::from(path)),
                std::env::var(TASK_ID_ENV).unwrap_or_else(|_| "default".to_string()),
            ),
            None => server,
        }
    }

    pub fn with_search(mut self, search: Box<dyn SearchBackend>) -> Self {
        self.search = search;
        self
    }

    pub fn tool_descriptors() -> Value {
        json!([
            {
                "name": "js_eval",
                "description": "Evaluate a JavaScript expression in an isolated QuickJS sandbox (no network, no filesystem, 2 s limit). The value of the last expression is returned as JSON; `input` is available as the global `input`.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "code": {"type": "string"},
                        "input": {}
                    },
                    "required": ["code"],
                    "additionalProperties": false
                }
            },
            {
                "name": "artifact_create",
                "description": "Create a document artifact (xlsx, docx, pdf or pptx) from structured JSON and save it to the agent's artifact folder. `filename` is a single file name with the matching extension; existing files are never overwritten.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "kind": {"type": "string", "enum": ["xlsx", "docx", "pdf", "pptx"]},
                        "filename": {"type": "string"},
                        "content": {"type": "object"}
                    },
                    "required": ["kind", "filename", "content"],
                    "additionalProperties": false
                }
            },
            {
                "name": "browser_script",
                "description": "Run a JavaScript program that drives the Maho browser in one call. Inside the script, `browser.call(tool, args)` invokes a Maho browser tool (e.g. browser_navigate, browser_snapshot) and returns its result; `input` holds the caller's input. The script's last expression is returned. Browser calls are capped by `max_browser_calls` (default 50).",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "script": {"type": "string"},
                        "input": {},
                        "max_browser_calls": {"type": "integer", "minimum": 1, "maximum": MAX_BROWSER_CALL_BUDGET}
                    },
                    "required": ["script"],
                    "additionalProperties": false
                }
            },
            {
                "name": "automation_create",
                "description": "Schedule this task's prompt to re-run on a fixed interval (1 minute to 7 days; at most 10 per task). Runs fire from the Maho browser while it is open.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "name": {"type": "string"},
                        "prompt": {"type": "string"},
                        "interval_minutes": {"type": "integer", "minimum": 1, "maximum": 10080}
                    },
                    "required": ["name", "prompt", "interval_minutes"],
                    "additionalProperties": false
                }
            },
            {
                "name": "automation_list",
                "description": "List this task's scheduled automations.",
                "inputSchema": {"type": "object", "properties": {}, "additionalProperties": false}
            },
            {
                "name": "automation_delete",
                "description": "Delete one of this task's automations by id.",
                "inputSchema": {
                    "type": "object",
                    "properties": {"id": {"type": "string"}},
                    "required": ["id"],
                    "additionalProperties": false
                }
            },
            {
                "name": "web_search",
                "description": "Search the web through Maho's search service. Returns titles, URLs and snippets.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "query": {"type": "string"},
                        "max_results": {"type": "integer", "minimum": 1, "maximum": 20}
                    },
                    "required": ["query"],
                    "additionalProperties": false
                }
            },
            {
                "name": "image_search",
                "description": "Search for images through Maho's search service. Returns image URLs with their source pages.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "query": {"type": "string"},
                        "max_results": {"type": "integer", "minimum": 1, "maximum": 20}
                    },
                    "required": ["query"],
                    "additionalProperties": false
                }
            }
        ])
    }

    pub fn call_tool(&self, name: &str, arguments: &Value) -> Result<Value, ToolError> {
        match name {
            "js_eval" => crate::js_sandbox::execute_tool(arguments).map_err(|error| match error {
                crate::js_sandbox::JsSandboxError::Timeout => ToolError::Timeout,
                crate::js_sandbox::JsSandboxError::InvalidInput => {
                    ToolError::InvalidArguments("expected {code, input?}".to_string())
                }
                other => ToolError::Js(other.to_string()),
            }),
            "artifact_create" => self.artifact_create(arguments),
            "browser_script" => self.browser_script(arguments),
            "automation_create" => self.automation_create(arguments),
            "automation_list" => self.automation_list(),
            "automation_delete" => self.automation_delete(arguments),
            "web_search" => self.search.search(SearchKind::Web, arguments),
            "image_search" => self.search.search(SearchKind::Images, arguments),
            other => Err(ToolError::UnknownTool(other.to_string())),
        }
    }

    fn automation_store(&self) -> Result<&maho_core::automations::AutomationStore, ToolError> {
        self.automations.as_ref().ok_or_else(|| {
            ToolError::Automation(format!("{AUTOMATION_STORE_ENV} is not set; automations are unavailable"))
        })
    }

    fn automation_create(&self, arguments: &Value) -> Result<Value, ToolError> {
        let minutes = arguments
            .get("interval_minutes")
            .and_then(Value::as_i64)
            .ok_or_else(|| ToolError::InvalidArguments("`interval_minutes` must be an integer".to_string()))?;
        let automation = self
            .automation_store()?
            .create(
                maho_core::automations::NewAutomation {
                    task_id: &self.task_id,
                    name: string_arg(arguments, "name")?,
                    prompt: string_arg(arguments, "prompt")?,
                    interval_secs: minutes.saturating_mul(60),
                },
                chrono::Utc::now().timestamp(),
            )
            .map_err(|error| ToolError::Automation(error.to_string()))?;
        serde_json::to_value(automation).map_err(|error| ToolError::Automation(error.to_string()))
    }

    fn automation_list(&self) -> Result<Value, ToolError> {
        let automations: Vec<_> = self
            .automation_store()?
            .list()
            .map_err(|error| ToolError::Automation(error.to_string()))?
            .into_iter()
            .filter(|automation| automation.task_id == self.task_id)
            .collect();
        Ok(json!({ "automations": automations }))
    }

    fn automation_delete(&self, arguments: &Value) -> Result<Value, ToolError> {
        let id = string_arg(arguments, "id")?;
        let store = self.automation_store()?;
        let owned = store
            .list()
            .map_err(|error| ToolError::Automation(error.to_string()))?
            .iter()
            .any(|automation| automation.id == id && automation.task_id == self.task_id);
        if !owned {
            return Err(ToolError::Automation(format!("automation {id} not found for this task")));
        }
        store.delete(id).map_err(|error| ToolError::Automation(error.to_string()))?;
        Ok(json!({ "deleted": id }))
    }

    fn artifact_create(&self, arguments: &Value) -> Result<Value, ToolError> {
        let kind = string_arg(arguments, "kind")?;
        let filename = string_arg(arguments, "filename")?;
        let content = arguments
            .get("content")
            .filter(|value| value.is_object())
            .ok_or_else(|| ToolError::InvalidArguments("`content` must be an object".to_string()))?;
        let bytes = serde_json::to_vec(content).map_err(|error| ToolError::Artifact(error.to_string()))?;
        let handle = crate::artifacts::create(&self.artifact_dir, filename, kind, &bytes)
            .map_err(|error| ToolError::Artifact(format!("{error:#}")))?;
        Ok(json!({
            "path": self.artifact_dir.join(&handle.storage_rel_path).to_string_lossy(),
            "filename": handle.display_name,
            "mimeType": handle.mime_type,
            "sizeBytes": handle.size_bytes,
        }))
    }

    fn browser_script(&self, arguments: &Value) -> Result<Value, ToolError> {
        let script = string_arg(arguments, "script")?;
        if script.len() > SCRIPT_SOURCE_LIMIT {
            return Err(ToolError::InvalidArguments("script exceeds 256 KiB".to_string()));
        }
        let budget = match arguments.get("max_browser_calls") {
            None => DEFAULT_BROWSER_CALL_BUDGET,
            Some(value) => value
                .as_u64()
                .filter(|n| (1..=MAX_BROWSER_CALL_BUDGET).contains(n))
                .ok_or_else(|| {
                    ToolError::InvalidArguments(format!(
                        "max_browser_calls must be 1..={MAX_BROWSER_CALL_BUDGET}"
                    ))
                })?,
        };
        let input = arguments.get("input").cloned().unwrap_or(Value::Null);
        run_browser_script(script, &input, budget, &self.peer_factory)
    }

    /// Runs `browser_script` arguments directly (used by `maho browser script`).
    pub fn run_script(&self, arguments: &Value) -> Result<Value, ToolError> {
        self.browser_script(arguments)
    }

    pub fn handle_message(&self, message: &Value) -> Option<Value> {
        let id = message.get("id").cloned();
        let method = message.get("method").and_then(Value::as_str).unwrap_or("");
        let id = id?;
        let result = match method {
            "initialize" => Ok(json!({
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {"tools": {"listChanged": false}},
                "serverInfo": {"name": SERVER_NAME, "version": env!("CARGO_PKG_VERSION")}
            })),
            "ping" => Ok(json!({})),
            "tools/list" => Ok(json!({"tools": Self::tool_descriptors()})),
            "tools/call" => {
                let params = message.get("params").cloned().unwrap_or(Value::Null);
                let name = params.get("name").and_then(Value::as_str).unwrap_or("");
                let arguments = params.get("arguments").cloned().unwrap_or_else(|| json!({}));
                Ok(tool_result(self.call_tool(name, &arguments)))
            }
            other => Err(json!({"code": -32601, "message": format!("method not found: {other}")})),
        };
        Some(match result {
            Ok(result) => json!({"jsonrpc": "2.0", "id": id, "result": result}),
            Err(error) => json!({"jsonrpc": "2.0", "id": id, "error": error}),
        })
    }

    pub fn serve(&self, input: impl BufRead, mut output: impl Write) -> std::io::Result<()> {
        for line in input.lines() {
            let line = line?;
            if line.trim().is_empty() {
                continue;
            }
            let response = match serde_json::from_str::<Value>(&line) {
                Ok(message) => self.handle_message(&message),
                Err(error) => Some(json!({
                    "jsonrpc": "2.0",
                    "id": Value::Null,
                    "error": {"code": -32700, "message": format!("parse error: {error}")}
                })),
            };
            if let Some(response) = response {
                serde_json::to_writer(&mut output, &response)?;
                output.write_all(b"\n")?;
                output.flush()?;
            }
        }
        Ok(())
    }
}

fn string_arg<'a>(arguments: &'a Value, key: &str) -> Result<&'a str, ToolError> {
    arguments
        .get(key)
        .and_then(Value::as_str)
        .ok_or_else(|| ToolError::InvalidArguments(format!("`{key}` must be a string")))
}

fn tool_result(outcome: Result<Value, ToolError>) -> Value {
    match outcome {
        Ok(value) => json!({
            "content": [{"type": "text", "text": value.to_string()}],
            "structuredContent": value,
            "isError": false
        }),
        Err(error) => {
            let payload = json!({"error": {"code": error.code(), "message": error.to_string()}});
            json!({
                "content": [{"type": "text", "text": payload.to_string()}],
                "structuredContent": payload,
                "isError": true
            })
        }
    }
}

struct ScriptState {
    peer: Option<Box<dyn BrowserPeer>>,
    calls: u64,
    failure: Option<ToolError>,
}

const SCRIPT_PRELUDE: &str = r#"
const browser = Object.freeze({
  call(tool, args) {
    const reply = JSON.parse(__maho_browser_call(String(tool), JSON.stringify(args === undefined ? {} : args)));
    if (reply.error !== undefined) { throw new Error(reply.error); }
    return reply.ok;
  }
});
"#;

fn run_browser_script(
    script: &str,
    input: &Value,
    budget: u64,
    peer_factory: &Arc<PeerFactory>,
) -> Result<Value, ToolError> {
    let runtime = Runtime::new().map_err(|error| ToolError::Js(error.to_string()))?;
    runtime.set_memory_limit(SCRIPT_MEMORY_LIMIT);
    let started = Instant::now();
    runtime.set_interrupt_handler(Some(Box::new(move || started.elapsed() >= SCRIPT_TIME_LIMIT)));
    let context = Context::full(&runtime).map_err(|error| ToolError::Js(error.to_string()))?;
    let state = Arc::new(Mutex::new(ScriptState {
        peer: None,
        calls: 0,
        failure: None,
    }));
    let input_json = input.to_string();
    let outcome: Result<String, String> = context.with(|ctx| {
        let setup = (|| -> rquickjs::Result<()> {
            let globals = ctx.globals();
            globals.set("input", ctx.json_parse(input_json.as_str())?)?;
            let call_state = Arc::clone(&state);
            let factory = Arc::clone(peer_factory);
            globals.set(
                "__maho_browser_call",
                Function::new(ctx.clone(), move |tool: String, args: String| -> String {
                    browser_call(&call_state, budget, &factory, &tool, &args)
                })?,
            )?;
            ctx.eval::<(), _>(SCRIPT_PRELUDE)?;
            Ok(())
        })();
        if let Err(error) = setup {
            return Err(error.to_string());
        }
        let evaluated = (|| -> rquickjs::Result<String> {
            let value: rquickjs::Value = ctx.eval(script)?;
            Ok(ctx
                .json_stringify(value)?
                .map(|s| s.to_string())
                .transpose()?
                .unwrap_or_else(|| "null".to_string()))
        })();
        evaluated.map_err(|error| match error {
            rquickjs::Error::Exception => ctx
                .catch()
                .get::<rquickjs::prelude::Coerced<String>>()
                .map(|message| message.0)
                .unwrap_or_else(|_| "exception".to_string()),
            other => other.to_string(),
        })
    });
    let failure = state.lock().unwrap_or_else(PoisonError::into_inner).failure.take();
    match outcome {
        Ok(json) => {
            if json.len() > MAX_RESULT_BYTES {
                return Err(ToolError::Js("script result exceeds 1 MiB".to_string()));
            }
            serde_json::from_str(&json).map_err(|error| ToolError::Js(error.to_string()))
        }
        Err(_) if started.elapsed() >= SCRIPT_TIME_LIMIT => Err(ToolError::Timeout),
        Err(message) => Err(failure.unwrap_or(ToolError::Js(message))),
    }
}

fn browser_call(
    state: &Arc<Mutex<ScriptState>>,
    budget: u64,
    factory: &PeerFactory,
    tool: &str,
    args: &str,
) -> String {
    let mut state = state.lock().unwrap_or_else(PoisonError::into_inner);
    let result = (|| -> Result<Value, ToolError> {
        if state.calls >= budget {
            return Err(ToolError::BrowserBudgetExceeded(budget));
        }
        state.calls += 1;
        let arguments: Value = serde_json::from_str(args)
            .map_err(|error| ToolError::InvalidArguments(error.to_string()))?;
        if state.peer.is_none() {
            state.peer = Some(factory()?);
        }
        state
            .peer
            .as_mut()
            .expect("peer initialised above")
            .call(tool, arguments)
    })();
    match result {
        Ok(value) => json!({"ok": value}).to_string(),
        Err(error) => {
            let message = error.to_string();
            state.failure.get_or_insert(error);
            json!({"error": message}).to_string()
        }
    }
}

pub struct StdioBrowserPeer {
    child: Child,
    stdin: ChildStdin,
    stdout: BufReader<ChildStdout>,
    next_id: u64,
}

impl StdioBrowserPeer {
    pub fn spawn(binary: &std::path::Path) -> Result<Self, ToolError> {
        let mut child = Command::new(binary)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn()
            .map_err(|error| ToolError::BrowserUnavailable(format!("{}: {error}", binary.display())))?;
        let stdin = child.stdin.take().expect("piped stdin");
        let stdout = BufReader::new(child.stdout.take().expect("piped stdout"));
        let mut peer = Self {
            child,
            stdin,
            stdout,
            next_id: 0,
        };
        peer.request(
            "initialize",
            json!({
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {},
                "clientInfo": {"name": SERVER_NAME, "version": env!("CARGO_PKG_VERSION")}
            }),
        )
        .map_err(|error| ToolError::BrowserUnavailable(error.to_string()))?;
        peer.send(&json!({"jsonrpc": "2.0", "method": "notifications/initialized"}))
            .map_err(|error| ToolError::BrowserUnavailable(error.to_string()))?;
        Ok(peer)
    }

    fn send(&mut self, message: &Value) -> Result<(), ToolError> {
        let mut line = message.to_string();
        line.push('\n');
        self.stdin
            .write_all(line.as_bytes())
            .and_then(|()| self.stdin.flush())
            .map_err(|error| ToolError::BrowserCallFailed(format!("write: {error}")))
    }

    fn request(&mut self, method: &str, params: Value) -> Result<Value, ToolError> {
        self.next_id += 1;
        let id = self.next_id;
        self.send(&json!({"jsonrpc": "2.0", "id": id, "method": method, "params": params}))?;
        loop {
            let mut line = String::new();
            let read = self
                .stdout
                .read_line(&mut line)
                .map_err(|error| ToolError::BrowserCallFailed(format!("read: {error}")))?;
            if read == 0 {
                return Err(ToolError::BrowserCallFailed("browser MCP closed its output".to_string()));
            }
            let Ok(message) = serde_json::from_str::<Value>(&line) else {
                continue;
            };
            if message.get("id").and_then(Value::as_u64) != Some(id) {
                continue;
            }
            if let Some(error) = message.get("error") {
                return Err(ToolError::BrowserCallFailed(error.to_string()));
            }
            return Ok(message.get("result").cloned().unwrap_or(Value::Null));
        }
    }
}

impl BrowserPeer for StdioBrowserPeer {
    fn call(&mut self, tool: &str, arguments: Value) -> Result<Value, ToolError> {
        let result = self.request("tools/call", json!({"name": tool, "arguments": arguments}))?;
        let text = result
            .get("content")
            .and_then(Value::as_array)
            .and_then(|items| items.iter().find_map(|item| item.get("text").and_then(Value::as_str)))
            .map(str::to_string);
        let payload = result
            .get("structuredContent")
            .cloned()
            .or_else(|| text.as_deref().and_then(|t| serde_json::from_str(t).ok()))
            .or_else(|| text.clone().map(Value::String))
            .unwrap_or(Value::Null);
        if result.get("isError").and_then(Value::as_bool) == Some(true) {
            return Err(ToolError::BrowserCallFailed(format!("{tool}: {payload}")));
        }
        Ok(payload)
    }
}

impl Drop for StdioBrowserPeer {
    fn drop(&mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
    }
}

/// Kinds a site tool may declare in a skill manifest.
pub const SITE_TOOL_KINDS: [&str; 2] = ["read", "action"];

/// Validates a site tool script before it is stored or run. Site tools are
/// replayed on later visits, so they must address elements with durable
/// locators; snapshot refs (`@e12`, `"ref": 12`) die with the snapshot that
/// minted them and are rejected.
pub fn validate_site_tool_script(script: &str) -> Result<(), ToolError> {
    if script.trim().is_empty() {
        return Err(ToolError::InvalidArguments("site tool script is empty".to_string()));
    }
    if script.len() > SCRIPT_SOURCE_LIMIT {
        return Err(ToolError::InvalidArguments("site tool script exceeds 256 KiB".to_string()));
    }
    let bytes = script.as_bytes();
    for (index, window) in bytes.windows(3).enumerate() {
        let preceded_by_word = index > 0 && (bytes[index - 1].is_ascii_alphanumeric() || bytes[index - 1] == b'_');
        if window[0] == b'@' && window[1] == b'e' && window[2].is_ascii_digit() && !preceded_by_word {
            return Err(ToolError::InvalidArguments(format!(
                "site tool uses an ephemeral snapshot ref near byte {index}; use a css/role locator"
            )));
        }
    }
    for pattern in ["ref:", "\"ref\":", "'ref':", "ref :"] {
        let mut search = 0;
        while let Some(found) = script[search..].find(pattern) {
            let start = search + found;
            let preceded_by_word = start > 0 && {
                let previous = bytes[start - 1];
                previous.is_ascii_alphanumeric() || previous == b'_'
            };
            let rest = script[start + pattern.len()..].trim_start();
            if !preceded_by_word && rest.starts_with(|c: char| c.is_ascii_digit()) {
                return Err(ToolError::InvalidArguments(format!(
                    "site tool passes a numeric snapshot ref near byte {start}; use a css/role locator"
                )));
            }
            search = start + pattern.len();
        }
    }
    Ok(())
}

/// Validates one manifest `site_tools` entry: `{name, file, kind, args_schema?}`.
pub fn validate_site_tool_entry(entry: &Value) -> Result<(), ToolError> {
    let name = entry
        .get("name")
        .and_then(Value::as_str)
        .ok_or_else(|| ToolError::InvalidArguments("site tool needs a string `name`".to_string()))?;
    if name.is_empty()
        || name.len() > 64
        || !name.chars().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == '_')
    {
        return Err(ToolError::InvalidArguments(format!(
            "site tool name `{name}` must be 1-64 chars of [a-z0-9_]"
        )));
    }
    let file = entry
        .get("file")
        .and_then(Value::as_str)
        .ok_or_else(|| ToolError::InvalidArguments(format!("site tool `{name}` needs a `file`")))?;
    if !file.ends_with(".js") || file.contains('/') || file.contains('\\') || file.starts_with('.') {
        return Err(ToolError::InvalidArguments(format!(
            "site tool `{name}` file must be a plain .js file name"
        )));
    }
    let kind = entry.get("kind").and_then(Value::as_str).unwrap_or("");
    if !SITE_TOOL_KINDS.contains(&kind) {
        return Err(ToolError::InvalidArguments(format!(
            "site tool `{name}` kind must be one of {SITE_TOOL_KINDS:?}"
        )));
    }
    if let Some(schema) = entry.get("args_schema") {
        if schema.get("type").and_then(Value::as_str) != Some("object") {
            return Err(ToolError::InvalidArguments(format!(
                "site tool `{name}` args_schema must be a JSON object schema"
            )));
        }
    }
    Ok(())
}

#[cfg(test)]
mod site_tool_tests {
    use super::*;

    #[test]
    fn durable_locator_scripts_pass() {
        let script = r#"browser.call("browser_locator_click", {tab_id: input.tab_id, locator: {css: "button[aria-label='Search']"}});
const refs = input.refs; const href = "mailto:a@example.com";"#;
        assert_eq!(validate_site_tool_script(script), Ok(()));
    }

    #[test]
    fn ephemeral_refs_are_rejected() {
        for script in [
            r#"browser.call("browser_click", {tab_id: 1, target: "@e12"})"#,
            r#"browser.call("browser_click", {"tab_id": 1, "ref": 12})"#,
            "browser.call('browser_click', {tab_id: 1, ref: 7})",
            "   ",
        ] {
            assert!(validate_site_tool_script(script).is_err(), "{script}");
        }
        assert_eq!(validate_site_tool_script("const user@example = 1"), Ok(()));
    }

    #[test]
    fn manifest_entries_are_checked() {
        let ok = json!({"name": "search_repos", "file": "search_repos.js", "kind": "read",
            "args_schema": {"type": "object", "properties": {"query": {"type": "string"}}}});
        assert_eq!(validate_site_tool_entry(&ok), Ok(()));
        for bad in [
            json!({"name": "Search", "file": "a.js", "kind": "read"}),
            json!({"name": "a", "file": "../a.js", "kind": "read"}),
            json!({"name": "a", "file": "a.sh", "kind": "read"}),
            json!({"name": "a", "file": "a.js", "kind": "delete"}),
            json!({"name": "a", "file": "a.js", "kind": "read", "args_schema": []}),
            json!({"file": "a.js", "kind": "read"}),
        ] {
            assert!(validate_site_tool_entry(&bad).is_err(), "{bad}");
        }
    }
}

#[cfg(test)]
mod automation_tool_tests {
    use super::*;
    use maho_core::automations::AutomationStore;

    fn server(store: &Path, task: &str) -> AgentToolsServer {
        let peer_factory: PeerFactory =
            Box::new(|| Err(ToolError::BrowserUnavailable("no browser in this test".into())));
        AgentToolsServer::new(PathBuf::from("/tmp/unused"), peer_factory, Box::new(NoSearchBackend))
            .with_automations(AutomationStore::new(store), task)
    }

    #[test]
    fn automation_tools_are_listed_and_scoped_to_their_task() {
        let dir = tempfile::tempdir().unwrap();
        let store = dir.path().join("automations.json");
        let names: Vec<String> = AgentToolsServer::tool_descriptors()
            .as_array()
            .unwrap()
            .iter()
            .map(|tool| tool["name"].as_str().unwrap().to_string())
            .collect();
        for name in ["automation_create", "automation_list", "automation_delete"] {
            assert!(names.iter().any(|n| n == name), "{name} missing from tools/list");
        }

        let mine = server(&store, "task-a");
        let created = mine
            .call_tool("automation_create", &json!({"name": "Inbox", "prompt": "Summarize mail", "interval_minutes": 30}))
            .unwrap();
        assert_eq!(created["interval_secs"], 1800);
        assert_eq!(created["task_id"], "task-a");
        let err = mine
            .call_tool("automation_create", &json!({"name": "x", "prompt": "y", "interval_minutes": 0}))
            .unwrap_err();
        assert_eq!(err.code(), "automation_failed");

        let other = server(&store, "task-b");
        assert_eq!(other.call_tool("automation_list", &json!({})).unwrap()["automations"], json!([]));
        let id = created["id"].as_str().unwrap();
        assert!(other.call_tool("automation_delete", &json!({"id": id})).is_err());

        assert_eq!(mine.call_tool("automation_list", &json!({})).unwrap()["automations"].as_array().unwrap().len(), 1);
        mine.call_tool("automation_delete", &json!({"id": id})).unwrap();
        assert_eq!(mine.call_tool("automation_list", &json!({})).unwrap()["automations"], json!([]));
    }

    #[test]
    fn automation_tools_fail_typed_without_a_store() {
        let peer_factory: PeerFactory = Box::new(|| Err(ToolError::BrowserUnavailable("none".into())));
        let bare = AgentToolsServer::new(PathBuf::from("/tmp/unused"), peer_factory, Box::new(NoSearchBackend));
        assert_eq!(bare.call_tool("automation_list", &json!({})).unwrap_err().code(), "automation_failed");
    }
}

#[cfg(test)]
mod http_search_tests {
    use super::*;
    use std::sync::{Arc, Mutex};

    /// Mock proxy on its own runtime thread; records the auth header and body it saw.
    fn mock_proxy(status: u16, reply: Value) -> (String, Arc<Mutex<Vec<(String, Value)>>>) {
        let seen = Arc::new(Mutex::new(Vec::new()));
        let seen_in_handler = Arc::clone(&seen);
        let (tx, rx) = std::sync::mpsc::channel();
        std::thread::spawn(move || {
            let runtime = tokio::runtime::Runtime::new().unwrap();
            runtime.block_on(async move {
                let app = axum::Router::new().route(
                    "/v1/search",
                    axum::routing::post(move |headers: axum::http::HeaderMap, axum::Json(body): axum::Json<Value>| {
                        let seen = Arc::clone(&seen_in_handler);
                        let reply = reply.clone();
                        async move {
                            let auth = headers.get("authorization").and_then(|v| v.to_str().ok()).unwrap_or("").to_string();
                            seen.lock().unwrap().push((auth, body));
                            (axum::http::StatusCode::from_u16(status).unwrap(), axum::Json(reply))
                        }
                    }),
                );
                let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
                tx.send(listener.local_addr().unwrap()).unwrap();
                axum::serve(listener, app).await.unwrap();
            });
        });
        let addr = rx.recv().unwrap();
        (format!("http://{addr}/v1"), seen)
    }

    fn server_with(search: HttpSearchBackend) -> AgentToolsServer {
        let peer_factory: PeerFactory = Box::new(|| Err(ToolError::BrowserUnavailable("none".into())));
        AgentToolsServer::new(PathBuf::from("/tmp/unused"), peer_factory, Box::new(search))
    }

    #[test]
    fn web_search_goes_through_the_proxy_with_the_maho_token() {
        let reply = json!({"kind": "web", "query": "rust", "results": [{"title": "Rust", "url": "https://rust-lang.org", "snippet": "s"}]});
        let (base, seen) = mock_proxy(200, reply.clone());
        let dir = tempfile::tempdir().unwrap();
        let models = dir.path().join("models.json");
        std::fs::write(&models, serde_json::to_vec(&json!({"providers": {
            "byok": {"baseUrl": "https://api.example/v1", "apiKey": "wrong"},
            "maho": {"baseUrl": format!("{base}/"), "apiKey": "maho-user-token"}
        }})).unwrap()).unwrap();
        let backend = HttpSearchBackend::from_models_file(&base, &models).expect("proxy provider found");

        let out = server_with(backend).call_tool("image_search", &json!({"query": "rust", "max_results": 3})).unwrap();
        assert_eq!(out, reply);
        let seen = seen.lock().unwrap();
        assert_eq!(seen.len(), 1);
        assert_eq!(seen[0].0, "Bearer maho-user-token");
        assert_eq!(seen[0].1, json!({"kind": "images", "query": "rust", "max_results": 3}));
    }

    #[test]
    fn missing_entitlement_is_typed_unavailable() {
        let (base, _) = mock_proxy(403, json!({"error": {"code": "search_not_entitled", "message": "needs a paid plan"}}));
        let err = server_with(HttpSearchBackend::new(&base, "t")).call_tool("web_search", &json!({"query": "x"})).unwrap_err();
        assert_eq!(err.code(), "search_unavailable");
        assert!(err.to_string().contains("search_not_entitled"), "{err}");

        let (base, _) = mock_proxy(400, json!({"error": {"code": "invalid_request", "message": "query is required"}}));
        let err = server_with(HttpSearchBackend::new(&base, "t")).call_tool("web_search", &json!({"query": "x"})).unwrap_err();
        assert_eq!(err.code(), "invalid_arguments");

        let unreachable = HttpSearchBackend::new("http://127.0.0.1:9/v1", "t");
        assert_eq!(server_with(unreachable).call_tool("web_search", &json!({"query": "x"})).unwrap_err().code(), "search_unavailable");
    }

    #[test]
    fn no_proxy_provider_means_no_http_backend() {
        let dir = tempfile::tempdir().unwrap();
        let models = dir.path().join("models.json");
        assert!(HttpSearchBackend::from_models_file("https://proxy.maho.co/v1", &models).is_none());
        std::fs::write(&models, br#"{"providers":{"byok":{"baseUrl":"https://api.example/v1","apiKey":"k"}}}"#).unwrap();
        assert!(HttpSearchBackend::from_models_file("https://proxy.maho.co/v1", &models).is_none());
        let err = NoSearchBackend.search(SearchKind::Web, &json!({"query": "x"})).unwrap_err();
        assert_eq!(err.code(), "search_unavailable");
    }
}
