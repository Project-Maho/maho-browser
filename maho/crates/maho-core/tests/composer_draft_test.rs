//! Regression tests for the settings-backed composer draft store (plan todo 1, C1-core).
//!
//! Contract under test:
//!   scope JSON: {"kind":"new_task"} | {"kind":"conversation","conversationId":"<id>"}
//!   keys:       web_ai.composer_draft.v1.new_task
//!               web_ai.composer_draft.v1.conversation:<id>
//!   value JSON: {"version":1,"text":<verbatim>,"updatedAt":<native RFC3339>}

use maho_core::composer_draft::COMPOSER_DRAFT_KEY_PREFIX;
use maho_core::maho_core::MahoCore;

const NEW_TASK_SCOPE: &str = r#"{"kind":"new_task"}"#;

fn core() -> MahoCore {
    // Process-wide key injection; idempotent across tests in this binary.
    let _ = maho_storage::sqlite::set_sqlcipher_key("composer-draft-test-key");
    let core = MahoCore::new().with_storage(":memory:");
    assert!(core.storage_ref().is_some(), "test storage must open");
    core
}

fn conversation_scope(id: &str) -> String {
    serde_json::json!({ "kind": "conversation", "conversationId": id }).to_string()
}

fn raw_setting(core: &MahoCore, key: &str) -> Option<String> {
    core.storage_ref()
        .expect("storage")
        .get_setting(key)
        .expect("get_setting")
}

// === key mapping ===

#[test]
fn new_task_scope_maps_to_new_task_key() {
    let core = core();
    assert!(core.set_composer_draft(NEW_TASK_SCOPE, "hello"));
    assert_eq!(
        raw_setting(&core, &format!("{COMPOSER_DRAFT_KEY_PREFIX}new_task")).is_some(),
        true
    );
}

#[test]
fn conversation_scope_maps_to_conversation_key() {
    let mut core = core();
    assert!(core.create_conversation_persisted("conv-1", None, None, None));
    assert!(core.set_composer_draft(&conversation_scope("conv-1"), "hi"));
    assert!(raw_setting(
        &core,
        &format!("{COMPOSER_DRAFT_KEY_PREFIX}conversation:conv-1")
    )
    .is_some());
}

// === roundtrip per scope kind ===

#[test]
fn new_task_roundtrip_returns_draft_json() {
    let core = core();
    assert!(core.set_composer_draft(NEW_TASK_SCOPE, "draft body"));
    let raw = core.get_composer_draft(NEW_TASK_SCOPE).expect("draft json");
    let parsed: serde_json::Value = serde_json::from_str(&raw).unwrap();
    assert_eq!(parsed["version"], 1);
    assert_eq!(parsed["text"], "draft body");
    let updated_at = parsed["updatedAt"].as_str().expect("updatedAt string");
    chrono::DateTime::parse_from_rfc3339(updated_at).expect("RFC3339 updatedAt");
}

#[test]
fn conversation_roundtrip_returns_draft_json() {
    let mut core = core();
    assert!(core.create_conversation_persisted("conv-rt", None, None, None));
    let scope = conversation_scope("conv-rt");
    assert!(core.set_composer_draft(&scope, "conv draft"));
    let parsed: serde_json::Value =
        serde_json::from_str(&core.get_composer_draft(&scope).expect("draft")).unwrap();
    assert_eq!(parsed["text"], "conv draft");
}

#[test]
fn scopes_are_isolated_from_each_other() {
    let mut core = core();
    assert!(core.create_conversation_persisted("conv-a", None, None, None));
    assert!(core.create_conversation_persisted("conv-b", None, None, None));
    assert!(core.set_composer_draft(NEW_TASK_SCOPE, "n"));
    assert!(core.set_composer_draft(&conversation_scope("conv-a"), "a"));
    assert!(core.set_composer_draft(&conversation_scope("conv-b"), "b"));

    let text = |raw: String| -> String {
        serde_json::from_str::<serde_json::Value>(&raw).unwrap()["text"]
            .as_str()
            .unwrap()
            .to_string()
    };
    assert_eq!(text(core.get_composer_draft(NEW_TASK_SCOPE).unwrap()), "n");
    assert_eq!(
        text(
            core.get_composer_draft(&conversation_scope("conv-a"))
                .unwrap()
        ),
        "a"
    );
    assert_eq!(
        text(
            core.get_composer_draft(&conversation_scope("conv-b"))
                .unwrap()
        ),
        "b"
    );
}

// === verbatim text ===

#[test]
fn text_is_stored_verbatim_without_trimming_or_normalizing() {
    let core = core();
    let text = "  leading and trailing\n\ttabs\r\n and \"quotes\" \u{1F600}  ";
    assert!(core.set_composer_draft(NEW_TASK_SCOPE, text));
    let parsed: serde_json::Value =
        serde_json::from_str(&core.get_composer_draft(NEW_TASK_SCOPE).unwrap()).unwrap();
    assert_eq!(parsed["text"].as_str().unwrap(), text);
}

#[test]
fn whitespace_only_text_is_preserved_not_treated_as_empty() {
    let core = core();
    assert!(core.set_composer_draft(NEW_TASK_SCOPE, "   "));
    let parsed: serde_json::Value =
        serde_json::from_str(&core.get_composer_draft(NEW_TASK_SCOPE).unwrap()).unwrap();
    assert_eq!(parsed["text"].as_str().unwrap(), "   ");
}

// === empty-string deletes ===

