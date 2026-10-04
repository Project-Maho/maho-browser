use serde::{Deserialize, Serialize};

use crate::common::Url;

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ChatRequestMode {
    GeneralChat,
    PageQuestion,
    PageTransformation,
    BrowserAction,
    ToolAssistedTask,
    AiSearch,
}

impl Default for ChatRequestMode {
    fn default() -> Self {
        Self::GeneralChat
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum PageExtractionStatus {
    NotRequested,
    Pending,
    Partial,
    Complete,
    Failed,
}

impl Default for PageExtractionStatus {
    fn default() -> Self {
        Self::NotRequested
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PageLink {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub text: Option<String>,
    pub url: Url,
}

impl PageLink {
    pub fn new(url: Url) -> Self {
        Self { text: None, url }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PageContext {
    pub title: String,
    pub url: Url,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub selected_text: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub main_text: Option<String>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub headings: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub meta_description: Option<String>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub links: Vec<PageLink>,
    #[serde(default)]
    pub extraction_status: PageExtractionStatus,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub extraction_warnings: Vec<String>,
}

impl PageContext {
    pub fn new(title: String, url: Url) -> Self {
        Self {
            title,
            url,
            selected_text: None,
            main_text: None,
            headings: Vec::new(),
            meta_description: None,
            links: Vec::new(),
            extraction_status: PageExtractionStatus::NotRequested,
            extraction_warnings: Vec::new(),
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ChatRequestContext {
    pub request_mode: ChatRequestMode,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub page_context: Option<PageContext>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tools: Option<Vec<crate::mcp::ToolDescriptor>>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub session_id: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub page_url: Option<String>,
}

impl ChatRequestContext {
    pub fn new(request_mode: ChatRequestMode) -> Self {
        Self {
            request_mode,
            page_context: None,
            tools: None,
            session_id: None,
            page_url: None,
        }
    }

    pub fn with_page_context(mut self, page_context: PageContext) -> Self {
        self.page_context = Some(page_context);
        self
    }

    pub fn with_tools(mut self, tools: Vec<crate::mcp::ToolDescriptor>) -> Self {
        self.tools = Some(tools);
        self
    }
}

impl Default for ChatRequestContext {
    fn default() -> Self {
        Self::new(ChatRequestMode::GeneralChat)
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Conversation {
    pub id: String,
    pub title: Option<String>,
    pub space_id: Option<String>,
    pub model: Option<String>,
    pub created_at: String,
    pub updated_at: String,
    #[serde(default)]
    pub archived_at: Option<String>,
    #[serde(default)]
    pub project_id: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct ConversationProject {
    pub id: String,
    pub name: String,
    pub created_at: String,
    pub updated_at: String,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum ConversationListState {
    Active,
    Archived,
    All,
}

impl Default for ConversationListState {
    fn default() -> Self {
        Self::Active
    }
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum ConversationBulkOperation {
    Archive,
    Unarchive,
    Delete,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct ConversationBulkResult {
    pub requested_count: usize,
    pub affected_ids: Vec<String>,
    pub unchanged_ids: Vec<String>,
    pub missing_ids: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ConversationTurn {
    pub id: String,
    pub session_id: String,
    pub role: String,
    pub content: String,
    pub url_context: Option<String>,
    pub created_at: String,
}

/// A single content part in a mixed message (text or image).
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(tag = "kind", content = "data", rename_all = "snake_case")]
pub enum ChatContentPart {
    Text(String),
    Image { mime: String, data: Vec<u8> },
}

/// The content of a single chat message.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(tag = "type", content = "data", rename_all = "snake_case")]
pub enum ChatContent {
    Text(String),
    Image { mime: String, data: Vec<u8> },
    Mixed(Vec<ChatContentPart>),
}

impl ChatContent {
    /// Convenience constructor for a text message.
    pub fn text(s: impl Into<String>) -> Self {
        Self::Text(s.into())
    }

    /// Convenience constructor for an image message.
    pub fn image(mime: impl Into<String>, data: Vec<u8>) -> Self {
        Self::Image {
            mime: mime.into(),
            data,
        }
    }
}

/// A single message in a chat exchange (user, assistant, system, or tool).
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ChatMessage {
    pub role: String,
    pub content: ChatContent,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tool_calls: Option<serde_json::Value>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tool_call_id: Option<String>,
}

impl ChatMessage {
    pub fn user(content: ChatContent) -> Self {
        Self {
            role: "user".to_string(),
            content,
            tool_calls: None,
            tool_call_id: None,
        }
    }
    pub fn assistant(content: ChatContent) -> Self {
        Self {
            role: "assistant".to_string(),
            content,
            tool_calls: None,
            tool_call_id: None,
        }
    }
    pub fn system(content: ChatContent) -> Self {
        Self {
            role: "system".to_string(),
            content,
            tool_calls: None,
            tool_call_id: None,
        }
    }
}
