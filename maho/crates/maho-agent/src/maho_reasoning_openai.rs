use anyhow::Result;
use async_trait::async_trait;
use futures::StreamExt;
use reqwest::header::{AUTHORIZATION, CONTENT_TYPE};
use std::sync::Arc;
use swiftide_core::chat_completion::{
    errors::LanguageModelError, ChatCompletion, ChatCompletionRequest, ChatCompletionResponse,
    ChatCompletionResponseDelta, ChatCompletionStream, ToolCall,
};

mod sse;
mod wire;

use sse::stream_sse_bytes;
use wire::{map_message, map_tool, ChatCompletionResponseJson, OpenAiChatRequest};

#[derive(Clone, Debug)]
pub struct MahoReasoningOpenAI {
    api_key: Arc<secrecy::SecretString>,
    api_base: String,
    model: String,
    client: reqwest::Client,
}

impl MahoReasoningOpenAI {
    pub fn new(api_key: secrecy::SecretString, api_base: String, model: String) -> Self {
        Self {
            api_key: Arc::new(api_key),
            api_base,
            model,
            client: reqwest::Client::new(),
        }
    }

    fn request_payload(&self, request: &ChatCompletionRequest, stream: bool) -> OpenAiChatRequest {
        OpenAiChatRequest {
            model: self.model.clone(),
            messages: request.messages.iter().map(map_message).collect(),
            tools: if request.tools_spec.is_empty() {
                None
            } else {
                Some(request.tools_spec.iter().map(map_tool).collect())
            },
            stream,
        }
    }

    fn completions_url(&self) -> String {
        format!("{}/chat/completions", self.api_base.trim_end_matches('/'))
    }
}

/// Renders a failed completion response into a message that names the real
/// cause. Providers answer a rejected request with an error envelope instead of
/// `choices`; decoding that as a completion reported "missing field `choices`"
/// and buried the actual reason (for example `context_too_large`).
fn describe_completion_failure(status: reqwest::StatusCode, body: &str) -> String {
    #[derive(serde::Deserialize)]
    struct ErrorEnvelope {
        error: ProviderError,
    }

    #[derive(serde::Deserialize)]
    struct ProviderError {
        message: Option<String>,
        code: Option<String>,
        #[serde(rename = "type")]
        kind: Option<String>,
    }

    if let Ok(envelope) = serde_json::from_str::<ErrorEnvelope>(body) {
        let mut parts = Vec::new();
        if let Some(code) = envelope.error.code.as_deref().filter(|c| !c.is_empty()) {
            parts.push(code.to_string());
        }
        if let Some(kind) = envelope.error.kind.as_deref().filter(|k| !k.is_empty()) {
            parts.push(kind.to_string());
        }
        let detail = envelope
            .error
            .message
            .as_deref()
            .filter(|m| !m.is_empty())
            .unwrap_or("provider reported an error without a message");
        if parts.is_empty() {
            return format!("LLM request failed ({status}): {detail}");
        }
        return format!(
            "LLM request failed ({status}) [{}]: {detail}",
            parts.join(" ")
        );
    }

    let trimmed = body.trim();
    let snippet = if trimmed.len() > 500 {
        let mut end = 500;
        while end > 0 && !trimmed.is_char_boundary(end) {
            end -= 1;
        }
        format!("{}…", &trimmed[..end])
    } else {
        trimmed.to_string()
    };
    if snippet.is_empty() {
        format!("LLM request failed ({status}) with an empty body")
    } else {
        format!("LLM request failed ({status}): {snippet}")
    }
}

