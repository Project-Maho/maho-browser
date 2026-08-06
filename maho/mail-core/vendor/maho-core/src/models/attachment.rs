use serde::{Deserialize, Serialize};

/// Email attachment metadata.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Attachment {
    pub id: String,
    pub email_id: String,
    pub part_id: String,
    pub filename: Option<String>,
    pub mime_type: String,
    pub size: i64,
    pub content_id: Option<String>,
}
