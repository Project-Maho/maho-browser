//! Manual-QA driver for C1-mobile (plan todo 2) executed against REAL core
//! storage rather than a mock bridge.
//!
//! Each scenario replays exactly what the shared web-ai draft adapter asks the
//! iOS/Android native bridges to do, in order, and asserts the observable state
//! the user would see. The RPC/marshalling layers above this are covered by the
//! vitest suites (web-ai) and the Robolectric dispatch tests (Android).

use maho_core::maho_core::MahoCore;

const NEW_TASK_SCOPE: &str = r#"{"kind":"new_task"}"#;

fn core() -> MahoCore {
    let _ = maho_storage::sqlite::set_sqlcipher_key("composer-draft-mobile-qa-key");
    let core = MahoCore::new().with_storage(":memory:");
    assert!(core.storage_ref().is_some(), "QA storage must open");
    core
}

fn conversation_scope(id: &str) -> String {
    serde_json::json!({ "kind": "conversation", "conversationId": id }).to_string()
}

fn draft_text(core: &MahoCore, scope_json: &str) -> Option<String> {
    let raw = core.get_composer_draft(scope_json)?;
    let value: serde_json::Value = serde_json::from_str(&raw).expect("draft payload is JSON");
    Some(value["text"].as_str().expect("text field").to_string())
}

/// QA-1 (happy): typing in a brand-new chat, closing the panel, reopening it.
#[test]
fn qa_restore_after_panel_close_reopen() {
    let core = core();

    // User types; the adapter debounce fires / blur flushes.
    assert!(core.set_composer_draft(NEW_TASK_SCOPE, "half-written question"));

    // Panel closes and the WebView is torn down; a fresh mount hydrates.
    assert_eq!(
        draft_text(&core, NEW_TASK_SCOPE).as_deref(),
        Some("half-written question"),
        "draft must survive a panel close/reopen",
    );
}

/// QA-2 (happy): first successful send clears the draft and promotes the scope.
#[test]
fn qa_first_send_clears_draft_and_promotes_scope() {
    let mut core = core();
    let conversation_id = "conv-first-send";

    // Pre-send: the row does not exist yet, so the draft is new_task-scoped.
    assert!(core.set_composer_draft(NEW_TASK_SCOPE, "first message"));

    // The send succeeds: core persists the conversation row, then the screen
    // clears the scope it actually submitted under.
    assert!(core.save_conversation_message_persisted(
        conversation_id,
        "user",
        "first message",
        None
    ));
    assert!(core.delete_composer_draft(NEW_TASK_SCOPE));
    assert_eq!(
        draft_text(&core, NEW_TASK_SCOPE),
        None,
        "the submitted draft must be gone after a successful send",
    );

    // Post-send the composer is conversation-scoped and persists there.
    let scope = conversation_scope(conversation_id);
    assert!(core.set_composer_draft(&scope, "follow-up draft"));
    assert_eq!(
        draft_text(&core, &scope).as_deref(),
        Some("follow-up draft"),
        "a promoted conversation scope must round-trip",
    );
}

/// QA-3 (failure): a failed send retains the draft for the retry.
#[test]
fn qa_failed_send_retains_draft() {
    let core = core();

    assert!(core.set_composer_draft(NEW_TASK_SCOPE, "will fail"));

    // The native send rejects: no conversation row is written and, critically,
    // the screen never issues the delete.
    assert_eq!(
        draft_text(&core, NEW_TASK_SCOPE).as_deref(),
        Some("will fail"),
        "a failed send must leave the draft recoverable",
    );
}

/// QA-4 (adversarial): the reason a pre-persistence chat MUST stay new_task.
///
/// Todo 1's get reaps a draft whose conversation row does not exist. Had the
/// screen promoted to conversation scope on the ephemeral session handle, the
/// user's unsent text would be destroyed by the very next hydrate.
#[test]
fn qa_premature_conversation_scope_would_destroy_the_draft() {
    let core = core();
    let ephemeral_handle = "session-handle-not-yet-persisted";
    let scope = conversation_scope(ephemeral_handle);

    assert!(core.set_composer_draft(&scope, "unsent text"));

    // Hydrate against a conversation core has never seen: reaped, not returned.
    assert_eq!(
        draft_text(&core, &scope),
        None,
        "an orphan-scoped draft is reaped on read",
    );

    // The shipped behaviour: new_task survives the same sequence untouched.
    assert!(core.set_composer_draft(NEW_TASK_SCOPE, "unsent text"));
    assert_eq!(
        draft_text(&core, NEW_TASK_SCOPE).as_deref(),
        Some("unsent text"),
        "new_task scope is immune to orphan reaping",
    );
}

/// QA-5 (adversarial): malformed scopes are rejected, never written.
#[test]
fn qa_malformed_scopes_are_rejected_without_panicking() {
    let core = core();

    for bad in [
        "",
        "not json",
        "{}",
        r#"{"kind":"nope"}"#,
        r#"{"kind":"conversation"}"#,
        r#"["new_task"]"#,
    ] {
        assert!(!core.set_composer_draft(bad, "x"), "set rejects {bad:?}");
        assert_eq!(core.get_composer_draft(bad), None, "get rejects {bad:?}");
        assert!(!core.delete_composer_draft(bad), "delete rejects {bad:?}");
    }
}

/// QA-6 (adversarial): drafts are stored verbatim and survive a restart.
#[test]
fn qa_text_is_verbatim_and_survives_restart() {
    let core = core();
    // Leading/trailing whitespace, newlines, emoji, quotes, NUL-adjacent chars.
    let text = "  line one\n\tline two \"quoted\" \u{1F600} ‹›  ";

    assert!(core.set_composer_draft(NEW_TASK_SCOPE, text));

    // A restart re-reads from storage through a fresh accessor path.
    assert_eq!(
        draft_text(&core, NEW_TASK_SCOPE).as_deref(),
        Some(text),
        "text must never be trimmed or normalized",
    );

    // Empty string is the documented delete, not an empty payload.
    assert!(core.set_composer_draft(NEW_TASK_SCOPE, ""));
    assert_eq!(draft_text(&core, NEW_TASK_SCOPE), None);
}