#[async_trait]
impl ChatCompletion for MahoReasoningOpenAI {
    async fn complete(
        &self,
        request: &ChatCompletionRequest,
    ) -> Result<ChatCompletionResponse, LanguageModelError> {
        use secrecy::ExposeSecret;

        let response = self
            .client
            .post(self.completions_url())
            .header(CONTENT_TYPE, "application/json")
            .header(
                AUTHORIZATION,
                format!("Bearer {}", self.api_key.expose_secret()),
            )
            .json(&self.request_payload(request, false))
            .send()
            .await
            .map_err(LanguageModelError::permanent)?;
        // A rejected request answers with an error envelope, not `choices`, so
        // decode the body once and report the provider's own reason instead of
        // a serde "missing field" error.
        let status = response.status();
        let body = response
            .text()
            .await
            .map_err(LanguageModelError::permanent)?;
        let json = match serde_json::from_str::<ChatCompletionResponseJson>(&body) {
            Ok(json) => json,
            Err(parse_error) => {
                return Err(LanguageModelError::permanent(anyhow::anyhow!(
                    "{} (parse error: {parse_error})",
                    describe_completion_failure(status, &body)
                )));
            }
        };
        let choice = json.choices.first().ok_or_else(|| {
            LanguageModelError::permanent(anyhow::anyhow!("Empty choices in completion response"))
        })?;

        let mut tool_calls = Vec::new();
        if let Some(calls) = &choice.message.tool_calls {
            for call in calls {
                tool_calls.push(
                    ToolCall::builder()
                        .id(call.id.clone())
                        .name(call.function.name.clone())
                        .args(call.function.arguments.clone())
                        .build()
                        .map_err(|error| {
                            LanguageModelError::permanent(anyhow::anyhow!(
                                "Failed to build ToolCall: {error}"
                            ))
                        })?,
                );
            }
        }

        let reasoning = choice
            .message
            .reasoning_content
            .as_deref()
            .or(choice.message.reasoning.as_deref());
        Ok(ChatCompletionResponse {
            message: choice.message.content.clone(),
            tool_calls: (!tool_calls.is_empty()).then_some(tool_calls),
            delta: reasoning.map(|reasoning| ChatCompletionResponseDelta {
                message_chunk: None,
                reasoning_chunk: Some(reasoning.to_string()),
                tool_calls_chunk: None,
            }),
            ..ChatCompletionResponse::default()
        })
    }

    async fn complete_stream(&self, request: &ChatCompletionRequest) -> ChatCompletionStream {
        use secrecy::ExposeSecret;

        let response = match self
            .client
            .post(self.completions_url())
            .header(CONTENT_TYPE, "application/json")
            .header(
                AUTHORIZATION,
                format!("Bearer {}", self.api_key.expose_secret()),
            )
            .json(&self.request_payload(request, true))
            .send()
            .await
        {
            Ok(response) => response,
            Err(error) => {
                return Box::pin(futures::stream::once(async move {
                    Err(LanguageModelError::permanent(error))
                }));
            }
        };

        if !response.status().is_success() {
            let status = response.status();
            let body = response.text().await.unwrap_or_default();
            let message = describe_completion_failure(status, &body);
            return Box::pin(futures::stream::once(async move {
                Err(LanguageModelError::permanent(anyhow::anyhow!(message)))
            }));
        }

        let byte_stream = response.bytes_stream().map(|result| {
            result
                .map(|bytes| bytes.to_vec())
                .map_err(LanguageModelError::permanent)
        });
        stream_sse_bytes(Box::pin(byte_stream))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A provider that rejects the request answers with an error envelope, not
    /// with `choices`. Decoding it as a completion produced
    /// "error decoding response body ... missing field `choices`", which hid the
    /// real cause (`context_too_large`) from the user and from the logs.
    #[test]
    fn completion_error_envelope_surfaces_provider_message() {
        let body = r#"{"error":{"message":"Your input exceeds the context window of this model. Please adjust your input and try again.","type":"invalid_request_error","code":"context_too_large"}}"#;

        let message = describe_completion_failure(reqwest::StatusCode::BAD_REQUEST, body);

        assert!(
            message.contains("context_too_large"),
            "provider error code must survive, got: {message}"
        );
        assert!(
            message.contains("exceeds the context window"),
            "provider explanation must survive, got: {message}"
        );
        assert!(
            !message.contains("missing field"),
            "serde noise must not replace the real cause, got: {message}"
        );
    }

    /// A non-JSON gateway failure still has to name itself rather than collapse
    /// into a decode error.
    #[test]
    fn completion_failure_without_json_envelope_keeps_status_and_body() {
        let message =
            describe_completion_failure(reqwest::StatusCode::BAD_GATEWAY, "upstream timeout");

        assert!(
            message.contains("502"),
            "status must survive, got: {message}"
        );
        assert!(
            message.contains("upstream timeout"),
            "body must survive, got: {message}"
        );
    }

    #[test]
    fn completion_failure_truncates_unicode_on_a_character_boundary() {
        let body = "한".repeat(200);

        let message = describe_completion_failure(reqwest::StatusCode::BAD_GATEWAY, &body);

        assert!(message.contains("502"));
        assert!(message.ends_with('…'));
    }
}
