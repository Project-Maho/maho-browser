use serde::{Deserialize, Serialize};

use super::email::EmailSummary;

/// Query parameters for full-text email search.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct SearchQuery {
    pub query: String,
    pub account_id: Option<String>,
    pub folder_id: Option<String>,
    /// Mailbox name/path/type from `in:`, distinct from an explicit folder ID.
    pub mailbox: Option<String>,
    pub limit: Option<i64>,
    pub offset: Option<i64>,
    // Structured filters — applied as SQL WHERE, NOT passed to FTS
    pub from: Option<String>,
    pub to: Option<String>,
    pub subject: Option<String>,
    pub has_attachment: Option<bool>,
    pub is_unread: Option<bool>,
    pub is_starred: Option<bool>,
    pub date_from: Option<String>,
    pub date_to: Option<String>,
}

/// Search results with pagination info.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SearchResult {
    pub emails: Vec<EmailSummary>,
    pub total_count: i64,
    pub query: String,
}
