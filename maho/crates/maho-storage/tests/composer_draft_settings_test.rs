//! Baseline characterization + regression tests for the settings-table surface backing
//! the composer draft store (plan todo 1, C1-core).
//!
//! The `baseline_*` tests document behavior that existed BEFORE `delete_setting` was
//! introduced; they must keep passing afterwards. The `delete_setting_*` tests are the
//! regression tests for the new API.

use maho_storage::sqlite::SqliteStorage;

fn sqlite_store() -> SqliteStorage {
    SqliteStorage::open_in_memory_with_key(&uuid::Uuid::new_v4().to_string()).unwrap()
}

// === Baseline characterization (pre-existing behavior) ===

#[test]
fn baseline_setting_roundtrip_is_verbatim() {
    let db = sqlite_store();
    let key = "web_ai.composer_draft.v1.new_task";
    let value = "  spaced\n\ttabbed  ";
    db.set_setting(key, value).unwrap();
    assert_eq!(db.get_setting(key).unwrap().as_deref(), Some(value));
}

#[test]
fn baseline_missing_setting_is_none() {
    let db = sqlite_store();
    assert!(db
        .get_setting("web_ai.composer_draft.v1.conversation:absent")
        .unwrap()
        .is_none());
}

#[test]
fn baseline_set_setting_overwrites_in_place() {
    let db = sqlite_store();
    let key = "web_ai.composer_draft.v1.new_task";
    db.set_setting(key, "first").unwrap();
    db.set_setting(key, "second").unwrap();
    assert_eq!(db.get_setting(key).unwrap().as_deref(), Some("second"));
}

// === delete_setting regression tests (new API) ===

#[test]
fn delete_setting_removes_existing_row_and_reports_true() {
    let db = sqlite_store();
    let key = "web_ai.composer_draft.v1.conversation:c1";
    db.set_setting(key, "draft").unwrap();
    assert!(db.delete_setting(key).unwrap());
    assert!(db.get_setting(key).unwrap().is_none());
}

#[test]
fn delete_setting_missing_row_reports_false() {
    let db = sqlite_store();
    assert!(!db.delete_setting("never.written").unwrap());
}

#[test]
fn delete_setting_is_idempotent() {
    let db = sqlite_store();
    let key = "web_ai.composer_draft.v1.new_task";
    db.set_setting(key, "draft").unwrap();
    assert!(db.delete_setting(key).unwrap());
    assert!(!db.delete_setting(key).unwrap());
}

#[test]
fn delete_setting_only_touches_the_named_key() {
    let db = sqlite_store();
    db.set_setting("web_ai.composer_draft.v1.new_task", "a")
        .unwrap();
    db.set_setting("web_ai.composer_draft.v1.conversation:x", "b")
        .unwrap();
    assert!(db
        .delete_setting("web_ai.composer_draft.v1.conversation:x")
        .unwrap());
    assert_eq!(
        db.get_setting("web_ai.composer_draft.v1.new_task")
            .unwrap()
            .as_deref(),
        Some("a")
    );
}
