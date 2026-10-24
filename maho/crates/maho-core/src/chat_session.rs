// Copyright 2026 Maho Browser. All rights reserved.

use futures_util::StreamExt;
use std::ffi::{c_char, c_void, CString};
use std::sync::{Arc, Mutex, MutexGuard, OnceLock};
use std::time::Duration;
use tokio::runtime::Runtime;
use tokio::task::JoinHandle;

#[derive(Clone, Debug)]
pub struct ChatConfig {
    pub api_key: String,
    pub endpoint: String,
    pub model: String,
    pub system_instruction: String,
}

pub use maho_types::tool::ToolDescriptor;

#[derive(Clone)]
pub struct MahoChatEventSink {
    pub on_token: Option<unsafe extern "C" fn(user_data: *mut c_void, token: *const c_char)>,
    pub on_thinking: Option<unsafe extern "C" fn(user_data: *mut c_void, thinking: *const c_char)>,
    pub on_complete: Option<
        unsafe extern "C" fn(
            user_data: *mut c_void,
            full_text: *const c_char,
            tool_calls_json: *const c_char,
        ),
    >,
    pub on_error: Option<unsafe extern "C" fn(user_data: *mut c_void, error: *const c_char)>,
    pub user_data: *mut c_void,
}

unsafe impl Send for MahoChatEventSink {}
unsafe impl Sync for MahoChatEventSink {}

impl MahoChatEventSink {
    pub fn on_token(&self, token: &str) {
        if let Some(cb) = self.on_token {
            if let Ok(c_token) = CString::new(token) {
                unsafe {
                    cb(self.user_data, c_token.as_ptr());
                }
            }
        }
    }

    pub fn on_thinking(&self, thinking: &str) {
        if let Some(cb) = self.on_thinking {
            if let Ok(c_thinking) = CString::new(thinking) {
                unsafe {
                    cb(self.user_data, c_thinking.as_ptr());
                }
            }
        }
    }

    pub fn on_complete(&self, full_text: &str, tool_calls_json: &str) {
        if let Some(cb) = self.on_complete {
            let c_full_text = CString::new(full_text).unwrap_or_default();
            let c_tool_calls_json = CString::new(tool_calls_json).unwrap_or_default();
            unsafe {
                cb(
                    self.user_data,
                    c_full_text.as_ptr(),
                    c_tool_calls_json.as_ptr(),
                );
            }
        }
    }

    pub fn on_error(&self, error: String) {
        if let Some(cb) = self.on_error {
            if let Ok(c_error) = CString::new(error) {
                unsafe {
                    cb(self.user_data, c_error.as_ptr());
                }
            }
        }
    }
}

const MAX_HISTORY_MESSAGES: usize = 100;
// Bound server-controlled sparse indices before allocating tool-call slots.
const MAX_TOOL_CALLS: usize = 128;
// A stream that stays silent this long is dead (keepalive comments reset it).
const STREAM_IDLE_TIMEOUT: Duration = Duration::from_secs(120);
const MAX_ERROR_CHARS: usize = 512;
const MAX_ERROR_BODY_BYTES: usize = 8 * 1024;

pub struct ChatSession {
    config: ChatConfig,
    tools: Vec<ToolDescriptor>,
    history: Vec<serde_json::Value>,
    active_task: Option<JoinHandle<()>>,
    event_sink: Option<MahoChatEventSink>,
    /// Bumped on every cancel/new run/sink replacement; a run only emits while
    /// its id is current, and only while holding the gate.
    gate: Arc<CallbackGate>,
}

/// Callback quiescence barrier shared by a session and its runs.
///
/// A run holds the lock across the generation check *and* the native callback,
/// so `invalidate` (which takes the same lock to bump the generation) returns
/// only once no callback is executing, and every later check sees the new
/// generation. After `invalidate` returns, the outgoing sink's `user_data` is
/// never touched again and may be freed by the caller.
///
/// Contract: sink callbacks must not call back into the owning `ChatSession`
/// (cancel, send, set_event_sink, free) — that would self-deadlock on this
/// non-reentrant lock. The Swift and JNI sinks only enqueue into their own
/// buffer under a leaf lock.
struct CallbackGate {
    generation: Mutex<u64>,
}

impl CallbackGate {
    fn new() -> Self {
        Self {
            generation: Mutex::new(0),
        }
    }

    /// Poisoning cannot weaken the barrier: the guarded value is a plain
    /// counter, so a panicked holder leaves it consistent.
    fn lock(&self) -> MutexGuard<'_, u64> {
        self.generation
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
    }

    /// Retires the current generation, waiting out any in-flight callback.
    fn invalidate(&self) {
        *self.lock() += 1;
    }
}

impl ChatSession {
    pub fn new(config: ChatConfig) -> Self {
        let mut history = Vec::new();
        if !config.system_instruction.is_empty() {
            history.push(serde_json::json!({
                "role": "system",
                "content": config.system_instruction
            }));
        }
        Self {
            config,
            tools: Vec::new(),
            history,
            active_task: None,
            event_sink: None,
            gate: Arc::new(CallbackGate::new()),
        }
    }

    pub fn register_tool(&mut self, name: &str, description: &str, schema_json: &str) -> bool {
        let schema: serde_json::Value = match serde_json::from_str(schema_json) {
            Ok(v) => v,
            Err(_) => return false,
        };
        self.tools.push(ToolDescriptor {
            name: name.to_string(),
            description: description.to_string(),
            parameters_schema: schema,
            provenance: maho_types::tool::ToolProvenance::BuiltinBrowser,
            sensitive: false,
            permission: maho_types::tool::ToolPermission::AutoApprove,
        });
        true
    }

    pub fn set_event_sink(&mut self, sink: MahoChatEventSink) {
        // The in-flight run captured the outgoing sink; quiesce it so the caller
        // may release the old `user_data` as soon as this returns.
        self.abort_and_join();
        self.event_sink = Some(sink);
    }

    pub fn send_user_turn(&mut self, message: &str) -> bool {
        self.history.push(serde_json::json!({
            "role": "user",
            "content": message
        }));
        self.trim_history();
        self.trigger_completion()
    }

    pub fn append_user_message(&mut self, content: &str) {
        self.history.push(serde_json::json!({
            "role": "user",
            "content": content
        }));
        self.trim_history();
    }

