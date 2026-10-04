use base64::{engine::general_purpose::STANDARD as BASE64, Engine as _};
use futures_util::{Stream, StreamExt};
use maho_types::chat::{ChatContent, ChatContentPart};
use maho_types::llm::{LLMStreamChunk, ModelInfo};
use serde_json::Value;
use std::pin::Pin;

use crate::llm_provider::LLMProvider;
use crate::stream_utils;

pub struct AnthropicProvider {
    pub endpoint: String,
    pub api_key: String,
}

impl AnthropicProvider {
    pub fn new(endpoint: String, api_key: String) -> Self {
        Self { endpoint, api_key }
    }
}

impl LLMProvider for AnthropicProvider {
    fn complete(
        &self,
        model: &str,
        messages: Vec<Value>,
        tools: Vec<Value>,
    ) -> Pin<Box<dyn Stream<Item = Result<LLMStreamChunk, String>> + Send>> {
        let (tx, rx) = tokio::sync::mpsc::channel(256);
        let client = crate::http::shared_http_client();

        let endpoint_url = format!("{}/messages", self.endpoint.trim_end_matches('/'));

        let mut req = client
            .post(&endpoint_url)
            .header("Content-Type", "application/json")
            .header("x-api-key", &self.api_key)
            .header("anthropic-version", "2023-06-01");

        // Anthropic uses a `system` top-level field, not a system message in the array.
        // Extract system message if present and separate from user/assistant messages.
        let mut system_text: Option<String> = None;
        let mut api_messages: Vec<Value> = Vec::new();
        for msg in &messages {
            if msg.get("role").and_then(|r| r.as_str()) == Some("system") {
                if let Some(content) = msg.get("content").and_then(|c| c.as_str()) {
                    system_text = Some(content.to_string());
                }
            } else {
                api_messages.push(msg.clone());
            }
        }

        let mut body = serde_json::json!({
            "model": model,
            "messages": api_messages,
            "stream": true,
            "max_tokens": 4096,
        });

        if let Some(sys) = system_text {
            body.as_object_mut()
                .unwrap()
                .insert("system".to_string(), Value::String(sys));
        }

        if !tools.is_empty() {
            body.as_object_mut()
                .unwrap()
                .insert("tools".to_string(), Value::Array(tools));
        }

        req = req.json(&body);

        tokio::spawn(async move {
            let res = match req.send().await {
                Ok(r) => r,
                Err(e) => {
                    let _ = tx.send(Err(format!("Request failed: {}", e))).await;
                    return;
                }
            };

            if !res.status().is_success() {
                let status = res.status();
                let err_text = res.text().await.unwrap_or_default();
                let _ = tx
                    .send(Err(format!("HTTP error {}: {}", status, err_text)))
                    .await;
                return;
            }

            let mut bytes_stream = res.bytes_stream();
            let mut utf8_pending = Vec::new();
            let mut sse_buffer = String::new();

            while let Some(chunk_result) = bytes_stream.next().await {
                if tx.is_closed() {
                    break;
                }

                let chunk = match chunk_result {
                    Ok(b) => b,
                    Err(e) => {
                        let _ = tx.send(Err(format!("Stream error: {}", e))).await;
                        return;
                    }
                };

                let chunk_str = match stream_utils::push_utf8_chunk(&mut utf8_pending, &chunk) {
                    Ok(s) => s,
                    Err(_) => {
                        let _ = tx.send(Err("Invalid UTF-8 chunk".to_string())).await;
                        return;
                    }
                };

                sse_buffer.push_str(&chunk_str);

                // Anthropic SSE format: `event: <type>\ndata: <json>\n\n`
                while let Some(pos) = sse_buffer.find("\n\n") {
                    if tx.is_closed() {
                        break;
                    }

                    let event_block = sse_buffer[..pos].to_string();
                    sse_buffer.drain(..pos + 2);

                    let mut event_type: Option<&str> = None;
                    let mut data_line: Option<&str> = None;

                    for line in event_block.lines() {
                        if let Some(rest) = line.strip_prefix("event:") {
                            event_type = Some(rest.trim());
                        } else if let Some(rest) = line.strip_prefix("data:") {
                            data_line = Some(rest.trim());
                        }
                    }

                    let data = match data_line {
                        Some(d) => d,
                        None => continue,
                    };

                    let json: Value = match serde_json::from_str(data) {
                        Ok(val) => val,
                        Err(_) => continue,
                    };

                    match event_type {
                        Some("content_block_delta") => {
                            if let Some(delta) = json.get("delta") {
                                let delta_type = delta.get("type").and_then(|t| t.as_str());
                                match delta_type {
                                    Some("text_delta") => {
                                        if let Some(text) =
                                            delta.get("text").and_then(|t| t.as_str())
                                        {
                                            if !text.is_empty() {
                                                if tx
                                                    .send(Ok(LLMStreamChunk::Token(
                                                        text.to_string(),
                                                    )))
                                                    .await
                                                    .is_err()
                                                {
                                                    return;
                                                }
                                            }
                                        }
                                    }
                                    Some("input_json_delta") => {
                                        let index =
                                            json.get("index").and_then(|i| i.as_u64()).unwrap_or(0)
                                                as usize;
                                        let partial_json = delta
                                            .get("partial_json")
                                            .and_then(|p| p.as_str())
                                            .unwrap_or_default()
                                            .to_string();
                                        if tx
                                            .send(Ok(LLMStreamChunk::ToolCallDelta {
                                                index,
                                                id: None,
                                                name: None,
                                                arguments_delta: partial_json,
                                            }))
                                            .await
                                            .is_err()
                                        {
                                            return;
                                        }
                                    }
                                    _ => {}
                                }
                            }
                        }
                        Some("content_block_start") => {
                            if let Some(content_block) = json.get("content_block") {
                                if content_block.get("type").and_then(|t| t.as_str())
                                    == Some("tool_use")
                                {
                                    let index =
                                        json.get("index").and_then(|i| i.as_u64()).unwrap_or(0)
                                            as usize;
                                    let id = content_block
                                        .get("id")
                                        .and_then(|i| i.as_str())
                                        .map(|s| s.to_string());
                                    let name = content_block
                                        .get("name")
                                        .and_then(|n| n.as_str())
                                        .map(|s| s.to_string());
                                    if tx
                                        .send(Ok(LLMStreamChunk::ToolCallDelta {
                                            index,
                                            id,
                                            name,
                                            arguments_delta: String::new(),
                                        }))
                                        .await
                                        .is_err()
                                    {
                                        return;
                                    }
                                }
                            }
                        }
                        Some("error") => {
                            let err_msg = json
                                .get("error")
                                .and_then(|e| e.get("message"))
                                .and_then(|m| m.as_str())
                                .unwrap_or("Unknown Anthropic error");
                            let _ = tx.send(Err(err_msg.to_string())).await;
                            return;
                        }
                        Some("message_stop") => {
                            return;
                        }
                        _ => {}
                    }
                }
            }
            if !utf8_pending.is_empty() {
                let _ = tx.send(Err("Incomplete UTF-8 stream".to_string())).await;
            }
        });

        stream_utils::bounded_channel_to_stream(rx)
    }

