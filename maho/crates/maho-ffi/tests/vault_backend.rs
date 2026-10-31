use std::ffi::CString;
use std::ptr;

use maho_core::maho_core::MahoCore;
use maho_core::vault_manager::VaultLoginInput;
use maho_ffi::vault_backend::{
    maho_vault_backend_buffer_data, maho_vault_backend_buffer_free, maho_vault_backend_buffer_len,
    maho_vault_backend_result_consume, maho_vault_backend_result_free,
    maho_vault_backend_result_status, maho_vault_backend_session_close,
    maho_vault_backend_session_execute, maho_vault_backend_session_execute_credentials,
    maho_vault_backend_session_free, maho_vault_backend_session_new, MahoVaultBackendResultStatus,
};
use maho_types::vault::{
    CredentialOrigin, VaultBatchReadRequest, VaultBatchReadResult, VaultItemKind,
    VaultItemPublicMetadata, VaultRevision, VaultSchemaVersion,
};
use zeroize::Zeroizing;

const MASTER: &[u8] = b"correct horse battery staple";
const RECOVERY: &[u8] = b"backend-session-recovery";
const SECRET: &str = "S3NTINEL-backend-session-secret";

struct TestCore {
    core: *mut MahoCore,
    profile: std::path::PathBuf,
}

impl TestCore {
    fn new() -> Self {
        maho_storage::sqlite::set_sqlcipher_key("vault-backend-ffi-test-key")
            .expect("configure SQLCipher key");
        let profile =
            std::env::temp_dir().join(format!("maho-vault-backend-{}", uuid::Uuid::new_v4()));
        std::fs::create_dir_all(&profile).expect("create test profile");
        let database = profile.join("vault.sqlite");
        let database = database.to_str().expect("test path is UTF-8");
        let mut core = MahoCore::new().with_storage(database);
        core.initialize_vault(MASTER, RECOVERY)
            .expect("initialize vault");
        Self {
            core: Box::into_raw(Box::new(core)),
            profile,
        }
    }

    fn add_login(&mut self) -> maho_types::vault::VaultItemId {
        let origin = CredentialOrigin::try_from("https://backend.example").expect("valid origin");
        // SAFETY: this fixture exclusively owns the live core pointer.
        unsafe {
            (&mut *self.core)
                .vault_add_login(VaultLoginInput {
                    metadata: VaultItemPublicMetadata {
                        favorite: false,
                        trashed_at: None,
                        has_notes: false,
                        title: "Backend".to_string(),
                        origins: vec![origin],
                        username_hint: "backend@example.com".to_string(),
                        item_kind: VaultItemKind::Login,
                        totp: None,
                        passkey: None,
                    },
                    username: "backend@example.com".to_string(),
                    password: Zeroizing::new(SECRET.to_string()),
                    form_details: None,
                })
                .expect("add login")
                .id
        }
    }

    fn lock(&mut self) {
        // SAFETY: this fixture exclusively owns the live core pointer.
        unsafe { (&mut *self.core).lock_vault().expect("lock vault") };
    }

    fn drop_core(&mut self) {
        if !self.core.is_null() {
            // SAFETY: this fixture exclusively owns the live core pointer.
            unsafe { maho_ffi::maho_core_free(self.core) };
            self.core = ptr::null_mut();
        }
    }
}

impl Drop for TestCore {
    fn drop(&mut self) {
        self.drop_core();
        let _ = std::fs::remove_dir_all(&self.profile);
    }
}

fn request(item_ids: Vec<maho_types::vault::VaultItemId>) -> CString {
    let request = VaultBatchReadRequest {
        schema_version: VaultSchemaVersion::CURRENT,
        item_ids,
    };
    CString::new(serde_json::to_string(&request).expect("serialize request"))
        .expect("request has no NUL")
}

fn credential_discovery_request(origin: &str) -> CString {
    CString::new(serde_json::json!({ "origin": origin }).to_string())
        .expect("discovery request has no NUL")
}