    pub fn append_assistant_message(&mut self, content: &str, tool_calls_json: &str) {
        let mut msg = serde_json::json!({
            "role": "assistant"
        });
        if !content.is_empty() {
            msg.as_object_mut().unwrap().insert(
                "content".to_string(),
                serde_json::Value::String(content.to_string()),
            );
        }
        if !tool_calls_json.is_empty() {
            if let Ok(serde_json::Value::Array(tcs)) = serde_json::from_str(tool_calls_json) {
                if tcs.iter().any(|tc| !tc.is_object()) {
                    let error = "Tool calls must contain only JSON objects";
                    if let Some(sink) = &self.event_sink {
                        sink.on_error(error.to_string());
                    } else {
                        eprintln!("{error}");
                    }
                    return;
                }
                let mapped_tcs: Vec<serde_json::Value> = tcs.into_iter().map(|tc| {
                    let tc_obj = tc.as_object().unwrap();
                    serde_json::json!({
                        "id": tc_obj.get("id").unwrap_or(&serde_json::Value::Null),
                        "type": "function",
                        "function": {
                            "name": tc_obj.get("name").unwrap_or(&serde_json::Value::Null),
                            "arguments": tc_obj.get("arguments_json").unwrap_or(&serde_json::Value::Null)
                        }
                    })
                }).collect();
                msg.as_object_mut().unwrap().insert(
                    "tool_calls".to_string(),
                    serde_json::Value::Array(mapped_tcs),
                );
            }
        }
        self.history.push(msg);
        self.trim_history();
    }

    pub fn send_tool_result(
        &mut self,
        tool_call_id: &str,
        tool_name: &str,
        result: &str,
        trigger: bool,
    ) -> bool {
        self.history.push(serde_json::json!({
            "role": "tool",
            "tool_call_id": tool_call_id,
            "name": tool_name,
            "content": result
        }));
        self.trim_history();
        if trigger {
            self.trigger_completion()
        } else {
            true
        }
    }

    fn trim_history(&mut self) {
        if self.history.len() <= MAX_HISTORY_MESSAGES {
            return;
        }
        let has_system = self
            .history
            .first()
            .and_then(|m| m.get("role"))
            .and_then(|r| r.as_str())
            == Some("system");
        let start_idx = if has_system { 1 } else { 0 };
        let excess = self.history.len() - MAX_HISTORY_MESSAGES;

        let mut cut_idx = (start_idx + excess).min(self.history.len());
        while cut_idx < self.history.len() {
            let role = self.history[cut_idx].get("role").and_then(|r| r.as_str());
            if role != Some("tool") {
                break;
            }
            cut_idx += 1;
        }
        if cut_idx > start_idx && cut_idx <= self.history.len() {
            self.history.drain(start_idx..cut_idx);
        }
    }

    /// Joins the outgoing run's *callbacks* (not its task): on return no sink
    /// callback is executing and none will run again. Abort only lands at an
    /// await point, so the task itself may linger but can no longer emit.
    fn abort_and_join(&mut self) {
        self.gate.invalidate();
        if let Some(task) = self.active_task.take() {
            task.abort();
        }
    }

    pub fn cancel(&mut self) {
        self.abort_and_join();
    }

    pub fn trigger_completion(&mut self) -> bool {
        self.abort_and_join();
        let sink = match &self.event_sink {
            Some(s) => s.clone(),
            None => return false,
        };

        let config = self.config.clone();
        let messages = self.history.clone();

        let tools: Vec<serde_json::Value> = self
            .tools
            .iter()
            .map(|tool| {
                serde_json::json!({
                    "type": "function",
                    "function": {
                        "name": tool.name,
                        "description": tool.description,
                        "parameters": tool.parameters_schema
                    }
                })
            })
            .collect();

        let runtime = get_runtime();
        // `&mut self` excludes a concurrent invalidate, so this id stays current
        // until the next cancel/trigger/sink replacement/drop.
        let run_id = *self.gate.lock();
        let run_sink = RunSink {
            sink,
            gate: Arc::clone(&self.gate),
            run_id,
        };
        let handle = runtime.spawn(async move {
            run_completion(config, messages, tools, run_sink).await;
        });

        self.active_task = Some(handle);
        true
    }
}

impl Drop for ChatSession {
    fn drop(&mut self) {
        self.cancel();
    }
}

fn get_runtime() -> &'static Runtime {
    static RUNTIME: OnceLock<Runtime> = OnceLock::new();
    RUNTIME.get_or_init(|| Runtime::new().expect("Failed to create tokio runtime"))
}

/// Event sink bound to a single completion run. Every callback runs under the
/// session's `CallbackGate` and only while this run's generation is current, so
/// a cancelled/superseded/dropped run can never emit, and invalidation waits for
/// an emission already in progress.
struct RunSink {
    sink: MahoChatEventSink,
    gate: Arc<CallbackGate>,
    run_id: u64,
}

impl RunSink {
    fn emit(&self, deliver: impl FnOnce(&MahoChatEventSink)) {
        let generation = self.gate.lock();
        if *generation == self.run_id {
            deliver(&self.sink);
        }
        drop(generation);
    }

    fn on_token(&self, token: &str) {
        self.emit(|sink| sink.on_token(token));
    }

    fn on_thinking(&self, thinking: &str) {
        self.emit(|sink| sink.on_thinking(thinking));
    }

    fn on_complete(&self, full_text: &str, tool_calls_json: &str) {
        self.emit(|sink| sink.on_complete(full_text, tool_calls_json));
    }

    fn on_error(&self, error: String) {
        self.emit(|sink| sink.on_error(error));
    }
}

impl From<MahoChatEventSink> for RunSink {
    fn from(sink: MahoChatEventSink) -> Self {
        Self {
            sink,
            gate: Arc::new(CallbackGate::new()),
            run_id: 0,
        }
    }
}

/// Redacts the token following every case-insensitive `marker` occurrence.
fn redact_after(text: &str, marker: &str) -> String {
    let lower = text.to_ascii_lowercase();
    let mut out = String::with_capacity(text.len());
    let mut cursor = 0;
    while let Some(rel) = lower[cursor..].find(marker) {
        let start = cursor + rel + marker.len();
        out.push_str(&text[cursor..start]);
        let end = text[start..]
            .find(|c: char| {
                c.is_whitespace() || matches!(c, '&' | '"' | '\'' | ',' | ';' | ')' | '>')
            })
            .map_or(text.len(), |i| start + i);
        if end > start {
            out.push_str("[REDACTED]");
        }
        cursor = end;
    }
    out.push_str(&text[cursor..]);
    out
}

/// Bounds and scrubs upstream/transport error text before it reaches the UI:
/// redacts `Bearer ` and `key=` credentials and caps the length.
fn sanitize_error_text(text: &str) -> String {
    let redacted = redact_after(&redact_after(text, "bearer "), "key=");
    if redacted.chars().count() <= MAX_ERROR_CHARS {
        return redacted;
    }
    let mut truncated: String = redacted.chars().take(MAX_ERROR_CHARS).collect();
    truncated.push_str("...");
    truncated
}