    fn models(&self) -> Vec<ModelInfo> {
        vec![
            ModelInfo {
                id: "claude-sonnet-4-20250514".to_string(),
                name: "Claude Sonnet 4".to_string(),
                context_length: Some(200000),
                supports_vision: true,
                supports_tools: true,
            },
            ModelInfo {
                id: "claude-opus-4-20250514".to_string(),
                name: "Claude Opus 4".to_string(),
                context_length: Some(200000),
                supports_vision: true,
                supports_tools: true,
            },
            ModelInfo {
                id: "claude-3-5-haiku-20241022".to_string(),
                name: "Claude 3.5 Haiku".to_string(),
                context_length: Some(200000),
                supports_vision: true,
                supports_tools: true,
            },
        ]
    }

    fn supports_vision(&self) -> bool {
        true
    }

    fn supports_tools(&self) -> bool {
        true
    }
}

pub fn chat_content_to_anthropic_value(content: &ChatContent) -> Value {
    match content {
        ChatContent::Text(s) => Value::String(s.clone()),
        ChatContent::Image { mime, data } => {
            let b64 = BASE64.encode(data);
            Value::Array(vec![serde_json::json!({
                "type": "image",
                "source": {
                    "type": "base64",
                    "media_type": mime,
                    "data": b64
                }
            })])
        }
        ChatContent::Mixed(parts) => {
            let items: Vec<Value> = parts
                .iter()
                .map(|p| match p {
                    ChatContentPart::Text(s) => serde_json::json!({ "type": "text", "text": s }),
                    ChatContentPart::Image { mime, data } => {
                        let b64 = BASE64.encode(data);
                        serde_json::json!({
                            "type": "image",
                            "source": {
                                "type": "base64",
                                "media_type": mime,
                                "data": b64
                            }
                        })
                    }
                })
                .collect();
            Value::Array(items)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use maho_types::chat::{ChatContent, ChatContentPart};

    #[test]
    fn test_anthropic_vision_format() {
        let data = vec![0xFFu8, 0xD8, 0xFF];
        let content = ChatContent::image("image/jpeg", data.clone());
        let val = chat_content_to_anthropic_value(&content);
        let arr = val.as_array().expect("should be array");
        assert_eq!(arr.len(), 1);
        assert_eq!(arr[0]["type"], "image");
        assert_eq!(arr[0]["source"]["type"], "base64");
        assert_eq!(arr[0]["source"]["media_type"], "image/jpeg");
        let expected_b64 = base64::engine::general_purpose::STANDARD.encode(&data);
        assert_eq!(arr[0]["source"]["data"], expected_b64);
    }

    #[test]
    fn test_anthropic_text_unchanged() {
        let content = ChatContent::text("hello");
        let val = chat_content_to_anthropic_value(&content);
        assert_eq!(val, Value::String("hello".to_string()));
    }

    #[test]
    fn test_anthropic_mixed_format() {
        let parts = vec![
            ChatContentPart::Text("what is this?".to_string()),
            ChatContentPart::Image {
                mime: "image/png".to_string(),
                data: vec![1u8, 2, 3],
            },
        ];
        let content = ChatContent::Mixed(parts);
        let val = chat_content_to_anthropic_value(&content);
        let arr = val.as_array().expect("should be array");
        assert_eq!(arr.len(), 2);
        assert_eq!(arr[0]["type"], "text");
        assert_eq!(arr[0]["text"], "what is this?");
        assert_eq!(arr[1]["type"], "image");
        assert_eq!(arr[1]["source"]["media_type"], "image/png");
    }
}