fn secret_resolution_request(
    item_id: maho_types::vault::VaultItemId,
    expected_revision: VaultRevision,
    action: &str,
) -> CString {
    CString::new(
        serde_json::json!({
            "itemId": item_id,
            "expectedRevision": expected_revision.value().to_string(),
            "action": action,
        })
        .to_string(),
    )
    .expect("resolution request has no NUL")
}

unsafe fn consume_json(result: *mut maho_ffi::vault_backend::MahoVaultBackendResult) -> String {
    let buffer = maho_vault_backend_result_consume(result);
    assert!(
        !buffer.is_null(),
        "successful result must have a backend buffer"
    );
    let bytes = std::slice::from_raw_parts(
        maho_vault_backend_buffer_data(buffer),
        maho_vault_backend_buffer_len(buffer),
    );
    let json = std::str::from_utf8(bytes)
        .expect("backend buffer is UTF-8 JSON")
        .to_owned();
    maho_vault_backend_buffer_free(buffer);
    json
}

#[test]
fn vault_backend_null_safety_and_invalid_requests_are_typed() {
    let request = request(Vec::new());
    // SAFETY: null inputs are explicitly supported ABI boundary cases.
    unsafe {
        assert!(maho_vault_backend_session_new(ptr::null_mut()).is_null());
        assert!(maho_vault_backend_session_execute(ptr::null_mut(), request.as_ptr()).is_null());
    }

    let fixture = TestCore::new();
    // SAFETY: fixture owns a live core and the request is NUL-terminated.
    let session = unsafe { maho_vault_backend_session_new(fixture.core) };
    assert!(!session.is_null());
    // SAFETY: a null request is rejected without dereference.
    assert!(unsafe { maho_vault_backend_session_execute(session, ptr::null()) }.is_null());

    let invalid = CString::new("not JSON").expect("literal has no NUL");
    // SAFETY: session and invalid request are live for this call.
    let result = unsafe { maho_vault_backend_session_execute(session, invalid.as_ptr()) };
    assert_eq!(
        unsafe { maho_vault_backend_result_status(result) },
        MahoVaultBackendResultStatus::InvalidRequest
    );
    // SAFETY: invalid requests have no response payload and the result is consumed once.
    assert!(unsafe { maho_vault_backend_result_consume(result) }.is_null());
    // SAFETY: invalid result ownership is released once.
    unsafe { maho_vault_backend_result_free(result) };

    let invalid_utf8 = [0xff_u8 as i8, 0];
    // SAFETY: the bytes are NUL-terminated but intentionally not UTF-8.
    let result = unsafe { maho_vault_backend_session_execute(session, invalid_utf8.as_ptr()) };
    assert_eq!(
        unsafe { maho_vault_backend_result_status(result) },
        MahoVaultBackendResultStatus::InvalidRequest
    );
    // SAFETY: invalid result ownership is released once.
    unsafe { maho_vault_backend_result_free(result) };
    // SAFETY: session is exclusively owned by this test.
    unsafe { maho_vault_backend_session_free(session) };
}

#[test]
fn vault_backend_worker_batch_uses_dedicated_buffer_and_keeps_metadata_public() {
    let mut fixture = TestCore::new();
    let item_id = fixture.add_login();
    // SAFETY: fixture owns a live core for one snapshot creation call.
    let session = unsafe { maho_vault_backend_session_new(fixture.core) };
    let request = request(vec![item_id]);

    let session_address = session as usize;
    let result_address = std::thread::spawn(move || {
        // SAFETY: the opaque session is exclusively transferred to this worker; no MahoCore is captured.
        unsafe {
            maho_vault_backend_session_execute(
                session_address as *mut maho_ffi::vault_backend::MahoVaultBackendSession,
                request.as_ptr(),
            ) as usize
        }
    })
    .join()
    .expect("worker does not panic");
    let result = result_address as *mut maho_ffi::vault_backend::MahoVaultBackendResult;
    assert_eq!(
        unsafe { maho_vault_backend_result_status(result) },
        MahoVaultBackendResultStatus::Success
    );

    // SAFETY: the successful result is consumed exactly once through its dedicated buffer.
    let payload: VaultBatchReadResult =
        serde_json::from_str(&unsafe { consume_json(result) }).expect("parse backend batch result");
    assert_eq!(payload.items.len(), 1);
    assert_eq!(payload.items[0].id, item_id);
    assert_eq!(payload.items[0].revision, VaultRevision::new(1));
    assert!(payload.items[0].last_used_at.is_none());
    assert!(!serde_json::to_string(&payload)
        .expect("serialize public result")
        .contains(SECRET));
    // SAFETY: this fixture still exclusively owns its live core after the worker returns.
    let current = unsafe {
        (&*fixture.core)
            .vault_batch_read(&VaultBatchReadRequest {
                schema_version: VaultSchemaVersion::CURRENT,
                item_ids: vec![item_id],
            })
            .expect("read current public metadata")
    };
    assert_eq!(current.items[0].revision, VaultRevision::new(1));
    assert!(current.items[0].last_used_at.is_none());
    // SAFETY: consumed result ownership is released once.
    unsafe { maho_vault_backend_result_free(result) };

    // SAFETY: session is exclusively owned by this test.
    unsafe { maho_vault_backend_session_free(session) };
}

