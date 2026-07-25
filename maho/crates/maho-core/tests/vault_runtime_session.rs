use maho_core::maho_core::MahoCore;
use maho_core::vault_manager::{VaultCrudError, VaultLoginInput};
use maho_core::vault_runtime::session::VaultBackendSessionError;
use maho_types::vault::{
    CredentialOrigin, VaultBatchReadRequest, VaultItemKind, VaultItemPublicMetadata,
    VaultSchemaVersion,
};
use zeroize::Zeroizing;

const MASTER: &[u8] = b"correct horse battery staple";
const RECOVERY: &[u8] = b"recovery-emergency-phrase-deterministic";
const SENTINEL: &str = "S3NTINEL-maho-vault-9F4C";

fn assert_send_sync<T: Send + Sync>() {}

fn unlocked_core(dir: &tempfile::TempDir) -> MahoCore {
    maho_storage::sqlite::set_sqlcipher_key("vault-backend-batch-test-key").expect("configure key");
    let path = dir.path().join("vault-backend-batch.sqlite");
    let mut core = MahoCore::new().with_storage(path.to_str().expect("utf8 path"));
    core.initialize_vault(MASTER, RECOVERY)
        .expect("initialize vault");
    core
}

fn login(title: &str, origin: &str, username: &str) -> VaultLoginInput {
    VaultLoginInput {
        metadata: VaultItemPublicMetadata {
            title: title.to_string(),
            origins: vec![CredentialOrigin::try_from(origin).expect("valid test origin")],
            username_hint: String::new(),
            item_kind: VaultItemKind::Login,
            totp: None,
            passkey: None,
        },
        username: username.to_string(),
        password: Zeroizing::new(SENTINEL.to_string()),
    }
}

#[test]
fn vault_backend_session_moves_to_a_worker_and_reads_without_moving_core() {
    // Given an unlocked core with a persisted Vault item and a backend session snapshot.
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    let item = core
        .vault_add_login(login("Worker", "https://worker.example", "worker"))
        .expect("add item");
    let session = core
        .create_vault_backend_session()
        .expect("create backend session");
    let request = VaultBatchReadRequest {
        schema_version: VaultSchemaVersion::CURRENT,
        item_ids: vec![item.id],
    };

    // When the session, rather than MahoCore, is moved to a worker thread.
    assert_send_sync::<maho_core::vault_runtime::session::VaultBackendSession>();
    let result = std::thread::spawn(move || session.batch_read(&request))
        .join()
        .expect("worker must not panic")
        .expect("worker batch read");

    // Then the worker receives the same secret-free public DTO through the runtime seam.
    assert_eq!(result.items.len(), 1);
    assert_eq!(result.items[0].id, item.id);
}

#[test]
fn vault_backend_session_fails_closed_after_core_drop() {
    // Given a session snapshot created from an unlocked core.
    let dir = tempfile::tempdir().expect("temporary directory");
    let core = unlocked_core(&dir);
    let session = core
        .create_vault_backend_session()
        .expect("create backend session");
    let request = VaultBatchReadRequest {
        schema_version: VaultSchemaVersion::CURRENT,
        item_ids: vec![],
    };

    // When its owning core and runtime are dropped.
    drop(core);
    let result = session.batch_read(&request);

    // Then the weak runtime snapshot reports a typed unavailable error.
    assert!(matches!(
        result,
        Err(VaultBackendSessionError::RuntimeUnavailable)
    ));
}

#[test]
fn vault_backend_session_denies_batch_reads_after_vault_lock() {
    // Given a session snapshot created while the Vault is unlocked.
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    let session = core
        .create_vault_backend_session()
        .expect("create backend session");
    let request = VaultBatchReadRequest {
        schema_version: VaultSchemaVersion::CURRENT,
        item_ids: vec![],
    };

    // When the core locks the runtime after session creation.
    core.lock_vault().expect("lock vault");
    let result = session.batch_read(&request);

    // Then the session performs no backend access and returns the existing lock error.
    assert!(matches!(
        result,
        Err(VaultBackendSessionError::Batch(VaultCrudError::Locked))
    ));
}
