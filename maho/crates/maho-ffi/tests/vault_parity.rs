use maho_core::maho_core::MahoCore;
use maho_ffi::*;
use serde_json::{json, Value};
use std::ffi::{c_char, CStr, CString};

fn call(
    core: &mut MahoCore,
    function: unsafe extern "C" fn(*mut MahoCore, *const c_char) -> *mut c_char,
    request: Value,
) -> Value {
    let request = CString::new(request.to_string()).unwrap();
    // SAFETY: The live core is exclusively borrowed and request remains valid through the call.
    owned(unsafe { function(core, request.as_ptr()) })
}

fn owned(pointer: *mut c_char) -> Value {
    assert!(!pointer.is_null());
    // SAFETY: Each pointer is an owned FFI response, read and freed exactly once.
    unsafe {
        let value = serde_json::from_slice(CStr::from_ptr(pointer).to_bytes()).unwrap();
        maho_string_free(pointer);
        value
    }
}

#[test]
fn vault_parity_ffi_json_round_trips_all_new_endpoints() {
    maho_storage::sqlite::set_sqlcipher_key("vault-parity-ffi-key").unwrap();
    let dir = tempfile::tempdir().unwrap();
    let mut core = MahoCore::new().with_storage(dir.path().join("vault.sqlite").to_str().unwrap());
    core.initialize_vault(b"master", b"recovery").unwrap();
    let added = call(
        &mut core,
        maho_vault_add_login_json,
        json!({"metadata": {
        "title":"Login", "origins":["https://example.com"], "usernameHint":"", "itemKind":"login", "totp":null,"passkey":null
    }, "username":"alice","password":"password","notes":"private note"}),
    );
    assert_eq!(added["ok"], true);
    let id = added["data"]["id"].clone();
    let notes = call(&mut core, maho_vault_get_notes_json, json!({"id":id}));
    assert_eq!(notes["data"]["notes"], "private note");
    assert_eq!(notes["data"]["username"], "alice");
    let favorite = call(
        &mut core,
        maho_vault_set_favorite_json,
        json!({"id":id,"expectedRevision":"1","favorite":true}),
    );
    assert_eq!(favorite["data"]["revision"], "2");
    let totp = call(
        &mut core,
        maho_vault_set_login_totp_json,
        json!({"id":id,"expectedRevision":"2","secret":"GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ"}),
    );
    assert_eq!(totp["data"]["revision"], "3");
    let code = call(&mut core, maho_vault_totp_code_json, json!({"id":id}));
    assert_eq!(code["data"]["code"].as_str().unwrap().len(), 6);
    assert_eq!(code["data"]["period"], 30);
    assert!((1..=30).contains(&code["data"]["secondsRemaining"].as_u64().unwrap()));
    let trash = call(
        &mut core,
        maho_vault_trash_item_json,
        json!({"id":id,"expectedRevision":"3"}),
    );
    assert_eq!(trash["data"]["revision"], "4");
    let restored = call(
        &mut core,
        maho_vault_restore_item_json,
        json!({"id":id,"expectedRevision":"4"}),
    );
    assert_eq!(restored["data"]["revision"], "5");
    let note = call(
        &mut core,
        maho_vault_add_secure_note_json,
        json!({"title":"Note","notes":"secret text"}),
    );
    let note_id = note["data"]["id"].clone();
    let changed = call(
        &mut core,
        maho_vault_update_secure_note_json,
        json!({"id":note_id,"expectedRevision":"1","title":"Renamed","notes":"new text"}),
    );
    assert_eq!(changed["data"]["revision"], "2");
    let trash_note = call(
        &mut core,
        maho_vault_trash_item_json,
        json!({"id":note_id,"expectedRevision":"2"}),
    );
    assert_eq!(trash_note["ok"], true);
    // SAFETY: Core is live and exclusively owned for these synchronous calls.
    let emptied = owned(unsafe { maho_vault_empty_trash_json(&mut core) });
    assert_eq!(emptied["data"]["count"], 1);
    // SAFETY: Core is live and exclusively owned for this synchronous call.
    let health = owned(unsafe { maho_vault_health_report_json(&mut core) });
    assert_eq!(health["data"]["totalLogins"], 1);
    assert_eq!(health["data"]["weak"], json!([id]));
    core.lock_vault().unwrap();
    assert_eq!(
        call(&mut core, maho_vault_get_notes_json, json!({"id":id}))["ok"],
        false
    );
    let generated = call(
        &mut core,
        maho_vault_generate_password_json,
        json!({"length":24}),
    );
    assert_eq!(generated["data"]["password"].as_str().unwrap().len(), 24);
    assert!(generated["data"]["entropyBits"].is_number());
    let strength = call(
        &mut core,
        maho_vault_password_strength_json,
        json!({"password":"password"}),
    );
    assert!(strength["data"]["score"].as_u64().unwrap() <= 1);
}
