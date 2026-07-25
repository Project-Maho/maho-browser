use std::ffi::{CStr, CString};
use std::path::PathBuf;

use maho_core::maho_core::MahoCore;
use serde_json::Value;

const MASTER: &str = "correct horse battery staple";
const RECOVERY: &str = "policy-ffi-recovery";
const SECRET_SENTINEL: &str = "S3NTINEL-maho-vault-policy-ffi";

struct TestCore {
    core: *mut MahoCore,
    profile: PathBuf,
}

impl TestCore {
    fn new() -> Self {
        maho_storage::sqlite::set_sqlcipher_key("vault-policy-ffi-test-key")
            .expect("configure SQLCipher key");
        let profile =
            std::env::temp_dir().join(format!("maho-vault-policy-{}", uuid::Uuid::new_v4()));
        std::fs::create_dir_all(&profile).expect("create test profile");
        let database = profile.join("vault.sqlite");
        let database = database.to_str().expect("temporary path is UTF-8");
        let core = Box::into_raw(Box::new(MahoCore::new().with_storage(database)));
        Self { core, profile }
    }

    fn without_storage() -> Self {
        let profile = std::env::temp_dir().join(format!(
            "maho-vault-policy-no-store-{}",
            uuid::Uuid::new_v4()
        ));
        std::fs::create_dir_all(&profile).expect("create test profile");
        let core = Box::into_raw(Box::new(MahoCore::new()));
        Self { core, profile }
    }
}

impl Drop for TestCore {
    fn drop(&mut self) {
        if !self.core.is_null() {
            unsafe { maho_ffi::maho_core_free(self.core) };
            self.core = std::ptr::null_mut();
        }
        let _ = std::fs::remove_dir_all(&self.profile);
    }
}

fn request(value: Value) -> CString {
    CString::new(value.to_string()).expect("test JSON contains no NUL")
}

fn read_owned_json(pointer: *mut std::ffi::c_char) -> Value {
    assert!(!pointer.is_null(), "Vault JSON ABI returned null");
    let raw = unsafe { CStr::from_ptr(pointer) }
        .to_str()
        .expect("Vault JSON ABI returns UTF-8")
        .to_owned();
    unsafe { maho_ffi::maho_string_free(pointer) };
    serde_json::from_str(&raw).expect("Vault JSON ABI returns JSON")
}

fn initialize(core: *mut MahoCore) {
    let init = request(serde_json::json!({
        "masterPassphrase": MASTER,
        "recoverySecret": RECOVERY,
    }));
    let response =
        read_owned_json(unsafe { maho_ffi::maho_vault_initialize_json(core, init.as_ptr()) });
    assert_eq!(response["ok"], true);
}

fn set_policy(core: *mut MahoCore, policy: &str) -> Value {
    let request = request(serde_json::json!({
        "schemaVersion": 1,
        "policy": policy,
        "itemId": null,
        "origin": null,
        "expiresAt": null,
    }));
    read_owned_json(unsafe { maho_ffi::maho_vault_set_policy_json(core, request.as_ptr()) })
}

#[test]
fn vault_policy_ffi_persists_and_reports_absent_storage_honestly() {
    let fixture = TestCore::new();
    initialize(fixture.core);

    let saved = set_policy(fixture.core, "while_unlocked");
    assert_eq!(saved["ok"], true);
    assert_eq!(saved["data"]["policy"], "while_unlocked");
    let loaded = read_owned_json(unsafe { maho_ffi::maho_vault_get_policy_json(fixture.core) });
    assert_eq!(loaded["ok"], true);
    assert_eq!(loaded["data"]["policy"], "while_unlocked");

    let no_storage = TestCore::without_storage();
    let unavailable =
        read_owned_json(unsafe { maho_ffi::maho_vault_get_policy_json(no_storage.core) });
    assert_eq!(unavailable["ok"], false);
    assert_eq!(unavailable["error"]["code"], "storage_failure");
}

#[test]
fn vault_audit_page_ffi_returns_real_sanitized_records_with_pagination() {
    let fixture = TestCore::new();
    initialize(fixture.core);
    set_policy(fixture.core, "ask_every_use");
    let add = request(serde_json::json!({
        "metadata": {
            "title": "Policy audit",
            "origins": ["https://policy-audit.example"],
            "usernameHint": "ignored-by-core",
            "itemKind": "login",
            "totp": null,
            "passkey": null
        },
        "username": "person@example.test",
        "password": SECRET_SENTINEL
    }));
    let added =
        read_owned_json(unsafe { maho_ffi::maho_vault_add_login_json(fixture.core, add.as_ptr()) });
    assert_eq!(added["ok"], true);
    let item_id = added["data"]["id"].as_str().unwrap().to_string();

    let page_request = request(serde_json::json!({"schemaVersion": 1, "cursor": null, "limit": 1}));
    let first = read_owned_json(unsafe {
        maho_ffi::maho_vault_audit_page_json(fixture.core, page_request.as_ptr())
    });
    assert_eq!(first["ok"], true);
    assert_eq!(first["data"]["entries"].as_array().unwrap().len(), 1);
    assert!(first["data"]["nextCursor"].as_str().is_some());

    let rendered = first.to_string();
    assert!(!rendered.contains(SECRET_SENTINEL));
    assert!(!rendered.contains(&item_id));
    assert!(!rendered.contains("itemId"));
    assert!(!rendered.contains("itemAlias"));
}
