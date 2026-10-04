use serde::{Deserialize, Serialize};

/// Supported AI providers.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum AiProvider {
    OpenAi,
    Anthropic,
    Ollama,
    OpenRouter,
}

/// AI provider configuration.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AiConfig {
    #[serde(default = "default_feature")]
    pub feature: String,
    pub provider: AiProvider,
    pub model: String,
    pub api_key: Option<String>,
    pub base_url: Option<String>,
    /// Use the browser's BYOK key at call time instead of mail's stored key.
    #[serde(default)]
    pub use_browser_key: bool,
}

fn default_feature() -> String {
    "default".to_string()
}

/// Request to summarize one or more emails.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SummaryRequest {
    pub email_ids: Vec<String>,
}

/// AI-generated email summary.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SummaryResponse {
    pub summary: String,
}

/// Request to generate a reply draft.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ReplyDraftRequest {
    pub email_id: String,
    pub instructions: Option<String>,
}

/// AI-generated reply draft.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ReplyDraftResponse {
    pub draft: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ToneRequest {
    pub text: String,
    pub tone: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ToneResponse {
    pub adjusted_text: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ClassifyRequest {
    pub email_id: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ClassifyResponse {
    pub category: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct NlSearchRequest {
    pub query: String,
    pub account_id: Option<String>,
    pub folder_id: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TestAiConnectionResult {
    pub success: bool,
    pub message: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub detail: Option<String>,
}
