//! Direct LLM API client for Memory feature.
//!
//! Bypasses opencode for deterministic JSON extraction with
//! `response_format: json_object`. Reuses user-configured provider/key/model
//! (read at MemoryManager::set_auth time from C++ prefs).

use crate::error::CoreError;
use crate::memory_manager::{AuthSnapshot, RawFact};
use serde::Deserialize;
use serde_json::{json, Value};

const FACT_EXTRACTION_SYSTEM: &str =
    "You extract durable user facts from text. Return ONLY a JSON object with this shape: \
     {\"facts\":[{\"fact\":\"<statement>\",\"category\":\"preference|setting|decision|knowledge\",\"importance\":0.0-1.0}]}. \
     Skip transient or task-specific state. Maximum 5 facts. Empty array if no durable facts.";

const BROWSING_SUMMARY_SYSTEM: &str =
    "You summarize browsing activity into a concise paragraph (≤150 words) describing \
     the user's interests and recent topics. Plain text. No markdown. No headers.";

#[derive(Deserialize)]
struct Choice {
    message: ChatMessage,
}
#[derive(Deserialize)]
struct ChatMessage {
    content: String,
}
#[derive(Deserialize)]
struct ChatCompletionResp {
    choices: Vec<Choice>,
}

#[derive(Deserialize)]
struct FactsEnvelope {
    #[serde(default)]
    facts: Vec<RawFact>,
}

pub async fn extract_facts_json(
    auth: &AuthSnapshot,
    text: &str,
) -> Result<Vec<RawFact>, CoreError> {
    let body = if auth.provider == "anthropic" {
        json!({
            "model": auth.model,
            "messages": [
                {"role": "system", "content": FACT_EXTRACTION_SYSTEM},
                {"role": "user",   "content": format!("Extract facts:\n\n{}", text)}
            ],
            "temperature": 0.1,
            "max_tokens": 512
        })
    } else {
        json!({
            "model": auth.model,
            "messages": [
                {"role": "system", "content": FACT_EXTRACTION_SYSTEM},
                {"role": "user",   "content": format!("Extract facts:\n\n{}", text)}
            ],
            "temperature": 0.1,
            "max_tokens": 512,
            "response_format": { "type": "json_object" }
        })
    };

    let client = crate::http::shared_http_client();

    let content = if auth.provider == "anthropic" {
        let url = format!("{}/messages", auth.base_url.trim_end_matches('/'));
        let resp_text = client
            .post(&url)
            .header("x-api-key", &auth.api_key)
            .header("anthropic-version", "2023-06-01")
            .json(&body)
            .send()
            .await
            .map_err(|e| CoreError::Network(e.to_string()))?
            .error_for_status()
            .map_err(|e| CoreError::Network(e.to_string()))?
            .text()
            .await
            .map_err(|e| CoreError::Network(e.to_string()))?;

        // Anthropic response: {"content":[{"type":"text","text":"..."}], ...}
        let json: Value = serde_json::from_str(&resp_text)
            .map_err(|e| CoreError::Parse(format!("json parse: {e}")))?;
        json.pointer("/content/0/text")
            .and_then(|v| v.as_str())
            .unwrap_or_default()
            .to_string()
    } else {
        let url = format!("{}/chat/completions", auth.base_url.trim_end_matches('/'));
        let mut req = client.post(&url).json(&body);
        if !auth.api_key.is_empty() {
            req = req.bearer_auth(&auth.api_key);
        }
        let resp: ChatCompletionResp = req
            .send()
            .await
            .map_err(|e| CoreError::Network(e.to_string()))?
            .error_for_status()
            .map_err(|e| CoreError::Network(e.to_string()))?
            .json()
            .await
            .map_err(|e| CoreError::Network(e.to_string()))?;
        resp.choices
            .into_iter()
            .next()
            .ok_or_else(|| CoreError::Parse("no choices".into()))?
            .message
            .content
    };

    let cleaned_content = clean_json_codeblock(&content);

    let env: FactsEnvelope = serde_json::from_str(&cleaned_content)
        .map_err(|e| CoreError::Parse(format!("json: {e}; raw: {cleaned_content}")))?;
    Ok(env.facts)
}

fn clean_json_codeblock(s: &str) -> String {
    let s = s.trim();
    if s.starts_with("```json") && s.ends_with("```") {
        s[7..s.len() - 3].to_string()
    } else if s.starts_with("```") && s.ends_with("```") {
        s[3..s.len() - 3].to_string()
    } else {
        s.to_string()
    }
}

pub async fn summarize_browsing(auth: &AuthSnapshot, text: &str) -> Result<String, CoreError> {
    let body = json!({
        "model": auth.model,
        "messages": [
            {"role": "system", "content": BROWSING_SUMMARY_SYSTEM},
            {"role": "user",   "content": format!("Recent activity:\n\n{}", text)}
        ],
        "temperature": 0.2,
        "max_tokens": 256
    });
    let client = crate::http::shared_http_client();

    if auth.provider == "anthropic" {
        let url = format!("{}/messages", auth.base_url.trim_end_matches('/'));
        let resp_text = client
            .post(&url)
            .header("x-api-key", &auth.api_key)
            .header("anthropic-version", "2023-06-01")
            .json(&body)
            .send()
            .await
            .map_err(|e| CoreError::Network(e.to_string()))?
            .error_for_status()
            .map_err(|e| CoreError::Network(e.to_string()))?
            .text()
            .await
            .map_err(|e| CoreError::Network(e.to_string()))?;

        let json: Value = serde_json::from_str(&resp_text)
            .map_err(|e| CoreError::Parse(format!("json parse: {e}")))?;
        Ok(json
            .pointer("/content/0/text")
            .and_then(|v| v.as_str())
            .unwrap_or_default()
            .to_string())
    } else {
        let url = format!("{}/chat/completions", auth.base_url.trim_end_matches('/'));
        let mut req = client.post(&url).json(&body);
        if !auth.api_key.is_empty() {
            req = req.bearer_auth(&auth.api_key);
        }
        let resp: ChatCompletionResp = req
            .send()
            .await
            .map_err(|e| CoreError::Network(e.to_string()))?
            .error_for_status()
            .map_err(|e| CoreError::Network(e.to_string()))?
            .json()
            .await
            .map_err(|e| CoreError::Network(e.to_string()))?;
        Ok(resp
            .choices
            .into_iter()
            .next()
            .ok_or_else(|| CoreError::Parse("no choices".into()))?
            .message
            .content)
    }
}
