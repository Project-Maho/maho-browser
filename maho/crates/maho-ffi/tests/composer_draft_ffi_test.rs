//! FFI contract tests for the composer draft surface (plan todo 1, C1-core).
//!
//! Exercises the real exported C ABI: `maho_core_get_composer_draft`,
//! `maho_core_set_composer_draft`, `maho_core_delete_composer_draft`.

use std::ffi::{CStr, CString};
use std::os::raw::c_char;

use serde_json::Value;

const NEW_TASK_SCOPE: &str = r#"{"kind":"new_task"}"#;

fn cs(s: &str) -> CString {
    CString::new(s).unwrap()
}

fn conversation_scope(id: &str) -> CString {
    cs(&serde_json::json!({ "kind": "conversation", "conversationId": id }).to_string())
}

/// Take ownership of an FFI-returned string; None when null.
fn take_string(ptr: *mut c_char) -> Option<String> {
    if ptr.is_null() {
        return None;
    }
    let out = unsafe { CStr::from_ptr(ptr) }.to_str().unwrap().to_string();
    unsafe { maho_ffi::maho_string_free(ptr) };
    Some(out)
}

struct TestCore {
    ptr: *mut maho_core::maho_core::MahoCore,
    _dir: tempfile::TempDir,
}

impl Drop for TestCore {
    fn drop(&mut self) {
        unsafe { maho_ffi::maho_core_free(self.ptr) };
    }
}

fn new_core() -> TestCore {
    let key = cs("f0e1d2c3b4a5968778695a4b3c2d1e0ff0e1d2c3b4a5968778695a4b3c2d1e0f");
    assert!(unsafe { maho_ffi::maho_storage_set_sqlcipher_key(key.as_ptr()) });
    let dir = tempfile::tempdir().unwrap();
    let db_path = cs(dir.path().join("draft.sqlite").to_str().unwrap());
    let ptr = unsafe { maho_ffi::maho_core_new_with_storage(db_path.as_ptr()) };
    assert!(!ptr.is_null());
    TestCore { ptr, _dir: dir }
}

#[test]
fn ffi_set_get_delete_roundtrip_new_task() {
    let core = new_core();
    let scope = cs(NEW_TASK_SCOPE);
    let text = cs("  hello ffi\n draft  ");

    assert!(unsafe {
        maho_ffi::maho_core_set_composer_draft(core.ptr, scope.as_ptr(), text.as_ptr())
    });

    let raw =
        take_string(unsafe { maho_ffi::maho_core_get_composer_draft(core.ptr, scope.as_ptr()) })
            .expect("draft json");
    let parsed: Value = serde_json::from_str(&raw).unwrap();
    assert_eq!(parsed["version"], 1);
    assert_eq!(parsed["text"], "  hello ffi\n draft  ");
    chrono::DateTime::parse_from_rfc3339(parsed["updatedAt"].as_str().unwrap()).unwrap();

    assert!(unsafe { maho_ffi::maho_core_delete_composer_draft(core.ptr, scope.as_ptr()) });
    assert!(take_string(unsafe {
        maho_ffi::maho_core_get_composer_draft(core.ptr, scope.as_ptr())
    })
    .is_none());
}

#[test]
fn ffi_conversation_scope_roundtrip() {
    let core = new_core();
    let conv_id = cs("conv-ffi-1");
    assert!(unsafe {
        maho_ffi::maho_core_create_conversation(
            core.ptr,
            conv_id.as_ptr(),
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null(),
        )
    });
    let scope = conversation_scope("conv-ffi-1");
    let text = cs("conversation draft");
    assert!(unsafe {
        maho_ffi::maho_core_set_composer_draft(core.ptr, scope.as_ptr(), text.as_ptr())
    });
    let raw =
        take_string(unsafe { maho_ffi::maho_core_get_composer_draft(core.ptr, scope.as_ptr()) })
            .expect("draft json");
    let parsed: Value = serde_json::from_str(&raw).unwrap();
    assert_eq!(parsed["text"], "conversation draft");
}

#[test]
fn ffi_empty_text_deletes_draft() {
    let core = new_core();
    let scope = cs(NEW_TASK_SCOPE);
    let text = cs("to be cleared");
    assert!(unsafe {
        maho_ffi::maho_core_set_composer_draft(core.ptr, scope.as_ptr(), text.as_ptr())
    });
    let empty = cs("");
    assert!(unsafe {
        maho_ffi::maho_core_set_composer_draft(core.ptr, scope.as_ptr(), empty.as_ptr())
    });
    assert!(take_string(unsafe {
        maho_ffi::maho_core_get_composer_draft(core.ptr, scope.as_ptr())
    })
    .is_none());
}

