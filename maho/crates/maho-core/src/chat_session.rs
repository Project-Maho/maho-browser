// Copyright 2026 Maho Browser. All rights reserved.

use std::ffi::{c_char, c_void, CString};
use std::sync::OnceLock;
use tokio::runtime::Runtime;
use tokio::task::JoinHandle;
use futures_util::StreamExt;

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
    pub on_complete: Option<unsafe extern "C" fn(user_data: *mut c_void, full_text: *const c_char, tool_calls_json: *const c_char)>,
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

    pub fn on_complete(&self, full_text: &str, tool_calls_json: &str) {
        if let Some(cb) = self.on_complete {
            let c_full_text = CString::new(full_text).unwrap_or_default();
            let c_tool_calls_json = CString::new(tool_calls_json).unwrap_or_default();
            unsafe {
                cb(self.user_data, c_full_text.as_ptr(), c_tool_calls_json.as_ptr());
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

pub struct ChatSession {
    config: ChatConfig,
    tools: Vec<ToolDescriptor>,
    history: Vec<serde_json::Value>,
    active_task: Option<JoinHandle<()>>,
    event_sink: Option<MahoChatEventSink>,
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
        self.event_sink = Some(sink);
    }

    pub fn send_user_turn(&mut self, message: &str) -> bool {
        self.history.push(serde_json::json!({
            "role": "user",
            "content": message
        }));
        self.trigger_completion()
    }

    pub fn append_user_message(&mut self, content: &str) {
        self.history.push(serde_json::json!({
            "role": "user",
            "content": content
        }));
    }


    pub fn append_assistant_message(&mut self, content: &str, tool_calls_json: &str) {
        let mut msg = serde_json::json!({
            "role": "assistant"
        });
        if !content.is_empty() {
            msg.as_object_mut().unwrap().insert("content".to_string(), serde_json::Value::String(content.to_string()));
        }
        if !tool_calls_json.is_empty() {
            if let Ok(serde_json::Value::Array(tcs)) = serde_json::from_str(tool_calls_json) {
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
                msg.as_object_mut().unwrap().insert("tool_calls".to_string(), serde_json::Value::Array(mapped_tcs));
            }
        }
        self.history.push(msg);
    }

    pub fn send_tool_result(&mut self, tool_call_id: &str, tool_name: &str, result: &str, trigger: bool) -> bool {
        self.history.push(serde_json::json!({
            "role": "tool",
            "tool_call_id": tool_call_id,
            "name": tool_name,
            "content": result
        }));
        if trigger {
            self.trigger_completion()
        } else {
            true
        }
    }

    fn abort_and_join(&mut self) {
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
        
        let tools: Vec<serde_json::Value> = self.tools.iter().map(|tool| {
            serde_json::json!({
                "type": "function",
                "function": {
                    "name": tool.name,
                    "description": tool.description,
                    "parameters": tool.parameters_schema
                }
            })
        }).collect();

        let runtime = get_runtime();
        let handle = runtime.spawn(async move {
            run_completion(config, messages, tools, sink).await;
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

async fn run_completion(
    config: ChatConfig,
    messages: Vec<serde_json::Value>,
    tools: Vec<serde_json::Value>,
    sink: MahoChatEventSink,
) {
    let client = crate::http::shared_http_client();
    let mut req = client.post(&config.endpoint)
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
        body.as_object_mut().unwrap().insert("tools".to_string(), serde_json::Value::Array(tools));
    }

    let res = match req.json(&body).send().await {
        Ok(r) => r,
        Err(e) => {
            sink.on_error(format!("Request failed: {}", e));
            return;
        }
    };

    if !res.status().is_success() {
        let status = res.status();
        let err_text = res.text().await.unwrap_or_default();
        sink.on_error(format!("HTTP error {}: {}", status, err_text));
        return;
    }

    let mut stream = res.bytes_stream();
    let mut sse_buffer = String::new();
    let mut full_text = String::new();
    let mut tool_calls = Vec::<serde_json::Value>::new();

    while let Some(chunk_result) = stream.next().await {
        let chunk = match chunk_result {
            Ok(bytes) => bytes,
            Err(e) => {
                sink.on_error(format!("Stream error: {}", e));
                return;
            }
        };

        let chunk_str = match std::str::from_utf8(&chunk) {
            Ok(s) => s,
            Err(_) => {
                sink.on_error("Invalid UTF-8 chunk".to_string());
                return;
            }
        };

        sse_buffer.push_str(chunk_str);

        while let Some(pos) = sse_buffer.find("\n\n") {
            let event_block = sse_buffer[..pos].to_string();
            sse_buffer.drain(..pos + 2);

            for line in event_block.lines() {
                if !line.starts_with("data:") {
                    continue;
                }
                let data = line["data:".len()..].trim();
                if data == "[DONE]" {
                    break;
                }

                let json: serde_json::Value = match serde_json::from_str(data) {
                    Ok(val) => val,
                    Err(_) => continue,
                };

                if let Some(choices) = json.pointer("/choices").and_then(|v| v.as_array()) {
                    if let Some(choice) = choices.first() {
                        if let Some(delta) = choice.get("delta") {
                            if let Some(content) = delta.get("content").and_then(|c| c.as_str()) {
                                if !content.is_empty() {
                                    full_text.push_str(content);
                                    sink.on_token(content);
                                }
                            }

                            if let Some(delta_tool_calls) = delta.get("tool_calls").and_then(|tc| tc.as_array()) {
                                for tc in delta_tool_calls {
                                    let index = tc.get("index").and_then(|i| i.as_u64()).unwrap_or(0) as usize;
                                    if tool_calls.len() <= index {
                                        tool_calls.resize(index + 1, serde_json::json!({
                                            "index": index,
                                            "id": "",
                                            "name": "",
                                            "arguments": ""
                                        }));
                                    }

                                    let tc_obj = tool_calls[index].as_object_mut().unwrap();
                                    if let Some(id) = tc.get("id").and_then(|id| id.as_str()) {
                                        tc_obj.insert("id".to_string(), serde_json::Value::String(id.to_string()));
                                    }
                                    if let Some(func) = tc.get("function") {
                                        if let Some(name) = func.get("name").and_then(|n| n.as_str()) {
                                            tc_obj.insert("name".to_string(), serde_json::Value::String(name.to_string()));
                                        }
                                        if let Some(args) = func.get("arguments").and_then(|a| a.as_str()) {
                                            let current_args = tc_obj.get_mut("arguments").unwrap();
                                            let mut new_args = current_args.as_str().unwrap_or_default().to_string();
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

    let tool_calls_json = if !tool_calls.is_empty() {
        let mapped_tool_calls: Vec<serde_json::Value> = tool_calls.into_iter().map(|tc| {
            let tc_obj = tc.as_object().unwrap();
            serde_json::json!({
                "index": tc_obj.get("index").unwrap(),
                "id": tc_obj.get("id").unwrap(),
                "name": tc_obj.get("name").unwrap(),
                "arguments_json": tc_obj.get("arguments").unwrap()
            })
        }).collect();
        serde_json::to_string(&mapped_tool_calls).unwrap_or_default()
    } else {
        String::new()
    };

    sink.on_complete(&full_text, &tool_calls_json);
}