#[test]
fn empty_string_set_deletes_the_row() {
    let core = core();
    assert!(core.set_composer_draft(NEW_TASK_SCOPE, "something"));
    assert!(core.set_composer_draft(NEW_TASK_SCOPE, ""));
    assert!(core.get_composer_draft(NEW_TASK_SCOPE).is_none());
    assert!(raw_setting(&core, &format!("{COMPOSER_DRAFT_KEY_PREFIX}new_task")).is_none());
}

#[test]
fn empty_string_set_on_absent_row_still_succeeds() {
    let core = core();
    assert!(core.set_composer_draft(NEW_TASK_SCOPE, ""));
    assert!(core.get_composer_draft(NEW_TASK_SCOPE).is_none());
}

// === explicit delete ===

#[test]
fn delete_removes_draft_and_is_idempotent() {
    let core = core();
    assert!(core.set_composer_draft(NEW_TASK_SCOPE, "x"));
    assert!(core.delete_composer_draft(NEW_TASK_SCOPE));
    assert!(core.get_composer_draft(NEW_TASK_SCOPE).is_none());
    // Deleting again is a successful no-op (mutation succeeded, nothing to remove).
    assert!(core.delete_composer_draft(NEW_TASK_SCOPE));
}

// === orphan cleanup on get ===

#[test]
fn get_for_missing_conversation_returns_none_and_purges_orphan_row() {
    let core = core();
    let key = format!("{COMPOSER_DRAFT_KEY_PREFIX}conversation:ghost");
    core.storage_ref()
        .unwrap()
        .set_setting(
            &key,
            r#"{"version":1,"text":"orphan","updatedAt":"2026-01-01T00:00:00Z"}"#,
        )
        .unwrap();
    assert!(core
        .get_composer_draft(&conversation_scope("ghost"))
        .is_none());
    assert!(
        raw_setting(&core, &key).is_none(),
        "orphan row must be purged"
    );
}

#[test]
fn get_for_existing_conversation_is_not_purged() {
    let mut core = core();
    assert!(core.create_conversation_persisted("conv-keep", None, None, None));
    let scope = conversation_scope("conv-keep");
    assert!(core.set_composer_draft(&scope, "keep me"));
    assert!(core.get_composer_draft(&scope).is_some());
    assert!(core.get_composer_draft(&scope).is_some());
}

/// Set does NOT require the conversation row to exist: a chat session can have a known
/// id before its first message creates the `conversations` row. The orphan is reaped by
/// the next `get` if the conversation never materializes.
#[test]
fn set_for_not_yet_persisted_conversation_is_accepted_then_reaped_on_get() {
    let core = core();
    let key = format!("{COMPOSER_DRAFT_KEY_PREFIX}conversation:pending");
    assert!(core.set_composer_draft(&conversation_scope("pending"), "text"));
    assert!(raw_setting(&core, &key).is_some());
    assert!(core
        .get_composer_draft(&conversation_scope("pending"))
        .is_none());
    assert!(raw_setting(&core, &key).is_none());
}

#[test]
fn delete_for_missing_conversation_still_purges_orphan() {
    let core = core();
    let key = format!("{COMPOSER_DRAFT_KEY_PREFIX}conversation:ghost2");
    core.storage_ref()
        .unwrap()
        .set_setting(&key, r#"{"version":1,"text":"o","updatedAt":"x"}"#)
        .unwrap();
    assert!(core.delete_composer_draft(&conversation_scope("ghost2")));
    assert!(raw_setting(&core, &key).is_none());
}

// === invalid scope handling ===

#[test]
fn invalid_scope_json_returns_none_and_false() {
    let core = core();
    for scope in [
        "",
        "not json",
        "{",
        "[]",
        "null",
        "42",
        r#"{"kind":"unknown"}"#,
        r#"{"kind":"conversation"}"#,
        r#"{"kind":"conversation","conversationId":""}"#,
        r#"{"kind":"conversation","conversationId":123}"#,
        r#"{"kind":123}"#,
        r#"{}"#,
    ] {
        assert!(
            core.get_composer_draft(scope).is_none(),
            "get must be None for scope {scope:?}"
        );
        assert!(
            !core.set_composer_draft(scope, "text"),
            "set must be false for scope {scope:?}"
        );
        assert!(
            !core.delete_composer_draft(scope),
            "delete must be false for scope {scope:?}"
        );
    }
}

#[test]
fn new_task_scope_ignores_extra_fields() {
    let core = core();
    let scope = r#"{"kind":"new_task","conversationId":"ignored"}"#;
    assert!(core.set_composer_draft(scope, "t"));
    assert!(raw_setting(&core, &format!("{COMPOSER_DRAFT_KEY_PREFIX}new_task")).is_some());
}

// === corrupt stored value ===

#[test]
fn corrupt_stored_value_returns_none() {
    let core = core();
    core.storage_ref()
        .unwrap()
        .set_setting(&format!("{COMPOSER_DRAFT_KEY_PREFIX}new_task"), "{not json")
        .unwrap();
    assert!(core.get_composer_draft(NEW_TASK_SCOPE).is_none());
}

// === no storage ===

#[test]
fn without_storage_all_operations_fail_softly() {
    let core = MahoCore::new();
    assert!(core.get_composer_draft(NEW_TASK_SCOPE).is_none());
    assert!(!core.set_composer_draft(NEW_TASK_SCOPE, "x"));
    assert!(!core.delete_composer_draft(NEW_TASK_SCOPE));
}