async fn run_completion(
    config: ChatConfig,
    messages: Vec<serde_json::Value>,
    tools: Vec<serde_json::Value>,
    sink: impl Into<RunSink>,
) {
    run_completion_with_idle(config, messages, tools, sink.into(), STREAM_IDLE_TIMEOUT).await;
}

async fn run_completion_with_idle(
    config: ChatConfig,
    messages: Vec<serde_json::Value>,
    tools: Vec<serde_json::Value>,
    sink: RunSink,
    idle_timeout: Duration,
) {
    let client = crate::http::shared_http_client();
    let mut req = client
        .post(&config.endpoint)
        .header("Content-Type", "application/json");

    if !config.api_key.is_empty() {
        req = req.header("Authorization", format!("Bearer {}", config.api_key));
    }

    let mut body = serde_json::json!({
        "model": config.model,
        "messages": messages,
        "stream": true,
    });

    if !tools.is_empty() {
        body.as_object_mut()
            .unwrap()
            .insert("tools".to_string(), serde_json::Value::Array(tools));
    }

    let res = match req.json(&body).send().await {
        Ok(r) => r,
        Err(e) => {
            sink.on_error(format!(
                "Request failed: {}",
                sanitize_error_text(&e.without_url().to_string())
            ));
            return;
        }
    };

    if !res.status().is_success() {
        let status = res.status();
        // Read at most a bounded prefix of the upstream error body.
        let mut err_bytes = Vec::new();
        let mut err_stream = res.bytes_stream();
        while err_bytes.len() < MAX_ERROR_BODY_BYTES {
            match err_stream.next().await {
                Some(Ok(chunk)) => err_bytes.extend_from_slice(&chunk),
                _ => break,
            }
        }
        let err_text = String::from_utf8_lossy(&err_bytes);
        sink.on_error(format!(
            "HTTP error {}: {}",
            status,
            sanitize_error_text(&err_text)
        ));
        return;
    }

    let mut stream = res.bytes_stream();
    let mut utf8_pending = Vec::new();
    let mut sse_buffer = String::new();
    let mut full_text = String::new();
    let mut tool_calls = Vec::<serde_json::Value>::new();
    // A stream is complete only once it carried [DONE] or a finish_reason.
    let mut done = false;
    let mut finished = false;

    'stream: loop {
        let next = match tokio::time::timeout(idle_timeout, stream.next()).await {
            Ok(next) => next,
            Err(_) => {
                sink.on_error(format!("Stream idle for more than {:?}", idle_timeout));
                return;
            }
        };
        let Some(chunk_result) = next else {
            break;
        };
        let chunk = match chunk_result {
            Ok(bytes) => bytes,
            Err(e) => {
                sink.on_error(format!(
                    "Stream error: {}",
                    sanitize_error_text(&e.without_url().to_string())
                ));
                return;
            }
        };

        let chunk_str = match crate::stream_utils::push_utf8_chunk(&mut utf8_pending, &chunk) {
            Ok(s) => s,
            Err(_) => {
                sink.on_error("Invalid UTF-8 chunk".to_string());
                return;
            }
        };

        sse_buffer.push_str(&chunk_str);

        while let Some((pos, separator_len)) = find_sse_separator(&sse_buffer) {
            let event_block = sse_buffer[..pos].to_string();
            sse_buffer.drain(..pos + separator_len);

            for line in event_block.lines() {
                // Comment/keepalive lines (": ping") and other fields are ignored.
                if !line.starts_with("data:") {
                    continue;
                }
                let data = line["data:".len()..].trim();
                if data.is_empty() {
                    continue;
                }
                if data == "[DONE]" {
                    done = true;
                    break 'stream;
                }

                let json: serde_json::Value = match serde_json::from_str(data) {
                    Ok(val) => val,
                    Err(_) => {
                        sink.on_error("Malformed stream frame".to_string());
                        return;
                    }
                };

                if let Some(err) = json.get("error").filter(|e| !e.is_null()) {
                    let message = err
                        .get("message")
                        .and_then(|m| m.as_str())
                        .map(str::to_string)
                        .or_else(|| err.as_str().map(str::to_string))
                        .unwrap_or_else(|| err.to_string());
                    sink.on_error(format!("Stream error: {}", sanitize_error_text(&message)));
                    return;
                }

                if let Some(choices) = json.pointer("/choices").and_then(|v| v.as_array()) {
                    if let Some(choice) = choices.first() {
                        if choice.get("finish_reason").is_some_and(|f| !f.is_null()) {
                            finished = true;
                        }
                        if let Some(delta) = choice.get("delta") {
                            // Reasoning/thinking deltas (OpenAI o-series & compatible:
                            // `reasoning_content`, Anthropic-style: `reasoning`).
                            let thinking_text = delta
                                .get("reasoning_content")
                                .or_else(|| delta.get("reasoning"))
                                .and_then(|r| r.as_str());
                            if let Some(reasoning) = thinking_text {
                                if !reasoning.is_empty() {
                                    sink.on_thinking(reasoning);
                                }
                            }

                            if let Some(content) = delta.get("content").and_then(|c| c.as_str()) {
                                if !content.is_empty() {
                                    full_text.push_str(content);
                                    sink.on_token(content);
                                }
                            }

                            if let Some(delta_tool_calls) =
                                delta.get("tool_calls").and_then(|tc| tc.as_array())
                            {
                                for tc in delta_tool_calls {
                                    let Some(index) = tool_call_slot(&tool_calls, tc) else {
                                        sink.on_error("Invalid tool-call index".to_string());
                                        return;
                                    };
                                    if tool_calls.len() <= index {
                                        tool_calls.resize(
                                            index + 1,
                                            serde_json::json!({
                                                "index": index,
                                                "id": "",
                                                "name": "",
                                                "arguments": ""
                                            }),
                                        );
                                    }

                                    let tc_obj = tool_calls[index].as_object_mut().unwrap();
                                    if let Some(id) = tc.get("id").and_then(|id| id.as_str()) {
                                        tc_obj.insert(
                                            "id".to_string(),
                                            serde_json::Value::String(id.to_string()),
                                        );
                                    }
                                    if let Some(func) = tc.get("function") {
                                        if let Some(name) =
                                            func.get("name").and_then(|n| n.as_str())
                                        {
                                            tc_obj.insert(
                                                "name".to_string(),
                                                serde_json::Value::String(name.to_string()),
                                            );
                                        }
                                        if let Some(args) =
                                            func.get("arguments").and_then(|a| a.as_str())
                                        {
                                            let current_args = tc_obj.get_mut("arguments").unwrap();
                                            let mut new_args = current_args
                                                .as_str()
                                                .unwrap_or_default()
                                                .to_string();
                                            new_args.push_str(args);
                                            *current_args = serde_json::Value::String(new_args);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if !utf8_pending.is_empty() {
        sink.on_error("Incomplete UTF-8 stream".to_string());
        return;
    }

    if !done && !finished {
        sink.on_error("stream ended unexpectedly".to_string());
        return;
    }

    // Sparse slots and nameless fragments are not executable calls.
    tool_calls.retain(|tc| {
        tc.get("name")
            .and_then(|n| n.as_str())
            .is_some_and(|n| !n.is_empty())
    });

    let tool_calls_json = if !tool_calls.is_empty() {
        let mapped_tool_calls: Vec<serde_json::Value> = tool_calls
            .into_iter()
            .map(|tc| {
                let tc_obj = tc.as_object().unwrap();
                serde_json::json!({
                    "index": tc_obj.get("index").unwrap(),
                    "id": tc_obj.get("id").unwrap(),
                    "name": tc_obj.get("name").unwrap(),
                    "arguments_json": tc_obj.get("arguments").unwrap()
                })
            })
            .collect();
        serde_json::to_string(&mapped_tool_calls).unwrap_or_default()
    } else {
        String::new()
    };

    sink.on_complete(&full_text, &tool_calls_json);
}

/// Resolves the slot a streamed tool-call fragment belongs to. A usable integer
/// `index` wins; when it is absent or not an integer, a new id opens a new
/// slot (parallel calls) and an id-less fragment continues the latest slot.
/// Returns `None` when the slot would exceed `MAX_TOOL_CALLS`.
fn tool_call_slot(tool_calls: &[serde_json::Value], tc: &serde_json::Value) -> Option<usize> {
    let index = match tc.get("index").and_then(serde_json::Value::as_u64) {
        Some(index) => usize::try_from(index).ok()?,
        None => match tc
            .get("id")
            .and_then(|id| id.as_str())
            .filter(|id| !id.is_empty())
        {
            Some(id) => tool_calls
                .iter()
                .position(|slot| slot.get("id").and_then(|i| i.as_str()) == Some(id))
                .unwrap_or(tool_calls.len()),
            None => tool_calls.len().saturating_sub(1),
        },
    };
    (index < MAX_TOOL_CALLS).then_some(index)
}

fn find_sse_separator(buffer: &str) -> Option<(usize, usize)> {
    match (buffer.find("\n\n"), buffer.find("\r\n\r\n")) {
        (Some(lf), Some(crlf)) if lf < crlf => Some((lf, 2)),
        (_, Some(crlf)) => Some((crlf, 4)),
        (Some(lf), None) => Some((lf, 2)),
        (None, None) => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CStr;
    use std::io::{Read, Write};
    use std::net::{SocketAddr, TcpListener, TcpStream};
    use std::sync::{mpsc, Mutex};
    use std::thread::{self, JoinHandle};
    use std::time::Duration;

    const TEST_TIMEOUT: Duration = Duration::from_secs(5);

    #[derive(Clone, Debug, PartialEq)]
    enum CallbackEvent {
        Token(String),
        Thinking(String),
        Complete {
            full_text: String,
            tool_calls_json: String,
        },
        Error(String),
    }

    struct CallbackState {
        events: Mutex<Vec<CallbackEvent>>,
        event_tx: mpsc::Sender<CallbackEvent>,
    }

    impl CallbackState {
        fn record(&self, event: CallbackEvent) {
            if let Ok(mut events) = self.events.lock() {
                events.push(event.clone());
            }
            let _ = self.event_tx.send(event);
        }
    }

    unsafe extern "C" fn on_token(user_data: *mut c_void, token: *const c_char) {
        let state = &*(user_data.cast::<CallbackState>());
        let token = CStr::from_ptr(token).to_string_lossy().into_owned();
        state.record(CallbackEvent::Token(token));
    }

    unsafe extern "C" fn on_thinking(user_data: *mut c_void, thinking: *const c_char) {
        let state = &*(user_data.cast::<CallbackState>());
        let thinking = CStr::from_ptr(thinking).to_string_lossy().into_owned();
        state.record(CallbackEvent::Thinking(thinking));
    }

    unsafe extern "C" fn on_complete(
        user_data: *mut c_void,
        full_text: *const c_char,
        tool_calls_json: *const c_char,
    ) {
        let state = &*(user_data.cast::<CallbackState>());
        state.record(CallbackEvent::Complete {
            full_text: CStr::from_ptr(full_text).to_string_lossy().into_owned(),
            tool_calls_json: CStr::from_ptr(tool_calls_json)
                .to_string_lossy()
                .into_owned(),
        });
    }

    unsafe extern "C" fn on_error(user_data: *mut c_void, error: *const c_char) {
        let state = &*(user_data.cast::<CallbackState>());
        let error = CStr::from_ptr(error).to_string_lossy().into_owned();
        state.record(CallbackEvent::Error(error));
    }

    #[derive(Debug)]
    struct CapturedRequest {
        request_target: String,
        headers: Vec<(String, String)>,
        body: Vec<u8>,
    }

    impl CapturedRequest {
        fn header(&self, name: &str) -> Option<&str> {
            self.headers
                .iter()
                .find(|(header_name, _)| header_name.eq_ignore_ascii_case(name))
                .map(|(_, value)| value.as_str())
        }
    }

    struct TestServer {
        addr: SocketAddr,
        frame_permit_tx: mpsc::Sender<()>,
        request_rx: mpsc::Receiver<CapturedRequest>,
        thread: Option<JoinHandle<Result<(), String>>>,
    }

    impl TestServer {
        fn spawn() -> Self {
            Self::spawn_with_separator("\n\n")
        }

        fn spawn_with_separator(separator: &'static str) -> Self {
            let listener = TcpListener::bind("127.0.0.1:0").expect("bind loopback server");
            let addr = listener.local_addr().expect("read loopback address");
            let (frame_permit_tx, frame_permit_rx) = mpsc::channel();
            let (request_tx, request_rx) = mpsc::channel();
            let thread = thread::spawn(move || {
                let (mut stream, _) = listener.accept().map_err(|error| error.to_string())?;
                stream
                    .set_read_timeout(Some(TEST_TIMEOUT))
                    .map_err(|error| error.to_string())?;
                stream
                    .set_write_timeout(Some(TEST_TIMEOUT))
                    .map_err(|error| error.to_string())?;

                let request = read_request(&mut stream)?;
                request_tx
                    .send(request)
                    .map_err(|error| error.to_string())?;

                stream
                    .write_all(
                        b"HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n",
                    )
                    .map_err(|error| error.to_string())?;
                write_sse_frame(
                    &mut stream,
                    &format!(
                        "data: {{\"choices\":[{{\"delta\":{{\"content\":\"Hello\"}}}}]}}{separator}"
                    ),
                )?;
                frame_permit_rx
                    .recv_timeout(TEST_TIMEOUT)
                    .map_err(|error| error.to_string())?;
                write_sse_frame(
                    &mut stream,
                    &format!(
                        "data: {{\"choices\":[{{\"delta\":{{\"content\":\" world\"}}}}]}}{separator}"
                    ),
                )?;
                frame_permit_rx
                    .recv_timeout(TEST_TIMEOUT)
                    .map_err(|error| error.to_string())?;
                write_sse_frame(&mut stream, &format!("data: [DONE]{separator}"))?;
                Ok(())
            });

            Self {
                addr,
                frame_permit_tx,
                request_rx,
                thread: Some(thread),
            }
        }

        fn endpoint(&self) -> String {
            format!("http://{}/v1/chat/completions", self.addr)
        }

        /// Spawn a server that streams the caller-supplied SSE data payloads
        /// (`data: <payload>` per frame), waiting for a permit between frames.
        fn spawn_with_frames(frames: Vec<String>) -> Self {
            let listener = TcpListener::bind("127.0.0.1:0").expect("bind loopback server");
            let addr = listener.local_addr().expect("read loopback address");
            let (frame_permit_tx, frame_permit_rx) = mpsc::channel();
            let (request_tx, request_rx) = mpsc::channel();
            let thread = thread::spawn(move || {
                let (mut stream, _) = listener.accept().map_err(|error| error.to_string())?;
                stream
                    .set_read_timeout(Some(TEST_TIMEOUT))
                    .map_err(|error| error.to_string())?;
                stream
                    .set_write_timeout(Some(TEST_TIMEOUT))
                    .map_err(|error| error.to_string())?;

                let request = read_request(&mut stream)?;
                request_tx
                    .send(request)
                    .map_err(|error| error.to_string())?;

                stream
                    .write_all(
                        b"HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n",
                    )
                    .map_err(|error| error.to_string())?;

                for frame in frames {
                    write_sse_frame(&mut stream, &format!("data: {frame}\n\n"))?;
                    frame_permit_rx
                        .recv_timeout(TEST_TIMEOUT)
                        .map_err(|error| error.to_string())?;
                }
                write_sse_frame(&mut stream, "data: [DONE]\n\n")?;
                Ok(())
            });

            Self {
                addr,
                frame_permit_tx,
                request_rx,
                thread: Some(thread),
            }
        }

        fn captured_request(&self) -> CapturedRequest {
            self.request_rx
                .recv_timeout(TEST_TIMEOUT)
                .expect("receive captured request")
        }

        fn release_next_frame(&self) {
            self.frame_permit_tx
                .send(())
                .expect("release next SSE frame");
        }

        fn join(&mut self) {
            let result = self
                .thread
                .take()
                .expect("server thread is present")
                .join()
                .expect("server thread did not panic");
            result.expect("loopback server completed successfully");
        }
    }

    impl Drop for TestServer {
        fn drop(&mut self) {
            if let Some(thread) = self.thread.take() {
                let _ = self.frame_permit_tx.send(());
                let _ = self.frame_permit_tx.send(());
                let _ = TcpStream::connect_timeout(&self.addr, TEST_TIMEOUT);
                let _ = thread.join();
            }
        }
    }

    fn write_sse_frame(stream: &mut TcpStream, frame: &str) -> Result<(), String> {
        stream
            .write_all(frame.as_bytes())
            .map_err(|error| error.to_string())?;
        stream.flush().map_err(|error| error.to_string())
    }

    fn read_request(stream: &mut TcpStream) -> Result<CapturedRequest, String> {
        let mut bytes = Vec::new();
        let header_end = loop {
            if let Some(position) = bytes.windows(4).position(|window| window == b"\r\n\r\n") {
                break position + 4;
            }
            let mut buffer = [0_u8; 1024];
            let read = stream
                .read(&mut buffer)
                .map_err(|error| error.to_string())?;
            if read == 0 {
                return Err("connection closed before request headers completed".to_string());
            }
            bytes.extend_from_slice(&buffer[..read]);
        };

        let head = std::str::from_utf8(&bytes[..header_end]).map_err(|error| error.to_string())?;
        let mut lines = head[..head.len() - 4].split("\r\n");
        let request_line = lines
            .next()
            .ok_or_else(|| "request line is missing".to_string())?;
        let mut request_line_parts = request_line.split_whitespace();
        let method = request_line_parts
            .next()
            .ok_or_else(|| "request method is missing".to_string())?;
        let request_target = request_line_parts
            .next()
            .ok_or_else(|| "request target is missing".to_string())?
            .to_string();
        if method != "POST" {
            return Err(format!("expected POST request, got {method}"));
        }

        let headers: Vec<(String, String)> = lines
            .map(|line| {
                line.split_once(':')
                    .map(|(name, value)| (name.to_string(), value.trim().to_string()))
                    .ok_or_else(|| format!("malformed request header: {line}"))
            })
            .collect::<Result<_, _>>()?;
        let content_length = headers
            .iter()
            .find(|(name, _)| name.eq_ignore_ascii_case("content-length"))
            .ok_or_else(|| "content-length header is missing".to_string())?
            .1
            .parse::<usize>()
            .map_err(|error| error.to_string())?;

        while bytes.len() < header_end + content_length {
            let mut buffer = [0_u8; 1024];
            let read = stream
                .read(&mut buffer)
                .map_err(|error| error.to_string())?;
            if read == 0 {
                return Err("connection closed before request body completed".to_string());
            }
            bytes.extend_from_slice(&buffer[..read]);
        }

        Ok(CapturedRequest {
            request_target,
            headers,
            body: bytes[header_end..header_end + content_length].to_vec(),
        })
    }

    #[test]
    fn review_malformed_tool_history_is_rejected_atomically() {
        let (event_tx, event_rx) = mpsc::channel();
        let state = Box::new(CallbackState { events: Mutex::new(Vec::new()), event_tx });
        let mut session = ChatSession::new(ChatConfig { api_key: String::new(), endpoint: String::new(), model: String::new(), system_instruction: String::new() });
        session.set_event_sink(MahoChatEventSink { on_token: None, on_thinking: None, on_complete: None, on_error: Some(on_error), user_data: (&*state as *const CallbackState).cast_mut().cast() });
        for invalid in ["[null]", "[1]", "[\"text\"]", "[{\"id\":\"valid\"},null]"] {
            session.append_assistant_message("do not append", invalid);
            assert!(session.history.is_empty());
            assert!(matches!(event_rx.recv_timeout(TEST_TIMEOUT), Ok(CallbackEvent::Error(_))));
        }
    }

    #[test]
    fn review_tool_index_is_bounded() {
        let mut server = TestServer::spawn_with_frames(vec![serde_json::json!({"choices":[{"delta":{"tool_calls":[{"index":65536,"function":{"name":"bad"}}]}}]}).to_string()]);
        let (event_tx, event_rx) = mpsc::channel();
        let state = Box::new(CallbackState { events: Mutex::new(Vec::new()), event_tx });
        let config = ChatConfig { api_key: String::new(), endpoint: server.endpoint(), model: "test".into(), system_instruction: String::new() };
        let sink = MahoChatEventSink { on_token: None, on_thinking: None, on_complete: Some(on_complete), on_error: Some(on_error), user_data: (&*state as *const CallbackState).cast_mut().cast() };
        server.release_next_frame();
        get_runtime().block_on(run_completion(config, vec![], vec![], sink));
        server.join();
        assert!(matches!(event_rx.recv_timeout(TEST_TIMEOUT), Ok(CallbackEvent::Error(_))));
    }

    #[test]
    fn review_chat_utf8_spans_http_chunks() {
        for split in 1..4 {
            let body = format!("data: {}\n\ndata: [DONE]\n\n", serde_json::json!({"choices":[{"delta":{"content":"\u{1f642}"}}]}));
            let boundary = body.find('\u{1f642}').unwrap() + split;
            let listener = TcpListener::bind("127.0.0.1:0").unwrap();
            let endpoint = format!("http://{}", listener.local_addr().unwrap());
            let server = thread::spawn(move || {
                let (mut socket, _) = listener.accept().unwrap();
                socket.set_read_timeout(Some(TEST_TIMEOUT)).unwrap();
                read_request(&mut socket).unwrap();
                socket.write_all(b"HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n").unwrap();
                for chunk in [&body.as_bytes()[..boundary], &body.as_bytes()[boundary..]] {
                    write!(socket, "{:x}\r\n", chunk.len()).unwrap();
                    socket.write_all(chunk).unwrap();
                    socket.write_all(b"\r\n").unwrap();
                }
                socket.write_all(b"0\r\n\r\n").unwrap();
            });
            let (event_tx, event_rx) = mpsc::channel();
            let state = Box::new(CallbackState { events: Mutex::new(Vec::new()), event_tx });
            let config = ChatConfig { api_key: String::new(), endpoint, model: "test".into(), system_instruction: String::new() };
            let sink = MahoChatEventSink { on_token: None, on_thinking: None, on_complete: Some(on_complete), on_error: Some(on_error), user_data: (&*state as *const CallbackState).cast_mut().cast() };
            get_runtime().block_on(run_completion(config, vec![], vec![], sink));
            server.join().unwrap();
            assert_eq!(event_rx.recv_timeout(TEST_TIMEOUT).unwrap(), CallbackEvent::Complete { full_text: "\u{1f642}".into(), tool_calls_json: String::new() });
        }
    }

    #[test]
    fn openai_compatible_sse_crlf_frames_stream_tokens_in_order() {
        let mut server = TestServer::spawn_with_separator("\r\n\r\n");
        let (event_tx, event_rx) = mpsc::channel();
        let callback_state = Box::new(CallbackState {
            events: Mutex::new(Vec::new()),
            event_tx,
        });
        let user_data = (&*callback_state as *const CallbackState).cast_mut().cast();

        let mut session = ChatSession::new(ChatConfig {
            api_key: String::new(),
            endpoint: server.endpoint(),
            model: "test-model".to_string(),
            system_instruction: String::new(),
        });
        session.set_event_sink(MahoChatEventSink {
            on_token: Some(on_token),
            on_thinking: None,
            on_complete: Some(on_complete),
            on_error: Some(on_error),
            user_data,
        });

        assert!(session.send_user_turn("Say hello"));
        let _request = server.captured_request();
        assert_eq!(
            event_rx.recv_timeout(TEST_TIMEOUT),
            Ok(CallbackEvent::Token("Hello".to_string()))
        );
        server.release_next_frame();
        assert_eq!(
            event_rx.recv_timeout(TEST_TIMEOUT),
            Ok(CallbackEvent::Token(" world".to_string()))
        );
        server.release_next_frame();
        assert_eq!(
            event_rx.recv_timeout(TEST_TIMEOUT),
            Ok(CallbackEvent::Complete {
                full_text: "Hello world".to_string(),
                tool_calls_json: String::new(),
            })
        );
        server.join();

        assert_eq!(
            *callback_state.events.lock().expect("lock callback events"),
            vec![
                CallbackEvent::Token("Hello".to_string()),
                CallbackEvent::Token(" world".to_string()),
                CallbackEvent::Complete {
                    full_text: "Hello world".to_string(),
                    tool_calls_json: String::new(),
                },
            ]
        );
    }

    #[test]
    fn openai_compatible_sse_uses_configured_endpoint_and_streams_tokens_in_order() {
        let mut server = TestServer::spawn();
        let endpoint = server.endpoint();
        let (event_tx, event_rx) = mpsc::channel();
        let callback_state = Box::new(CallbackState {
            events: Mutex::new(Vec::new()),
            event_tx,
        });
        let user_data = (&*callback_state as *const CallbackState).cast_mut().cast();

        let mut session = ChatSession::new(ChatConfig {
            api_key: "test-api-key".to_string(),
            endpoint: endpoint.clone(),
            model: "test-model".to_string(),
            system_instruction: "Follow the test instructions.".to_string(),
        });
        session.set_event_sink(MahoChatEventSink {
            on_token: Some(on_token),
            on_thinking: None,
            on_complete: Some(on_complete),
            on_error: Some(on_error),
            user_data,
        });

        assert!(session.send_user_turn("Say hello"));
        let request = server.captured_request();

        assert_eq!(
            event_rx.recv_timeout(TEST_TIMEOUT),
            Ok(CallbackEvent::Token("Hello".to_string()))
        );
        server.release_next_frame();
        assert_eq!(
            event_rx.recv_timeout(TEST_TIMEOUT),
            Ok(CallbackEvent::Token(" world".to_string()))
        );
        server.release_next_frame();
        assert_eq!(
            event_rx.recv_timeout(TEST_TIMEOUT),
            Ok(CallbackEvent::Complete {
                full_text: "Hello world".to_string(),
                tool_calls_json: String::new(),
            })
        );
        server.join();

        assert_eq!(request.request_target, "/v1/chat/completions");
        assert_eq!(request.header("authorization"), Some("Bearer test-api-key"));
        assert_eq!(request.header("content-type"), Some("application/json"));
        assert_eq!(
            serde_json::from_slice::<serde_json::Value>(&request.body).expect("parse request body"),
            serde_json::json!({
                "model": "test-model",
                "messages": [
                    {"role": "system", "content": "Follow the test instructions."},
                    {"role": "user", "content": "Say hello"}
                ],
                "stream": true
            })
        );
        assert_eq!(
            *callback_state.events.lock().expect("lock callback events"),
            vec![
                CallbackEvent::Token("Hello".to_string()),
                CallbackEvent::Token(" world".to_string()),
                CallbackEvent::Complete {
                    full_text: "Hello world".to_string(),
                    tool_calls_json: String::new(),
                },
            ]
        );
    }

    #[test]
    fn openai_compatible_sse_streams_reasoning_content_as_thinking() {
        let frames = vec![
            serde_json::json!({"choices":[{"delta":{"reasoning_content":"Let me think"}}]})
                .to_string(),
            serde_json::json!({"choices":[{"delta":{"reasoning_content":" about it."}}]})
                .to_string(),
            serde_json::json!({"choices":[{"delta":{"content":"The answer"}}]}).to_string(),
        ];
        let mut server = TestServer::spawn_with_frames(frames);
        let (event_tx, event_rx) = mpsc::channel();
        let callback_state = Box::new(CallbackState {
            events: Mutex::new(Vec::new()),
            event_tx,
        });
        let user_data = (&*callback_state as *const CallbackState).cast_mut().cast();

        let mut session = ChatSession::new(ChatConfig {
            api_key: String::new(),
            endpoint: server.endpoint(),
            model: "test-model".to_string(),
            system_instruction: String::new(),
        });
        session.set_event_sink(MahoChatEventSink {
            on_token: Some(on_token),
            on_thinking: Some(on_thinking),
            on_complete: Some(on_complete),
            on_error: Some(on_error),
            user_data,
        });

        assert!(session.send_user_turn("Think"));
        let _request = server.captured_request();

        assert_eq!(
            event_rx.recv_timeout(TEST_TIMEOUT),
            Ok(CallbackEvent::Thinking("Let me think".to_string()))
        );
        server.release_next_frame();
        assert_eq!(
            event_rx.recv_timeout(TEST_TIMEOUT),
            Ok(CallbackEvent::Thinking(" about it.".to_string()))
        );
        server.release_next_frame();
        assert_eq!(
            event_rx.recv_timeout(TEST_TIMEOUT),
            Ok(CallbackEvent::Token("The answer".to_string()))
        );
        server.release_next_frame();
        assert_eq!(
            event_rx.recv_timeout(TEST_TIMEOUT),
            Ok(CallbackEvent::Complete {
                full_text: "The answer".to_string(),
                tool_calls_json: String::new(),
            })
        );
        server.join();
    }

    /// Serves one fixed HTTP/1.1 response body and returns the endpoint.
    fn serve_raw(status: &'static str, body: String) -> (String, thread::JoinHandle<()>) {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let endpoint = format!("http://{}", listener.local_addr().unwrap());
        let server = thread::spawn(move || {
            let (mut socket, _) = listener.accept().unwrap();
            socket.set_read_timeout(Some(TEST_TIMEOUT)).unwrap();
            read_request(&mut socket).unwrap();
            write!(
                socket,
                "HTTP/1.1 {status}\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n{body}"
            )
            .unwrap();
        });
        (endpoint, server)
    }

    fn run_raw(status: &'static str, body: &str) -> Vec<CallbackEvent> {
        let (endpoint, server) = serve_raw(status, body.to_string());
        let (event_tx, _event_rx) = mpsc::channel();
        let state = Box::new(CallbackState { events: Mutex::new(Vec::new()), event_tx });
        let config = ChatConfig { api_key: String::new(), endpoint, model: "test".into(), system_instruction: String::new() };
        let sink = MahoChatEventSink { on_token: Some(on_token), on_thinking: None, on_complete: Some(on_complete), on_error: Some(on_error), user_data: (&*state as *const CallbackState).cast_mut().cast() };
        get_runtime().block_on(run_completion(config, vec![], vec![], sink));
        server.join().unwrap();
        let events = state.events.lock().unwrap().clone();
        events
    }

    #[test]
    fn review_truncated_stream_is_an_error_not_success() {
        let events = run_raw("200 OK", "data: {\"choices\":[{\"delta\":{\"content\":\"partial\"}}]}\n\n");
        assert_eq!(events.last(), Some(&CallbackEvent::Error("stream ended unexpectedly".into())));
        assert!(!events.iter().any(|e| matches!(e, CallbackEvent::Complete { .. })));
    }

    #[test]
    fn review_finish_reason_without_done_completes_and_keepalives_are_ignored() {
        let events = run_raw(
            "200 OK",
            ": keepalive\n\ndata: {\"choices\":[{\"delta\":{\"content\":\"ok\"}}]}\n\ndata: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n",
        );
        assert_eq!(events.last(), Some(&CallbackEvent::Complete { full_text: "ok".into(), tool_calls_json: String::new() }));
    }

    #[test]
    fn review_midstream_error_frame_and_parse_failure_surface_on_error() {
        let events = run_raw("200 OK", "data: {\"error\":{\"message\":\"overloaded\"}}\n\n");
        assert_eq!(events, vec![CallbackEvent::Error("Stream error: overloaded".into())]);
        let events = run_raw("200 OK", "data: {not json\n\n");
        assert_eq!(events, vec![CallbackEvent::Error("Malformed stream frame".into())]);
    }

    #[test]
    fn review_done_stops_the_whole_stream() {
        let events = run_raw(
            "200 OK",
            "data: {\"choices\":[{\"delta\":{\"content\":\"A\"}}]}\n\ndata: [DONE]\n\ndata: {\"choices\":[{\"delta\":{\"content\":\"B\"}}]}\n\n",
        );
        assert_eq!(events.last(), Some(&CallbackEvent::Complete { full_text: "A".into(), tool_calls_json: String::new() }));
    }

    #[test]
    fn review_parallel_tool_calls_without_index_get_separate_slots() {
        let events = run_raw(
            "200 OK",
            concat!(
                "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"id\":\"c1\",\"function\":{\"name\":\"a\",\"arguments\":\"{}\"}}]}}]}\n\n",
                "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"id\":\"c2\",\"function\":{\"name\":\"b\",\"arguments\":\"{}\"}}]}}]}\n\n",
                "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":\"x\",\"function\":{\"arguments\":\"\"}}]}}]}\n\n",
                "data: [DONE]\n\n",
            ),
        );
        let Some(CallbackEvent::Complete { tool_calls_json, .. }) = events.last() else { panic!("{events:?}") };
        let calls: Vec<serde_json::Value> = serde_json::from_str(tool_calls_json).unwrap();
        let names: Vec<_> = calls.iter().map(|c| c["name"].as_str().unwrap()).collect();
        assert_eq!(names, ["a", "b"]);
    }

    #[test]
    fn review_http_error_is_redacted_and_bounded() {
        let body = format!("bad key=sk-secret Bearer tok123 {}", "x".repeat(5000));
        let events = run_raw("500 Internal Server Error", &body);
        let [CallbackEvent::Error(message)] = events.as_slice() else { panic!("{events:?}") };
        assert!(!message.contains("sk-secret") && !message.contains("tok123"), "{message}");
        assert!(message.chars().count() < 600);
    }

    #[test]
    fn review_sanitize_error_text_redacts_credentials() {
        assert_eq!(sanitize_error_text("GET /v1?key=abc&x=1 failed"), "GET /v1?key=[REDACTED]&x=1 failed");
        assert_eq!(sanitize_error_text("Authorization: Bearer abc.def"), "Authorization: Bearer [REDACTED]");
    }

    #[test]
    fn review_stream_idle_timeout_reports_error() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let endpoint = format!("http://{}", listener.local_addr().unwrap());
        let (release_tx, release_rx) = mpsc::channel::<()>();
        let server = thread::spawn(move || {
            let (mut socket, _) = listener.accept().unwrap();
            socket.set_read_timeout(Some(TEST_TIMEOUT)).unwrap();
            read_request(&mut socket).unwrap();
            socket.write_all(b"HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n\r\n").unwrap();
            let _ = release_rx.recv_timeout(TEST_TIMEOUT);
        });
        let (event_tx, _event_rx) = mpsc::channel();
        let state = Box::new(CallbackState { events: Mutex::new(Vec::new()), event_tx });
        let config = ChatConfig { api_key: String::new(), endpoint, model: "test".into(), system_instruction: String::new() };
        let sink = MahoChatEventSink { on_token: None, on_thinking: None, on_complete: Some(on_complete), on_error: Some(on_error), user_data: (&*state as *const CallbackState).cast_mut().cast() };
        get_runtime().block_on(run_completion_with_idle(config, vec![], vec![], sink.into(), Duration::from_millis(100)));
        let _ = release_tx.send(());
        server.join().unwrap();
        let events = state.events.lock().unwrap().clone();
        assert!(matches!(events.as_slice(), [CallbackEvent::Error(m)] if m.contains("idle")), "{events:?}");
    }

    #[test]
    fn review_cancelled_generation_cannot_emit() {
        let (event_tx, _event_rx) = mpsc::channel();
        let state = Box::new(CallbackState { events: Mutex::new(Vec::new()), event_tx });
        let sink = MahoChatEventSink { on_token: Some(on_token), on_thinking: None, on_complete: Some(on_complete), on_error: Some(on_error), user_data: (&*state as *const CallbackState).cast_mut().cast() };
        let gate = Arc::new(CallbackGate::new());
        let run = RunSink { sink, gate: Arc::clone(&gate), run_id: 0 };
        run.on_token("live");
        gate.invalidate();
        run.on_token("stale");
        run.on_error("stale".into());
        run.on_complete("stale", "");
        assert_eq!(*state.events.lock().unwrap(), vec![CallbackEvent::Token("live".into())]);
    }

    #[test]
    fn review_sink_replacement_retires_the_outgoing_run() {
        let (event_tx, _event_rx) = mpsc::channel();
        let state = Box::new(CallbackState { events: Mutex::new(Vec::new()), event_tx });
        let sink = MahoChatEventSink { on_token: Some(on_token), on_thinking: None, on_complete: Some(on_complete), on_error: Some(on_error), user_data: (&*state as *const CallbackState).cast_mut().cast() };
        let mut session = ChatSession::new(ChatConfig { api_key: String::new(), endpoint: String::new(), model: String::new(), system_instruction: String::new() });
        session.set_event_sink(sink.clone());
        let run = RunSink { sink: sink.clone(), gate: Arc::clone(&session.gate), run_id: *session.gate.lock() };
        run.on_token("live");
        session.set_event_sink(sink);
        run.on_token("stale");
        drop(session);
        run.on_complete("stale", "");
        assert_eq!(*state.events.lock().unwrap(), vec![CallbackEvent::Token("live".into())]);
    }

    /// Callback context that parks inside `on_token` until released, recording
    /// whether the callback is still executing.
    struct BlockingState {
        entered_tx: mpsc::Sender<()>,
        release_rx: Mutex<mpsc::Receiver<()>>,
        exited: std::sync::atomic::AtomicBool,
    }

    unsafe extern "C" fn blocking_on_token(user_data: *mut c_void, _token: *const c_char) {
        let state = &*(user_data.cast::<BlockingState>());
        let _ = state.entered_tx.send(());
        let _ = state.release_rx.lock().unwrap().recv_timeout(TEST_TIMEOUT);
        state.exited.store(true, std::sync::atomic::Ordering::SeqCst);
    }

    #[test]
    fn review_cancel_waits_for_in_flight_callback() {
        let server = TestServer::spawn();
        let (entered_tx, entered_rx) = mpsc::channel();
        let (release_tx, release_rx) = mpsc::channel();
        let state = Arc::new(BlockingState { entered_tx, release_rx: Mutex::new(release_rx), exited: std::sync::atomic::AtomicBool::new(false) });
        let mut session = ChatSession::new(ChatConfig { api_key: String::new(), endpoint: server.endpoint(), model: "test".into(), system_instruction: String::new() });
        session.set_event_sink(MahoChatEventSink { on_token: Some(blocking_on_token), on_thinking: None, on_complete: None, on_error: None, user_data: Arc::as_ptr(&state).cast_mut().cast() });
        assert!(session.send_user_turn("hi"));
        let _request = server.captured_request();
        entered_rx.recv_timeout(TEST_TIMEOUT).expect("callback entered");

        // The in-flight callback holds the gate, so invalidation cannot pass it.
        assert!(matches!(session.gate.generation.try_lock(), Err(std::sync::TryLockError::WouldBlock)));

        let observer = Arc::clone(&state);
        let canceller = thread::spawn(move || {
            session.cancel();
            // Freeing the context is safe only if the callback already returned.
            let exited = observer.exited.load(std::sync::atomic::Ordering::SeqCst);
            drop(session);
            exited
        });
        release_tx.send(()).expect("release callback");
        assert!(canceller.join().expect("canceller did not panic"), "cancel returned while a callback was executing");
    }
}
