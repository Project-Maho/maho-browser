use super::VaultRuntime;
use crate::vault_manager::VaultManagerError;
use chrono::Utc;
use maho_types::vault::VaultItemId;
use std::sync::Arc;

fn assert_send_sync<T: Send + Sync>() {}

#[test]
fn vault_runtime_is_send_and_sync_without_an_unsafe_impl() {
    // Given the planned process-shared runtime boundary.
    // When its concurrency guarantees are checked at compile time.
    assert_send_sync::<VaultRuntime>();
    assert_send_sync::<Arc<VaultRuntime>>();
    // Then Arc<VaultRuntime> is safe to share across execution contexts.
}

#[test]
fn vault_runtime_facade_encrypts_decrypts_and_denies_after_lock() {
    // Given an initialized runtime and an item-bound payload.
    let runtime = VaultRuntime::new();
    runtime
        .initialize(b"master-secret", b"recovery-secret", Utc::now())
        .expect("runtime initialization succeeds");
    let item_id = VaultItemId::new();

    // When the payload is encrypted and then decrypted through the facade.
    let envelope = runtime
        .encrypt_and_wrap(b"item-bound payload", &item_id)
        .expect("runtime encryption succeeds");
    let plaintext = runtime
        .decrypt_record(&envelope, &item_id)
        .expect("runtime decryption succeeds");

    // Then it round-trips while unlocked and every crypto gate denies after lock.
    assert_eq!(plaintext.as_slice(), b"item-bound payload");
    runtime.lock().expect("runtime lock succeeds");
    assert!(matches!(
        runtime.ensure_unlocked(),
        Err(VaultManagerError::VaultLocked)
    ));
    assert!(matches!(
        runtime.encrypt_and_wrap(b"item-bound payload", &item_id),
        Err(VaultManagerError::VaultLocked)
    ));
}

#[test]
fn vault_runtime_initialization_persist_failure_reverts_before_unlocking_callers() {
    // Given an uninitialized runtime and a failed durable commit.
    let runtime = VaultRuntime::new();

    // When initialization reaches its persistence boundary.
    let result =
        runtime.initialize_and_persist(b"master-secret", b"recovery-secret", Utc::now(), |_| {
            Err(VaultManagerError::Storage("injected failure".to_string()))
        });

    // Then the generated key material is discarded before another caller can observe it.
    assert!(matches!(result, Err(VaultManagerError::Storage(_))));
    assert_eq!(
        runtime.status().lock_state,
        maho_types::vault::VaultLockState::Uninitialized
    );
}
