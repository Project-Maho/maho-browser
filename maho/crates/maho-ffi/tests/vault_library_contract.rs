use std::ffi::{CStr, CString};
use std::path::PathBuf;

use maho_core::maho_core::MahoCore;
use serde_json::Value;

const MASTER: &str = "correct horse battery staple";
const RECOVERY: &str = "library-contract-recovery";
const SECRET_SENTINEL: &str = "S3NTINEL-maho-vault-library-ffi";

struct TestCore {
    core: *mut MahoCore,
    profile: PathBuf,
}

impl TestCore {
    fn new() -> Self {
        maho_storage::sqlite::set_sqlcipher_key("vault-library-ffi-test-key")
            .expect("configure SQLCipher key");
        let profile =
            std::env::temp_dir().join(format!("maho-vault-library-{}", uuid::Uuid::new_v4()));
        std::fs::create_dir_all(&profile).expect("create test profile");
        let database = profile.join("vault.sqlite");
        let database = database.to_str().expect("temporary path is UTF-8");
        let core = Box::into_raw(Box::new(MahoCore::new().with_storage(database)));
        Self { core, profile }
    }
}

impl Drop for TestCore {
    fn drop(&mut self) {
        if !self.core.is_null() {
            // SAFETY: this fixture owns the core pointer exactly once.
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
    // SAFETY: Vault JSON ABI returns a NUL-terminated string owned by the caller.
    let raw = unsafe { CStr::from_ptr(pointer) }
        .to_str()
        .expect("Vault JSON ABI returns UTF-8")
        .to_owned();
    // SAFETY: this test consumes the returned allocation exactly once.
    unsafe { maho_ffi::maho_string_free(pointer) };
    serde_json::from_str(&raw).expect("Vault JSON ABI returns JSON")
}

fn initialize(core: *mut MahoCore) {
    let init = request(serde_json::json!({
        "masterPassphrase": MASTER,
        "recoverySecret": RECOVERY,
    }));
    // SAFETY: the fixture core and request are live for this FFI call.
    let response =
        read_owned_json(unsafe { maho_ffi::maho_vault_initialize_json(core, init.as_ptr()) });
    assert_eq!(response["ok"], true);
}

fn add_login(core: *mut MahoCore) -> Value {
    let add = request(serde_json::json!({
        "metadata": {
            "title": "Library account",
            "origins": ["https://library.example"],
            "usernameHint": "ignored-by-core",
            "itemKind": "login",
            "totp": null,
            "passkey": null,
        },
        "username": "person@example.test",
        "password": SECRET_SENTINEL,
    }));
    // SAFETY: the fixture core and request are live for this FFI call.
    read_owned_json(unsafe { maho_ffi::maho_vault_add_login_json(core, add.as_ptr()) })
}

fn list_request() -> CString {
    request(serde_json::json!({
        "schemaVersion": 1,
        "provider": null,
        "kinds": [],
        "cursor": null,
        "limit": 50,
    }))
}

fn search_request() -> CString {
    request(serde_json::json!({
        "schemaVersion": 1,
        "origin": "https://library.example",
        "provider": null,
        "kinds": [],
    }))
}

fn assert_metadata_only_json(value: &Value) {
    assert!(
        !value.to_string().contains(SECRET_SENTINEL),
        "public Vault JSON leaked a secret sentinel"
    );
    assert_no_secret_keys(value);
}

fn assert_no_secret_keys(value: &Value) {
    match value {
        Value::Object(object) => {
            for (key, nested) in object {
                assert!(
                    !matches!(
                        key.as_str(),
                        "password"
                            | "username"
                            | "secret"
                            | "encryptedPayload"
                            | "envelope"
                            | "ciphertext"
                            | "nonce"
                            | "tag"
                            | "privateKey"
                            | "seed"
                    ),
                    "public Vault JSON carried a secret-bearing field"
                );
                assert_no_secret_keys(nested);
            }
        }
        Value::Array(items) => {
            for item in items {
                assert_no_secret_keys(item);
            }
        }
        Value::Null | Value::Bool(_) | Value::Number(_) | Value::String(_) => {}
    }
}

fn read_owned_secret(pointer: *mut std::ffi::c_char) -> String {
    assert!(!pointer.is_null(), "Vault secret buffer ABI returned null");
    // SAFETY: Vault secret-buffer ABI returns a NUL-terminated string owned by this test.
    let secret = unsafe { CStr::from_ptr(pointer) }
        .to_str()
        .expect("Vault secret buffer ABI returns UTF-8")
        .to_owned();
    // SAFETY: this test consumes the secret buffer exactly once.
    unsafe { maho_ffi::maho_vault_free_secret_buffer(pointer) };
    secret
}

// --- Regression lock: FFI list/search JSON remain metadata-only ------------

#[test]
fn vault_library_lock_ffi_crud_and_queries_return_metadata_only_json() {
    // Given the profile-owned Vault JSON ABI is initialized and a secret is stored.
    let fixture = TestCore::new();
    initialize(fixture.core);
    let added = add_login(fixture.core);
    assert_eq!(added["ok"], true);
    assert_metadata_only_json(&added["data"]);

    // When library-facing add/list/search/update/delete operations cross FFI.
    let list = list_request();
    // SAFETY: the fixture core and request are live for this FFI call.
    let listed = read_owned_json(unsafe {
        maho_ffi::maho_vault_list_items_json(fixture.core, list.as_ptr())
    });
    let search = search_request();
    // SAFETY: the fixture core and request are live for this FFI call.
    let searched = read_owned_json(unsafe {
        maho_ffi::maho_vault_search_items_json(fixture.core, search.as_ptr())
    });
    let update = request(serde_json::json!({
        "id": added["data"]["id"],
        "expectedRevision": added["data"]["revision"],
        "metadata": {
            "title": "Library account renamed",
            "origins": ["https://library.example"],
            "usernameHint": "ignored-by-core",
            "itemKind": "login",
            "totp": null,
            "passkey": null,
        },
        "username": "renamed@example.test",
        "password": SECRET_SENTINEL,
    }));
    // SAFETY: the fixture core and request are live for this FFI call.
    let updated = read_owned_json(unsafe {
        maho_ffi::maho_vault_update_login_json(fixture.core, update.as_ptr())
    });
    let delete = request(serde_json::json!({
        "id": updated["data"]["id"],
        "expectedRevision": updated["data"]["revision"],
    }));
    // SAFETY: the fixture core and request are live for this FFI call.
    let deleted = read_owned_json(unsafe {
        maho_ffi::maho_vault_delete_item_json(fixture.core, delete.as_ptr())
    });

    // Then every observable JSON response is metadata-only.
    assert_eq!(listed["data"].as_array().expect("list data").len(), 1);
    assert_eq!(searched["data"].as_array().expect("search data").len(), 1);
    assert_eq!(updated["ok"], true);
    assert_eq!(deleted["ok"], true);
    assert_metadata_only_json(&listed);
    assert_metadata_only_json(&searched);
    assert_metadata_only_json(&updated["data"]);
    assert_metadata_only_json(&deleted["data"]);
}

#[test]
fn vault_library_use_secret_json_is_status_only_and_buffer_is_explicit() {
    // Given the profile-owned Vault JSON ABI stores one login.
    let fixture = TestCore::new();
    initialize(fixture.core);
    let added = add_login(fixture.core);
    assert_eq!(added["ok"], true);
    let use_secret = request(serde_json::json!({
        "itemId": added["data"]["id"],
        "expectedRevision": added["data"]["revision"],
        "action": "fill",
    }));

    // When the status-only ABI consumes the secret.
    // SAFETY: the fixture core and request are live for this FFI call.
    let used = read_owned_json(unsafe {
        maho_ffi::maho_vault_use_secret_json(fixture.core, use_secret.as_ptr())
    });

    // Then the JSON response contains no plaintext or secret-bearing fields.
    assert_eq!(used["ok"], true);
    assert_metadata_only_json(&used);

    let list = list_request();
    // SAFETY: the fixture core and request are live for this FFI call.
    let listed = read_owned_json(unsafe {
        maho_ffi::maho_vault_list_items_json(fixture.core, list.as_ptr())
    });
    let latest_revision = listed["data"][0]["revision"]
        .as_u64()
        .expect("use_secret increments revision");
    let raw_request = request(serde_json::json!({
        "itemId": added["data"]["id"],
        "expectedRevision": latest_revision,
        "action": "copy",
    }));

    // When the browser-process-only raw buffer ABI is used.
    // SAFETY: the fixture core and request are live for this FFI call.
    let secret = read_owned_secret(unsafe {
        maho_ffi::maho_vault_use_secret_buffer_json(fixture.core, raw_request.as_ptr())
    });

    // Then plaintext is available only through the explicit buffer path.
    assert_eq!(secret, SECRET_SENTINEL);
}
