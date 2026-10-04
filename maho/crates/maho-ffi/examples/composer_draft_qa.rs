//! Manual QA driver for the composer-draft FFI surface (plan todo 1, C1-core).
//!
//! Not a test: this is a real binary that drives the exported C ABI end-to-end against an
//! on-disk SQLCipher database and prints every observed payload, so the FFI contract can be
//! inspected by a human without trusting mocks.
//!
//! Run: `cd maho && cargo run -p maho-ffi --example composer_draft_qa`

use std::ffi::{CStr, CString};
use std::os::raw::c_char;

fn cs(s: &str) -> CString {
    CString::new(s).unwrap()
}

fn show(ptr: *mut c_char) -> String {
    if ptr.is_null() {
        return "<null>".to_string();
    }
    let out = unsafe { CStr::from_ptr(ptr) }.to_str().unwrap().to_string();
    unsafe { maho_ffi::maho_string_free(ptr) };
    out
}

fn main() {
    let key = cs("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    assert!(unsafe { maho_ffi::maho_storage_set_sqlcipher_key(key.as_ptr()) });

    let dir = std::env::temp_dir().join(format!("maho-composer-draft-qa-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let db = dir.join("qa.sqlite");
    let db_c = cs(db.to_str().unwrap());

    let core = unsafe { maho_ffi::maho_core_new_with_storage(db_c.as_ptr()) };
    assert!(!core.is_null(), "core must open");
    println!("db: {}", db.display());

    // --- Scope 1: new_task ---
    let new_task = cs(r#"{"kind":"new_task"}"#);
    println!(
        "[new_task] get before set        -> {}",
        show(unsafe { maho_ffi::maho_core_get_composer_draft(core, new_task.as_ptr()) })
    );
    let text = cs("  unsent draft\n\tsecond line  ");
    println!("[new_task] set(verbatim padded)  -> {}", unsafe {
        maho_ffi::maho_core_set_composer_draft(core, new_task.as_ptr(), text.as_ptr())
    });
    println!(
        "[new_task] get after set         -> {}",
        show(unsafe { maho_ffi::maho_core_get_composer_draft(core, new_task.as_ptr()) })
    );
    let empty = cs("");
    println!("[new_task] set(\"\") deletes       -> {}", unsafe {
        maho_ffi::maho_core_set_composer_draft(core, new_task.as_ptr(), empty.as_ptr())
    });
    println!(
        "[new_task] get after empty set   -> {}",
        show(unsafe { maho_ffi::maho_core_get_composer_draft(core, new_task.as_ptr()) })
    );

    // --- Scope 2: conversation (existing) ---
    let conv_id = cs("qa-conv-1");
    assert!(unsafe {
        maho_ffi::maho_core_create_conversation(
            core,
            conv_id.as_ptr(),
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null(),
        )
    });
    let conv_scope = cs(r#"{"kind":"conversation","conversationId":"qa-conv-1"}"#);
    let conv_text = cs("draft bound to a live conversation");
    println!("[conv/live] set                  -> {}", unsafe {
        maho_ffi::maho_core_set_composer_draft(core, conv_scope.as_ptr(), conv_text.as_ptr())
    });
    println!(
        "[conv/live] get                  -> {}",
        show(unsafe { maho_ffi::maho_core_get_composer_draft(core, conv_scope.as_ptr()) })
    );
    println!("[conv/live] delete               -> {}", unsafe {
        maho_ffi::maho_core_delete_composer_draft(core, conv_scope.as_ptr())
    });
    println!(
        "[conv/live] get after delete     -> {}",
        show(unsafe { maho_ffi::maho_core_get_composer_draft(core, conv_scope.as_ptr()) })
    );

    // --- Scope 3: orphan conversation (no such conversation row) ---
    let ghost_scope = cs(r#"{"kind":"conversation","conversationId":"qa-ghost"}"#);
    let ghost_text = cs("orphan draft");
    println!("[conv/ghost] set                 -> {}", unsafe {
        maho_ffi::maho_core_set_composer_draft(core, ghost_scope.as_ptr(), ghost_text.as_ptr())
    });
    println!(
        "[conv/ghost] get (purges orphan) -> {}",
        show(unsafe { maho_ffi::maho_core_get_composer_draft(core, ghost_scope.as_ptr()) })
    );
    println!(
        "[conv/ghost] get again           -> {}",
        show(unsafe { maho_ffi::maho_core_get_composer_draft(core, ghost_scope.as_ptr()) })
    );

    // --- Malformed scope behavior ---
    for bad in [
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
        r#"{}"#,
    ] {
        let scope = cs(bad);
        let t = cs("x");
        let got = show(unsafe { maho_ffi::maho_core_get_composer_draft(core, scope.as_ptr()) });
        let set =
            unsafe { maho_ffi::maho_core_set_composer_draft(core, scope.as_ptr(), t.as_ptr()) };
        let del = unsafe { maho_ffi::maho_core_delete_composer_draft(core, scope.as_ptr()) };
        println!("[malformed {bad:?}] get={got} set={set} delete={del}");
    }

    // --- Null-pointer behavior ---
    println!(
        "[null] get(core=null)            -> {}",
        show(unsafe {
            maho_ffi::maho_core_get_composer_draft(std::ptr::null_mut(), new_task.as_ptr())
        })
    );
    println!(
        "[null] get(scope=null)           -> {}",
        show(unsafe { maho_ffi::maho_core_get_composer_draft(core, std::ptr::null()) })
    );
    println!("[null] set(text=null)            -> {}", unsafe {
        maho_ffi::maho_core_set_composer_draft(core, new_task.as_ptr(), std::ptr::null())
    });

    // --- Persistence across core instances (panel close / process restart analogue) ---
    let persist_text = cs("draft that must survive a restart");
    assert!(unsafe {
        maho_ffi::maho_core_set_composer_draft(core, new_task.as_ptr(), persist_text.as_ptr())
    });
    unsafe { maho_ffi::maho_core_free(core) };
    let reopened = unsafe { maho_ffi::maho_core_new_with_storage(db_c.as_ptr()) };
    println!(
        "[restart] get after reopen       -> {}",
        show(unsafe { maho_ffi::maho_core_get_composer_draft(reopened, new_task.as_ptr()) })
    );
    unsafe { maho_ffi::maho_core_free(reopened) };

    let _ = std::fs::remove_dir_all(&dir);
    println!("cleanup: removed {}", dir.display());
}
