//! Folder normalization and classification logic.
//!
//! This module provides utilities for:
//! - Parsing IMAP LIST responses with attributes
//! - Classifying folders by special-use attributes (RFC 6154)
//! - Fallback name/path heuristics for providers like Gmail
//! - Determining folder selectability and canonical status

use crate::models::folder::FolderType;

/// Rich metadata about an IMAP folder from LIST response.
#[derive(Debug, Clone, PartialEq)]
pub struct FolderMetadata {
    /// Raw IMAP path (UTF-7 encoded mailbox name)
    pub raw_path: String,
    /// Decoded display name for UI
    pub display_name: String,
    /// IMAP LIST attributes (e.g., \\Inbox, \\Sent, \\Noselect)
    pub attributes: Vec<String>,
    /// Whether the folder can be selected (not \\Noselect)
    pub is_selectable: bool,
    /// Whether this is a canonical selectable folder (not a namespace container)
    pub is_canonical: bool,
    /// Delimiter used for hierarchy (e.g., "/" or ".")
    pub delimiter: Option<String>,
}

/// Classify a folder based on IMAP attributes first, then name heuristics.
///
/// Priority:
/// 1. RFC 6154 special-use attributes (\\Inbox, \\Sent, etc.)
/// 2. Name/path heuristics for known patterns
/// 3. Custom as fallback
pub fn classify_folder(metadata: &FolderMetadata) -> FolderType {
    // First: check IMAP special-use attributes (RFC 6154)
    for attr in &metadata.attributes {
        let attr_lower = attr.to_lowercase();
        match attr_lower.as_str() {
            "\\inbox" | "inbox" => return FolderType::Inbox,
            "\\sent" | "\\sentmail" | "\\sent mail" | "sent" | "sentmail" | "sent mail" => {
                return FolderType::Sent
            }
            "\\drafts" | "\\draft" | "drafts" | "draft" => return FolderType::Drafts,
            "\\trash" | "\\deleted" | "trash" | "deleted" => return FolderType::Trash,
            "\\junk" | "\\spam" | "junk" | "spam" => return FolderType::Spam,
            "\\archive" | "\\archives" | "archive" | "archives" => return FolderType::Archive,
            "\\all" | "\\allmail" | "all" | "allmail" => return FolderType::Archive,
            "\\flagged" | "\\starred" | "flagged" | "starred" => {
                // Flagged/Starred is often a virtual folder; treat as custom
                return FolderType::Custom;
            }
            _ => continue,
        }
    }

    // Second: name/path heuristics for providers without special-use
    classify_by_name(&metadata.display_name, &metadata.raw_path)
}

/// Classify folder based on name/path heuristics.
/// More conservative than attribute-based classification.
fn classify_by_name(display_name: &str, raw_path: &str) -> FolderType {
    let name_lower = display_name.to_lowercase();
    let path_lower = raw_path.to_lowercase();

    // Inbox is usually exact match
    if name_lower == "inbox" || path_lower == "inbox" {
        return FolderType::Inbox;
    }

    // Sent variations - be careful not to match "consent" etc.
    if is_sent_folder(&name_lower, &path_lower) {
        return FolderType::Sent;
    }

    // Drafts variations
    if is_drafts_folder(&name_lower, &path_lower) {
        return FolderType::Drafts;
    }

    // Trash variations
    if is_trash_folder(&name_lower, &path_lower) {
        return FolderType::Trash;
    }

    // Spam/Junk variations
    if is_spam_folder(&name_lower, &path_lower) {
        return FolderType::Spam;
    }

    // Archive variations
    if is_archive_folder(&name_lower, &path_lower) {
        return FolderType::Archive;
    }

    FolderType::Custom
}

fn is_sent_folder(name: &str, path: &str) -> bool {
    // Exact matches or clear sent patterns
    let sent_patterns = ["sent", "sent items", "sent mail", "볂은", "볂은메일"];
    for pattern in &sent_patterns {
        if name == *pattern || path == *pattern {
            return true;
        }
    }
    // Contains "sent" but not as part of other words
    if name.contains("sent")
        && !name.contains("consent")
        && !name.contains("dissent")
        && !name.contains("present")
        && !name.contains("absent")
    {
        return true;
    }
    false
}

