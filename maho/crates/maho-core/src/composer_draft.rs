//! Settings-table-backed composer draft store (C1-core).
//!
//! Drafts are unsent composer text that must survive panel close / context switches.
//! They are device- and profile-local: stored in the existing `settings(key, value)`
//! table, never synced, and never trimmed or normalized.
//!
//! Scope JSON (as received from FFI callers):
//!   `{"kind":"new_task"}`
//!   `{"kind":"conversation","conversationId":"<id>"}`
//!
//! Settings keys:
//!   `web_ai.composer_draft.v1.new_task`
//!   `web_ai.composer_draft.v1.conversation:<id>`
//!
//! Stored value JSON:
//!   `{"version":1,"text":<verbatim text>,"updatedAt":<native-assigned RFC3339>}`

use serde::{Deserialize, Serialize};

/// Shared prefix for every composer-draft settings key.
pub const COMPOSER_DRAFT_KEY_PREFIX: &str = "web_ai.composer_draft.v1.";

/// Current draft payload version.
pub const COMPOSER_DRAFT_VERSION: u32 = 1;

/// Parsed composer-draft scope.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ComposerDraftScope {
    NewTask,
    Conversation(String),
}

impl ComposerDraftScope {
    /// Parse a scope JSON document. Returns `None` for any malformed or unknown shape;
    /// callers translate that into null / false at the FFI boundary.
    pub fn parse(scope_json: &str) -> Option<Self> {
        let value: serde_json::Value = serde_json::from_str(scope_json).ok()?;
        let kind = value.get("kind")?.as_str()?;
        match kind {
            "new_task" => Some(ComposerDraftScope::NewTask),
            "conversation" => {
                let id = value.get("conversationId")?.as_str()?;
                if id.is_empty() {
                    return None;
                }
                Some(ComposerDraftScope::Conversation(id.to_string()))
            }
            _ => None,
        }
    }

    /// Settings key for this scope.
    pub fn storage_key(&self) -> String {
        match self {
            ComposerDraftScope::NewTask => format!("{COMPOSER_DRAFT_KEY_PREFIX}new_task"),
            ComposerDraftScope::Conversation(id) => {
                format!("{COMPOSER_DRAFT_KEY_PREFIX}conversation:{id}")
            }
        }
    }

    /// Conversation id this scope is bound to, if any.
    pub fn conversation_id(&self) -> Option<&str> {
        match self {
            ComposerDraftScope::NewTask => None,
            ComposerDraftScope::Conversation(id) => Some(id.as_str()),
        }
    }
}

/// Persisted draft payload.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct ComposerDraft {
    pub version: u32,
    /// Verbatim composer text — never trimmed, never normalized.
    pub text: String,
    /// Native-assigned RFC3339 timestamp of the last write.
    #[serde(rename = "updatedAt")]
    pub updated_at: String,
}

impl ComposerDraft {
    pub fn new(text: String, updated_at: String) -> Self {
        Self {
            version: COMPOSER_DRAFT_VERSION,
            text,
            updated_at,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_new_task_scope() {
        assert_eq!(
            ComposerDraftScope::parse(r#"{"kind":"new_task"}"#),
            Some(ComposerDraftScope::NewTask)
        );
    }

    #[test]
    fn parses_conversation_scope() {
        assert_eq!(
            ComposerDraftScope::parse(r#"{"kind":"conversation","conversationId":"c1"}"#),
            Some(ComposerDraftScope::Conversation("c1".to_string()))
        );
    }

    #[test]
    fn rejects_malformed_scopes() {
        for bad in [
            "",
            "{",
            "[]",
            "null",
            "42",
            r#"{"kind":"conversation"}"#,
            r#"{"kind":"conversation","conversationId":""}"#,
            r#"{"kind":"conversation","conversationId":7}"#,
            r#"{"kind":"other"}"#,
            r#"{}"#,
        ] {
            assert_eq!(ComposerDraftScope::parse(bad), None, "scope {bad:?}");
        }
    }

    #[test]
    fn key_mapping_matches_contract() {
        assert_eq!(
            ComposerDraftScope::NewTask.storage_key(),
            "web_ai.composer_draft.v1.new_task"
        );
        assert_eq!(
            ComposerDraftScope::Conversation("abc".into()).storage_key(),
            "web_ai.composer_draft.v1.conversation:abc"
        );
    }

    #[test]
    fn draft_serializes_with_camel_case_updated_at() {
        let json = serde_json::to_string(&ComposerDraft::new(
            "hi".into(),
            "2026-01-01T00:00:00+00:00".into(),
        ))
        .unwrap();
        assert_eq!(
            json,
            r#"{"version":1,"text":"hi","updatedAt":"2026-01-01T00:00:00+00:00"}"#
        );
    }
}
