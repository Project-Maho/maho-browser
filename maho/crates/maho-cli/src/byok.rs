use std::io::{self, Write};

use anyhow::{bail, Result};
use futures_util::StreamExt;
use reqwest::Client;
use serde::Serialize;
use serde_json::Value;

#[derive(Serialize)]
struct ChatRequest {
    model: String,
    messages: Vec<ChatMessage>,
    stream: bool,
}

#[derive(Serialize)]
struct ChatMessage {
    role: String,
    content: String,
}

/// Send a chat message directly to an OpenAI-compatible provider (BYOK mode).
pub async fn send_byok(
    provider: &str,
    api_key: &str,
    message: &str,
) -> Result<()> {
    let base_url = match provider {
        "openai" => "https://api.openai.com/v1",
        other => bail!(
            "unsupported BYOK provider: '{}'. Supported: openai",
            other
        ),
    };

    let client = Client::new();
    let url = format!("{}/chat/completions", base_url);

    let body = ChatRequest {
        model: "gpt-4o".to_string(),
        messages: vec![ChatMessage {
            role: "user".to_string(),
            content: message.to_string(),
        }],
        stream: true,
    };

    let resp = client
        .post(&url)
        .bearer_auth(api_key)
        .json(&body)
        .send()
        .await?;

    if !resp.status().is_success() {
        let status = resp.status();
        let text = resp.text().await.unwrap_or_default();
        bail!("BYOK chat failed ({}): {}", status, text);
    }

    let mut stream = resp.bytes_stream();
    let mut buffer = String::new();
    let stdout = io::stdout();
    let mut out = stdout.lock();

    while let Some(chunk) = stream.next().await {
        let chunk = chunk?;
        let text = String::from_utf8_lossy(&chunk);
        buffer.push_str(&text);

        while let Some(pos) = buffer.find('\n') {
            let line = buffer[..pos].to_string();
            buffer = buffer[pos + 1..].to_string();

            let line = line.trim();
            if line.is_empty() {
                continue;
            }

            if let Some(data) = line.strip_prefix("data: ") {
                if data.trim() == "[DONE]" {
                    writeln!(out)?;
                    return Ok(());
                }

                if let Ok(parsed) = serde_json::from_str::<Value>(data) {
                    if let Some(content) = parsed["choices"][0]["delta"]["content"].as_str() {
                        write!(out, "{}", content)?;
                        out.flush()?;
                    }
                }
            }
        }
    }

    writeln!(out)?;
    Ok(())
}