fn is_drafts_folder(name: &str, path: &str) -> bool {
    let drafts_patterns = ["drafts", "draft", "임시", "임시보관"];
    for pattern in &drafts_patterns {
        if name == *pattern || path == *pattern {
            return true;
        }
    }
    false
}

fn is_trash_folder(name: &str, path: &str) -> bool {
    let trash_patterns = [
        "trash",
        "deleted",
        "deleted items",
        "휴지통",
        "삭제",
        "deleted messages",
    ];
    for pattern in &trash_patterns {
        if name == *pattern || path == *pattern {
            return true;
        }
    }
    false
}

fn is_spam_folder(name: &str, path: &str) -> bool {
    let spam_patterns = ["spam", "junk", "스팸", "정크"];
    for pattern in &spam_patterns {
        if name == *pattern || path == *pattern {
            return true;
        }
    }
    false
}

fn is_archive_folder(name: &str, path: &str) -> bool {
    let archive_patterns = [
        "archive",
        "archives",
        "all mail",
        "allmail",
        "전체",
        "전처리함",
    ];
    for pattern in &archive_patterns {
        if name == *pattern || path == *pattern {
            return true;
        }
    }
    false
}

/// Check if a folder is a provider namespace container (non-selectable).
/// These should not appear as normal folders in the sidebar.
pub fn is_namespace_container(metadata: &FolderMetadata) -> bool {
    // Gmail's [Gmail] namespace is a common container
    if metadata.raw_path == "[Gmail]" || metadata.display_name == "[Gmail]" {
        return true;
    }

    // Any folder with Noselect that has children is likely a container
    if !metadata.is_selectable && has_children_indicator(metadata) {
        return true;
    }

    // Provider-specific namespace patterns
    let path_lower = metadata.raw_path.to_lowercase();
    if path_lower == "inbox" || path_lower.ends_with("/inbox") || path_lower.ends_with(".inbox") {
        // Inbox is never a container
        return false;
    }

    // Check for common namespace prefixes that are containers
    if let Some(delim) = metadata.delimiter.as_ref() {
        // If path ends with delimiter, it's a namespace container
        if metadata.raw_path.ends_with(delim) {
            return true;
        }
    }

    false
}

/// Check if folder has children indicator (heuristic based on name patterns).
fn has_children_indicator(metadata: &FolderMetadata) -> bool {
    // If the folder has a delimiter and is a prefix for other folders,
    // it likely has children. We can't know for sure without listing children.
    // For now, use conservative heuristics.
    if let Some(ref delim) = metadata.delimiter {
        // Common patterns like "[Gmail]" or namespace prefixes
        if metadata.raw_path.contains(delim) {
            return false; // Has delimiter in middle, not a pure container
        }
    }
    // Single-level names that are known namespaces
    matches!(
        metadata.raw_path.as_str(),
        "[Gmail]" | "[Google Mail]" | "Notes" | "Archive"
    )
}

/// Determine if folder should be shown in sidebar as selectable.
/// Filters out namespace containers and non-selectable folders.
pub fn is_visible_folder(metadata: &FolderMetadata) -> bool {
    // Must be selectable
    if !metadata.is_selectable {
        return false;
    }

    // Must not be a namespace container
    if is_namespace_container(metadata) {
        return false;
    }

    true
}

/// Normalize folder metadata from IMAP LIST response.
/// Takes raw IMAP folder data and produces clean metadata.
pub fn normalize_folder(
    raw_path: &str,
    display_name: String,
    attributes: &[&str],
    delimiter: Option<&str>,
) -> FolderMetadata {
    let attrs: Vec<String> = attributes.iter().map(|s| s.to_string()).collect();

    // Check for Noselect attribute
    let is_selectable = !attrs.iter().any(|a| a.to_lowercase().contains("noselect"));

    // Check for NoInferiors (cannot have children)
    let _no_inferiors = attrs
        .iter()
        .any(|a| a.to_lowercase().contains("noinferiors"));

    let metadata = FolderMetadata {
        raw_path: raw_path.to_string(),
        display_name,
        attributes: attrs,
        is_selectable,
        is_canonical: false, // Will be set after namespace check
        delimiter: delimiter.map(|s| s.to_string()),
    };

    // Determine if this is a canonical folder (not a container)
    let is_canonical = is_visible_folder(&metadata);

    FolderMetadata {
        is_canonical,
        ..metadata
    }
}