#[test]
fn vault_backend_credentials_discovery_is_secret_free_and_fill_resolution_is_exact() {
    let mut fixture = TestCore::new();
    let item_id = fixture.add_login();
    // SAFETY: fixture owns a live core for one snapshot creation call.
    let session = unsafe { maho_vault_backend_session_new(fixture.core) };

    let discovery = credential_discovery_request("https://backend.example");
    // SAFETY: session and request are valid for this call.
    let result =
        unsafe { maho_vault_backend_session_execute_credentials(session, discovery.as_ptr()) };
    assert_eq!(
        unsafe { maho_vault_backend_result_status(result) },
        MahoVaultBackendResultStatus::Success
    );
    let payload = unsafe { consume_json(result) };
    let metadata: serde_json::Value = serde_json::from_str(&payload).expect("parse metadata");
    let descriptor = metadata
        .as_array()
        .and_then(|items| items.first())
        .and_then(serde_json::Value::as_object)
        .expect("one credential descriptor");
    assert_eq!(descriptor["itemId"], item_id.to_string());
    assert_eq!(descriptor["observedRevision"], "1");
    assert_eq!(descriptor["username"], "backend@example.com");
    assert!(!descriptor.contains_key("password"));
    assert!(!descriptor.contains_key("secret"));
    assert!(!payload.contains(SECRET));
    // SAFETY: consumed result ownership is released once.
    unsafe { maho_vault_backend_result_free(result) };

    let resolve = secret_resolution_request(item_id, VaultRevision::new(1), "fill");
    // SAFETY: exact item identity, observed revision, and fill action are valid.
    let result =
        unsafe { maho_vault_backend_session_execute_credentials(session, resolve.as_ptr()) };
    assert_eq!(
        unsafe { maho_vault_backend_result_status(result) },
        MahoVaultBackendResultStatus::Success
    );
    let resolved: serde_json::Value =
        serde_json::from_str(&unsafe { consume_json(result) }).expect("parse resolved secret");
    assert_eq!(resolved["password"], SECRET);
    // SAFETY: consumed result ownership is released once.
    unsafe { maho_vault_backend_result_free(result) };

    // The successful fill advanced the item revision, so replaying the observed
    // discovery revision is stale and must not return a buffer.
    let stale = secret_resolution_request(item_id, VaultRevision::new(1), "fill");
    let result = unsafe { maho_vault_backend_session_execute_credentials(session, stale.as_ptr()) };
    assert_eq!(
        unsafe { maho_vault_backend_result_status(result) },
        MahoVaultBackendResultStatus::Failed
    );
    assert!(unsafe { maho_vault_backend_result_consume(result) }.is_null());
    unsafe { maho_vault_backend_result_free(result) };

    let wrong_item = secret_resolution_request(
        maho_types::vault::VaultItemId::new(),
        VaultRevision::new(1),
        "fill",
    );
    let result =
        unsafe { maho_vault_backend_session_execute_credentials(session, wrong_item.as_ptr()) };
    assert_eq!(
        unsafe { maho_vault_backend_result_status(result) },
        MahoVaultBackendResultStatus::Failed
    );
    assert!(unsafe { maho_vault_backend_result_consume(result) }.is_null());
    unsafe { maho_vault_backend_result_free(result) };

    let non_fill = secret_resolution_request(item_id, VaultRevision::new(2), "copy");
    let result =
        unsafe { maho_vault_backend_session_execute_credentials(session, non_fill.as_ptr()) };
    assert_eq!(
        unsafe { maho_vault_backend_result_status(result) },
        MahoVaultBackendResultStatus::InvalidRequest
    );
    assert!(unsafe { maho_vault_backend_result_consume(result) }.is_null());
    unsafe { maho_vault_backend_result_free(result) };

    // SAFETY: session is exclusively owned by this test.
    unsafe { maho_vault_backend_session_free(session) };
}

