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
}

impl ChatRequestContext {
    pub fn new(request_mode: ChatRequestMode) -> Self {
        Self {
            request_mode,
            page_context: None,
            tools: None,
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