#[test]
fn ffi_orphan_draft_is_purged_on_get() {
    let core = new_core();
    let scope = conversation_scope("ghost-conv");
    let text = cs("orphan text");
    assert!(unsafe {
        maho_ffi::maho_core_set_composer_draft(core.ptr, scope.as_ptr(), text.as_ptr())
    });
    // No such conversation => null and the row is reaped.
    assert!(take_string(unsafe {
        maho_ffi::maho_core_get_composer_draft(core.ptr, scope.as_ptr())
    })
    .is_none());
    // Second get still null (row already gone).
    assert!(take_string(unsafe {
        maho_ffi::maho_core_get_composer_draft(core.ptr, scope.as_ptr())
    })
    .is_none());
}

#[test]
fn ffi_invalid_scope_returns_null_and_false_without_panic() {
    let core = new_core();
    let text = cs("text");
    for bad in [
        "",
        "not json",
        "{",
        "[]",
        "null",
        r#"{"kind":"unknown"}"#,
        r#"{"kind":"conversation"}"#,
        r#"{"kind":"conversation","conversationId":""}"#,
    ] {
        let scope = cs(bad);
        assert!(
            unsafe { maho_ffi::maho_core_get_composer_draft(core.ptr, scope.as_ptr()) }.is_null(),
            "get must be null for {bad:?}"
        );
        assert!(
            !unsafe {
                maho_ffi::maho_core_set_composer_draft(core.ptr, scope.as_ptr(), text.as_ptr())
            },
            "set must be false for {bad:?}"
        );
        assert!(
            !unsafe { maho_ffi::maho_core_delete_composer_draft(core.ptr, scope.as_ptr()) },
            "delete must be false for {bad:?}"
        );
    }
}

#[test]
fn ffi_null_arguments_are_safe() {
    let core = new_core();
    let scope = cs(NEW_TASK_SCOPE);
    let text = cs("t");

    assert!(unsafe {
        maho_ffi::maho_core_get_composer_draft(std::ptr::null_mut(), scope.as_ptr())
    }
    .is_null());
    assert!(
        unsafe { maho_ffi::maho_core_get_composer_draft(core.ptr, std::ptr::null()) }.is_null()
    );
    assert!(!unsafe {
        maho_ffi::maho_core_set_composer_draft(std::ptr::null_mut(), scope.as_ptr(), text.as_ptr())
    });
    assert!(!unsafe {
        maho_ffi::maho_core_set_composer_draft(core.ptr, std::ptr::null(), text.as_ptr())
    });
    assert!(!unsafe {
        maho_ffi::maho_core_set_composer_draft(core.ptr, scope.as_ptr(), std::ptr::null())
    });
    assert!(!unsafe {
        maho_ffi::maho_core_delete_composer_draft(std::ptr::null_mut(), scope.as_ptr())
    });
    assert!(!unsafe { maho_ffi::maho_core_delete_composer_draft(core.ptr, std::ptr::null()) });
}

#[test]
fn ffi_draft_survives_core_reopen() {
    let key = cs("f0e1d2c3b4a5968778695a4b3c2d1e0ff0e1d2c3b4a5968778695a4b3c2d1e0f");
    assert!(unsafe { maho_ffi::maho_storage_set_sqlcipher_key(key.as_ptr()) });
    let dir = tempfile::tempdir().unwrap();
    let db_path = cs(dir.path().join("persist.sqlite").to_str().unwrap());
    let scope = cs(NEW_TASK_SCOPE);
    let text = cs("survives restart");

    let first = unsafe { maho_ffi::maho_core_new_with_storage(db_path.as_ptr()) };
    assert!(!first.is_null());
    assert!(unsafe {
        maho_ffi::maho_core_set_composer_draft(first, scope.as_ptr(), text.as_ptr())
    });
    unsafe { maho_ffi::maho_core_free(first) };

    let second = unsafe { maho_ffi::maho_core_new_with_storage(db_path.as_ptr()) };
    assert!(!second.is_null());
    let raw =
        take_string(unsafe { maho_ffi::maho_core_get_composer_draft(second, scope.as_ptr()) })
            .expect("draft persisted across core instances");
    let parsed: Value = serde_json::from_str(&raw).unwrap();
    assert_eq!(parsed["text"], "survives restart");
    unsafe { maho_ffi::maho_core_free(second) };
}
