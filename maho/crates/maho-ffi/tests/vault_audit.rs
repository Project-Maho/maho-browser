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
        maho_storage::sqlite::set_sqlcipher_key("vault-audit-ffi-test-key")
            .expect("configure SQLCipher key");
        let profile =
            std::env::temp_dir().join(format!("maho-vault-audit-{}", uuid::Uuid::new_v4()));
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
    read_owned_json(unsafe { maho_ffi::maho_vault_add_login_json(core, add.as_ptr()) })
}

fn audit_rows(core: *mut MahoCore) -> Vec<maho_storage::sqlite::VaultAuditRow> {
    assert!(!core.is_null(), "core pointer is live");
    // SAFETY: TestCore owns this pointer for the duration of the test.
    let core = unsafe { &*core };
    core.storage_ref()
        .expect("storage configured")
        .list_vault_audit_events()
        .expect("read vault audit rows")
}

fn assert_audit_rows_are_payload_safe(
    rows: &[maho_storage::sqlite::VaultAuditRow],
    forbidden_item_ids: &[String],
) {
    let rendered = format!("{rows:?}");
    assert!(
        !rendered.contains(SECRET_SENTINEL),
        "audit rows must not contain the secret sentinel"
    );
    for item_id in forbidden_item_ids {
        assert!(
            !rendered.contains(item_id),
            "audit rows must not contain Vault item id {item_id}"
        );
    }
    for row in rows {
        assert!(row.item_id.is_none(), "audit item_id must stay empty");
        assert!(row.item_alias.is_none(), "audit item_alias must stay empty");
    }
}

#[test]
fn vault_library_ffi_operations_emit_secret_free_audit_rows() {
    let fixture = TestCore::new();
    initialize(fixture.core);
    let added = add_login(fixture.core);
    assert_eq!(added["ok"], true);
    let item_id = added["data"]["id"]
        .as_str()
        .expect("added item id")
        .to_string();
    let use_secret = request(serde_json::json!({
        "itemId": added["data"]["id"],
        "expectedRevision": added["data"]["revision"],
        "action": "fill",
    }));

    let used = read_owned_json(unsafe {
        maho_ffi::maho_vault_use_secret_json(fixture.core, use_secret.as_ptr())
    });

    assert_eq!(used["ok"], true);
    let rows = audit_rows(fixture.core);
    assert_eq!(
        rows.iter()
            .map(|row| row.operation.as_str())
            .collect::<Vec<_>>(),
        vec!["item_created", "fill"]
    );
    assert!(rows.iter().all(|row| row.decision == "allowed"));
    assert_audit_rows_are_payload_safe(&rows, &[item_id]);
}
