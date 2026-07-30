use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub enum FolderType {
    Inbox,
    Sent,
    Drafts,
    Trash,
    Spam,
    Archive,
    Custom,
}

impl FolderType {
    pub fn from_str_lossy(s: &str) -> Self {
        match s {
            "inbox" => FolderType::Inbox,
            "sent" => FolderType::Sent,
            "drafts" => FolderType::Drafts,
            "trash" => FolderType::Trash,
            "spam" => FolderType::Spam,
            "archive" => FolderType::Archive,
            _ => FolderType::Custom,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Folder {
    pub id: String,
    pub account_id: String,
    pub name: String,
    pub path: String,
    pub folder_type: FolderType,
    pub unread_count: i64,
    pub total_count: i64,
}

/// Folder email counts for list views.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FolderCount {
    pub folder_id: String,
    pub unread_count: i64,
    pub total_count: i64,
}

/// Infer folder type from IMAP folder name.
pub fn guess_folder_type(name: &str) -> &'static str {
    let lower = name.to_lowercase();
    if lower == "inbox" {
        "inbox"
    } else if lower.contains("sent") || lower.contains("보낸") {
        "sent"
    } else if lower.contains("draft") || lower.contains("임시") {
        "drafts"
    } else if lower.contains("trash") || lower.contains("deleted") || lower.contains("휴지통") {
        "trash"
    } else if lower.contains("spam") || lower.contains("junk") || lower.contains("스팸") {
        "spam"
    } else if lower.contains("archive") || lower.contains("all mail") || lower.contains("전체") {
        "archive"
    } else {
        "custom"
    }
}
