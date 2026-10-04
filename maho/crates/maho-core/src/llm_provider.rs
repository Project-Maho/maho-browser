use base64::{engine::general_purpose::STANDARD as BASE64, Engine as _};
use futures_util::{Stream, StreamExt};
use maho_types::chat::{ChatContent, ChatContentPart};
use maho_types::llm::{LLMStreamChunk, ModelInfo};
use serde_json::Value;
use std::pin::Pin;

use crate::stream_utils;

pub trait LLMProvider: Send + Sync {
    fn complete(
        &self,
        model: &str,
        messages: Vec<Value>,
        tools: Vec<Value>,
    ) -> Pin<Box<dyn Stream<Item = Result<LLMStreamChunk, String>> + Send>>;

    fn models(&self) -> Vec<ModelInfo>;
    fn supports_vision(&self) -> bool;
    fn supports_tools(&self) -> bool;
}

/// # Threading
/// Async — callers at FFI boundaries must use their own `block_on` bridge.
pub async fn validate_api_key(provider: &str, api_key: &str) -> bool {
    if api_key.trim().is_empty() || api_key.contains("PLACEHOLDER") || api_key.len() < 8 {
        return false;
    }

    let client = crate::http::shared_http_client();

    let req = if provider == "anthropic" {
        client
            .post("https://api.anthropic.com/v1/messages")
            .header("x-api-key", api_key)
            .header("anthropic-version", "2023-06-01")
            .json(&serde_json::json!({
                "model": "claude-sonnet-4-20250514",
                "max_tokens": 1,
                "messages": [{"role": "user", "content": "Ping"}]
            }))
    } else {
        client
            .get("https://api.openai.com/v1/models")
            .header("Authorization", format!("Bearer {}", api_key))
    };

    match req.send().await {
        Ok(res) => {
            let status = res.status().as_u16();
            status != 401 && status != 403
        }
        Err(_) => false,
    }
}

pub struct OpenAICompatibleProvider {
    pub endpoint: String,
    pub api_key: String,
}

impl OpenAICompatibleProvider {
    pub fn new(endpoint: String, api_key: String) -> Self {
        Self { endpoint, api_key }
    }
}

