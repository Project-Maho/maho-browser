use serde::{Deserialize, Serialize};

use super::attachment::Attachment;

/// Full email record stored in the database.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Email {
    pub id: String,
    pub account_id: String,
    pub folder_id: String,
    pub uid: i64,
    pub message_id: String,
    pub in_reply_to: Option<String>,
    pub subject: String,
    pub from_address: String,
    pub from_name: Option<String>,
    pub to_addresses: String, // JSON array serialized
    pub cc_addresses: Option<String>,
    pub bcc_addresses: Option<String>,
    pub date: String,
    pub snippet: String,
    pub is_read: bool,
    pub is_starred: bool,
    pub is_draft: bool,
    pub has_attachments: bool,
    pub body_text: Option<String>,
    pub body_html: Option<String>,
    pub raw_size: i64,
    pub created_at: String,
    /// Timestamp when body was fetched from server; None if not yet fetched
    pub body_fetched_at: Option<String>,
    /// Serialized compose attachments, including base64 data, for local drafts.
    pub draft_attachments_json: Option<String>,
    pub email_references: Option<String>,
    /// Whether a local draft requests a receipt when sent.
    pub read_receipt: Option<bool>,
    /// Email address requesting a read receipt (Disposition-Notification-To header)
    pub mdn_requested: Option<String>,
}

/// Lightweight email info for list views (no body content).
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EmailSummary {
    pub id: String,
    pub account_id: String,
    pub folder_id: String,
    pub uid: i64,
    pub message_id: String,
    pub subject: String,
    pub from_address: String,
    pub from_name: Option<String>,
    pub date: String,
    pub snippet: String,
    pub is_read: bool,
    pub is_starred: bool,
    pub is_draft: bool,
    pub has_attachments: bool,
}

/// Full email detail including body and attachments (flat — kept for internal use).
#[allow(dead_code)]
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EmailDetail {
    pub id: String,
    pub account_id: String,
    pub folder_id: String,
    pub message_id: String,
    pub in_reply_to: Option<String>,
    pub subject: String,
    pub from_address: String,
    pub from_name: Option<String>,
    pub to_addresses: String,
    pub cc_addresses: Option<String>,
    pub bcc_addresses: Option<String>,
    pub date: String,
    pub snippet: String,
    pub is_read: bool,
    pub is_starred: bool,
    pub is_draft: bool,
    pub has_attachments: bool,
    pub body_text: Option<String>,
    pub body_html: Option<String>,
    pub raw_size: i64,
    pub created_at: String,
    pub attachments: Vec<Attachment>,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_email_summary_serialization_round_trip() {
        let summary = EmailSummary {
            id: "em1".to_string(),
            account_id: "acc1".to_string(),
            folder_id: "fold1".to_string(),
            uid: 42,
            message_id: "<test@example.com>".to_string(),
            subject: "Hello".to_string(),
            from_address: "sender@example.com".to_string(),
            from_name: Some("Sender".to_string()),
            date: "2024-01-01T00:00:00Z".to_string(),
            snippet: "Snippet".to_string(),
            is_read: false,
            is_starred: true,
            is_draft: false,
            has_attachments: false,
        };

        let json = serde_json::to_string(&summary).unwrap();
        let decoded: EmailSummary = serde_json::from_str(&json).unwrap();

        assert_eq!(decoded.subject, "Hello");
        assert!(decoded.is_starred);
        assert_eq!(decoded.from_name.as_deref(), Some("Sender"));
    }
}

/// Response wrapper matching the frontend shape: `{ email, attachments }`.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EmailDetailResponse {
    pub email: Email,
    pub attachments: Vec<Attachment>,
}