/// String representation of FolderType for database storage.
pub fn folder_type_to_string(folder_type: &FolderType) -> &'static str {
    match folder_type {
        FolderType::Inbox => "inbox",
        FolderType::Sent => "sent",
        FolderType::Drafts => "drafts",
        FolderType::Trash => "trash",
        FolderType::Spam => "spam",
        FolderType::Archive => "archive",
        FolderType::Custom => "custom",
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_classify_by_inbox_attribute() {
        let metadata = FolderMetadata {
            raw_path: "INBOX".to_string(),
            display_name: "INBOX".to_string(),
            attributes: vec!["\\Inbox".to_string()],
            is_selectable: true,
            is_canonical: true,
            delimiter: Some("/".to_string()),
        };
        assert_eq!(classify_folder(&metadata), FolderType::Inbox);
    }

    #[test]
    fn test_classify_by_sent_attribute() {
        let metadata = FolderMetadata {
            raw_path: "Sent".to_string(),
            display_name: "Sent".to_string(),
            attributes: vec!["\\Sent".to_string()],
            is_selectable: true,
            is_canonical: true,
            delimiter: Some("/".to_string()),
        };
        assert_eq!(classify_folder(&metadata), FolderType::Sent);
    }

    #[test]
    fn test_classify_by_name_heuristic() {
        let metadata = FolderMetadata {
            raw_path: "Sent Items".to_string(),
            display_name: "Sent Items".to_string(),
            attributes: vec![],
            is_selectable: true,
            is_canonical: true,
            delimiter: Some("/".to_string()),
        };
        assert_eq!(classify_folder(&metadata), FolderType::Sent);
    }

    #[test]
    fn test_classify_drafts() {
        let metadata = FolderMetadata {
            raw_path: "Drafts".to_string(),
            display_name: "Drafts".to_string(),
            attributes: vec!["\\Drafts".to_string()],
            is_selectable: true,
            is_canonical: true,
            delimiter: Some("/".to_string()),
        };
        assert_eq!(classify_folder(&metadata), FolderType::Drafts);
    }

    #[test]
    fn test_classify_trash() {
        let metadata = FolderMetadata {
            raw_path: "Trash".to_string(),
            display_name: "Trash".to_string(),
            attributes: vec!["\\Trash".to_string()],
            is_selectable: true,
            is_canonical: true,
            delimiter: Some("/".to_string()),
        };
        assert_eq!(classify_folder(&metadata), FolderType::Trash);
    }

    #[test]
    fn test_classify_spam() {
        let metadata = FolderMetadata {
            raw_path: "Spam".to_string(),
            display_name: "Spam".to_string(),
            attributes: vec!["\\Junk".to_string()],
            is_selectable: true,
            is_canonical: true,
            delimiter: Some("/".to_string()),
        };
        assert_eq!(classify_folder(&metadata), FolderType::Spam);
    }

    #[test]
    fn test_classify_archive() {
        let metadata = FolderMetadata {
            raw_path: "Archive".to_string(),
            display_name: "Archive".to_string(),
            attributes: vec!["\\Archive".to_string()],
            is_selectable: true,
            is_canonical: true,
            delimiter: Some("/".to_string()),
        };
        assert_eq!(classify_folder(&metadata), FolderType::Archive);
    }

    #[test]
    fn test_classify_allmail_as_archive() {
        let metadata = FolderMetadata {
            raw_path: "[Gmail]/All Mail".to_string(),
            display_name: "All Mail".to_string(),
            attributes: vec!["\\All".to_string()],
            is_selectable: true,
            is_canonical: true,
            delimiter: Some("/".to_string()),
        };
        assert_eq!(classify_folder(&metadata), FolderType::Archive);
    }

    #[test]
    fn test_classify_custom() {
        let metadata = FolderMetadata {
            raw_path: "My Folder".to_string(),
            display_name: "My Folder".to_string(),
            attributes: vec![],
            is_selectable: true,
            is_canonical: true,
            delimiter: Some("/".to_string()),
        };
        assert_eq!(classify_folder(&metadata), FolderType::Custom);
    }

    #[test]
    fn test_noselect_not_visible() {
        let metadata = FolderMetadata {
            raw_path: "[Gmail]".to_string(),
            display_name: "[Gmail]".to_string(),
            attributes: vec!["\\Noselect".to_string()],
            is_selectable: false,
            is_canonical: false,
            delimiter: Some("/".to_string()),
        };
        assert!(!is_visible_folder(&metadata));
        assert!(is_namespace_container(&metadata));
    }

    #[test]
    fn test_gmail_namespace_container() {
        let metadata = FolderMetadata {
            raw_path: "[Gmail]".to_string(),
            display_name: "[Gmail]".to_string(),
            attributes: vec!["\\Noselect".to_string()],
            is_selectable: false,
            is_canonical: false,
            delimiter: Some("/".to_string()),
        };
        assert!(is_namespace_container(&metadata));
        assert!(!is_visible_folder(&metadata));
    }

    #[test]
    fn test_gmail_all_mail_visible() {
        let metadata = FolderMetadata {
            raw_path: "[Gmail]/All Mail".to_string(),
            display_name: "All Mail".to_string(),
            attributes: vec!["\\All".to_string(), "\\Archive".to_string()],
            is_selectable: true,
            is_canonical: true,
            delimiter: Some("/".to_string()),
        };
        assert!(!is_namespace_container(&metadata));
        assert!(is_visible_folder(&metadata));
    }

    #[test]
    fn test_normalize_folder() {
        let metadata = normalize_folder("INBOX", "INBOX".to_string(), &["\\Inbox"], Some("/"));
        assert_eq!(metadata.raw_path, "INBOX");
        assert_eq!(metadata.display_name, "INBOX");
        assert!(metadata.is_selectable);
        assert!(metadata.is_canonical);
        assert_eq!(metadata.attributes.len(), 1);
    }

    #[test]
    fn test_normalize_noselect() {
        let metadata =
            normalize_folder("[Gmail]", "[Gmail]".to_string(), &["\\Noselect"], Some("/"));
        assert!(!metadata.is_selectable);
        assert!(!metadata.is_canonical);
    }

    #[test]
    fn test_sent_conservative_matching() {
        // Should NOT match "consent" as sent
        let metadata = FolderMetadata {
            raw_path: "Consent Forms".to_string(),
            display_name: "Consent Forms".to_string(),
            attributes: vec![],
            is_selectable: true,
            is_canonical: true,
            delimiter: Some("/".to_string()),
        };
        assert_eq!(classify_folder(&metadata), FolderType::Custom);

        // Should match exact "Sent"
        let metadata2 = FolderMetadata {
            raw_path: "Sent".to_string(),
            display_name: "Sent".to_string(),
            attributes: vec![],
            is_selectable: true,
            is_canonical: true,
            delimiter: Some("/".to_string()),
        };
        assert_eq!(classify_folder(&metadata2), FolderType::Sent);
    }

    #[test]
    fn test_folder_type_to_string() {
        assert_eq!(folder_type_to_string(&FolderType::Inbox), "inbox");
        assert_eq!(folder_type_to_string(&FolderType::Sent), "sent");
        assert_eq!(folder_type_to_string(&FolderType::Drafts), "drafts");
        assert_eq!(folder_type_to_string(&FolderType::Trash), "trash");
        assert_eq!(folder_type_to_string(&FolderType::Spam), "spam");
        assert_eq!(folder_type_to_string(&FolderType::Archive), "archive");
        assert_eq!(folder_type_to_string(&FolderType::Custom), "custom");
    }
}