#[test]
fn vault_backend_result_fails_closed_after_close_or_lock() {
    let mut fixture = TestCore::new();
    let item_id = fixture.add_login();
    // SAFETY: fixture owns a live core for one snapshot creation call.
    let session = unsafe { maho_vault_backend_session_new(fixture.core) };
    let request = request(vec![item_id]);
    // SAFETY: inputs are valid for this call.
    let result = unsafe { maho_vault_backend_session_execute(session, request.as_ptr()) };
    fixture.lock();
    // SAFETY: consumption revalidates the still-open session and rejects a locked runtime.
    assert!(unsafe { maho_vault_backend_result_consume(result) }.is_null());
    assert_eq!(
        unsafe { maho_vault_backend_result_status(result) },
        MahoVaultBackendResultStatus::Locked
    );
    // SAFETY: the rejected result remains caller-owned until freed.
    unsafe { maho_vault_backend_result_free(result) };

    let result = unsafe { maho_vault_backend_session_execute(session, request.as_ptr()) };
    assert_eq!(
        unsafe { maho_vault_backend_result_status(result) },
        MahoVaultBackendResultStatus::Locked
    );
    // SAFETY: error result ownership is released once.
    unsafe { maho_vault_backend_result_free(result) };
    // SAFETY: close invalidates all future session operations.
    unsafe { maho_vault_backend_session_close(session) };
    assert!(unsafe { maho_vault_backend_session_execute(session, request.as_ptr()) }.is_null());
    // SAFETY: session is exclusively owned by this test.
    unsafe { maho_vault_backend_session_free(session) };
}

#[test]
fn vault_backend_session_fails_closed_after_core_drop() {
    let mut fixture = TestCore::new();
    // SAFETY: fixture owns a live core for one snapshot creation call.
    let session = unsafe { maho_vault_backend_session_new(fixture.core) };
    fixture.drop_core();
    let request = request(Vec::new());
    // SAFETY: the opaque session remains valid after its core is dropped.
    let result = unsafe { maho_vault_backend_session_execute(session, request.as_ptr()) };
    assert_eq!(
        unsafe { maho_vault_backend_result_status(result) },
        MahoVaultBackendResultStatus::RuntimeUnavailable
    );
    // SAFETY: unavailable result ownership is released once.
    unsafe { maho_vault_backend_result_free(result) };
    // SAFETY: session is exclusively owned by this test.
    unsafe { maho_vault_backend_session_free(session) };
}

#[test]
fn vault_backend_owned_sources_do_not_reference_legacy_password_ffi() {
    let backend_sources = [
        include_str!("../src/vault_backend.rs"),
        include_str!("../src/vault_backend/result.rs"),
        include_str!("vault_backend.rs"),
        include_str!("null_safety.rs"),
    ];
    let legacy_suffixes = [
        "search_passwords",
        "reveal_password",
        "add_password",
        "update_password_username",
        "delete_password",
    ];

    for legacy_suffix in legacy_suffixes {
        let legacy_symbol = format!("maho_core_{legacy_suffix}");
        for source in backend_sources {
            assert!(
                !source.contains(&legacy_symbol),
                "backend-owned source must not reference {legacy_symbol}",
            );
        }
    }
}