impl LLMProvider for OpenAICompatibleProvider {
    fn complete(
        &self,
        model: &str,
        messages: Vec<Value>,
        tools: Vec<Value>,
    ) -> Pin<Box<dyn Stream<Item = Result<LLMStreamChunk, String>> + Send>> {
        let (tx, rx) = tokio::sync::mpsc::channel(256);
        let client = crate::http::shared_http_client();

        let endpoint_url = if self.endpoint.ends_with("/chat/completions") {
            self.endpoint.clone()
        } else {
            format!("{}/chat/completions", self.endpoint.trim_end_matches('/'))
        };

        let mut req = client
            .post(&endpoint_url)
            .header("Content-Type", "application/json");

        if !self.api_key.is_empty() {
            req = req.header("Authorization", format!("Bearer {}", self.api_key));
        }

        let mut body = serde_json::json!({
            "model": model,
            "messages": messages,
            "stream": true,
        });

        if !tools.is_empty() {
            body.as_object_mut()
                .unwrap()
                .insert("tools".to_string(), Value::Array(tools));
        }

        tokio::spawn(async move {
            let res = match req.json(&body).send().await {
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

                while let Some(pos) = sse_buffer.find("\n\n") {
                    if tx.is_closed() {
                        break;
                    }

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

                        let json: Value = match serde_json::from_str(data) {
                            Ok(val) => val,
                            Err(_) => continue,
                        };

                        if let Some(choices) = json.pointer("/choices").and_then(|v| v.as_array()) {
                            if let Some(choice) = choices.first() {
                                if let Some(delta) = choice.get("delta") {
                                    if let Some(content) =
                                        delta.get("content").and_then(|c| c.as_str())
                                    {
                                        if !content.is_empty() {
                                            if tx
                                                .send(Ok(LLMStreamChunk::Token(
                                                    content.to_string(),
                                                )))
                                                .await
                                                .is_err()
                                            {
                                                return;
                                            }
                                        }
                                    }

                                    if let Some(delta_tool_calls) =
                                        delta.get("tool_calls").and_then(|tc| tc.as_array())
                                    {
                                        for tc in delta_tool_calls {
                                            let index = tc
                                                .get("index")
                                                .and_then(|i| i.as_u64())
                                                .unwrap_or(0)
                                                as usize;
                                            let id = tc
                                                .get("id")
                                                .and_then(|id| id.as_str())
                                                .map(|s| s.to_string());
                                            let name = tc
                                                .get("function")
                                                .and_then(|f| f.get("name"))
                                                .and_then(|n| n.as_str())
                                                .map(|s| s.to_string());
                                            let arguments_delta = tc
                                                .get("function")
                                                .and_then(|f| f.get("arguments"))
                                                .and_then(|a| a.as_str())
                                                .unwrap_or_default()
                                                .to_string();
                                            if tx
                                                .send(Ok(LLMStreamChunk::ToolCallDelta {
                                                    index,
                                                    id,
                                                    name,
                                                    arguments_delta,
                                                }))
                                                .await
                                                .is_err()
                                            {
                                                return;
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
                let _ = tx.send(Err("Incomplete UTF-8 stream".to_string())).await;
            }
        });

        stream_utils::bounded_channel_to_stream(rx)
    }

    fn models(&self) -> Vec<ModelInfo> {
        vec![
            ModelInfo {
                id: "gpt-4o".to_string(),
                name: "GPT-4o".to_string(),
                context_length: Some(128000),
                supports_vision: true,
                supports_tools: true,
            },
            ModelInfo {
                id: "gpt-4-turbo".to_string(),
                name: "GPT-4 Turbo".to_string(),
                context_length: Some(128000),
                supports_vision: true,
                supports_tools: true,
            },
            ModelInfo {
                id: "gpt-3.5-turbo".to_string(),
                name: "GPT-3.5 Turbo".to_string(),
                context_length: Some(16385),
                supports_vision: false,
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

pub fn chat_content_to_openai_value(content: &ChatContent) -> Value {
    match content {
        ChatContent::Text(s) => Value::String(s.clone()),
        ChatContent::Image { mime, data } => {
            let b64 = BASE64.encode(data);
            serde_json::json!([{
                "type": "image_url",
                "image_url": { "url": format!("data:{};base64,{}", mime, b64) }
            }])
        }
        ChatContent::Mixed(parts) => {
            let items: Vec<Value> = parts
                .iter()
                .map(|p| match p {
                    ChatContentPart::Text(s) => serde_json::json!({ "type": "text", "text": s }),
                    ChatContentPart::Image { mime, data } => {
                        let b64 = BASE64.encode(data);
                        serde_json::json!({
                            "type": "image_url",
                            "image_url": { "url": format!("data:{};base64,{}", mime, b64) }
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
    fn test_openai_vision_format() {
        let data = vec![0xFFu8, 0xD8, 0xFF];
        let content = ChatContent::image("image/jpeg", data.clone());
        let val = chat_content_to_openai_value(&content);
        let arr = val.as_array().expect("should be array");
        assert_eq!(arr.len(), 1);
        assert_eq!(arr[0]["type"], "image_url");
        let url = arr[0]["image_url"]["url"].as_str().unwrap();
        let expected_b64 = base64::engine::general_purpose::STANDARD.encode(&data);
        assert_eq!(url, format!("data:image/jpeg;base64,{}", expected_b64));
    }

    #[test]
    fn test_openai_text_unchanged() {
        let content = ChatContent::text("hello");
        let val = chat_content_to_openai_value(&content);
        assert_eq!(val, Value::String("hello".to_string()));
    }

    #[test]
    fn test_openai_mixed_format() {
        let parts = vec![
            ChatContentPart::Text("describe this".to_string()),
            ChatContentPart::Image {
                mime: "image/png".to_string(),
                data: vec![1u8, 2, 3],
            },
        ];
        let content = ChatContent::Mixed(parts);
        let val = chat_content_to_openai_value(&content);
        let arr = val.as_array().expect("should be array");
        assert_eq!(arr.len(), 2);
        assert_eq!(arr[0]["type"], "text");
        assert_eq!(arr[0]["text"], "describe this");
        assert_eq!(arr[1]["type"], "image_url");
        let url = arr[1]["image_url"]["url"].as_str().unwrap();
        assert!(url.starts_with("data:image/png;base64,"));
    }
}
