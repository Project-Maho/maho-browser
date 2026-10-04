//! Cross-device Vault item sync (workstream W3): opaque ciphertext envelopes
//! travel through the snapshot path, and a second device applies them without
//! ever holding the plaintext secrets.

use base64::Engine as _;
use maho_core::maho_core::MahoCore;
use maho_core::vault_manager::VaultLoginInput;
use maho_types::vault::{CredentialOrigin, VaultItemKind, VaultItemPublicMetadata};
use zeroize::Zeroizing;

const SEED: &[u8; 32] = b"escrow-account-seed-0123456789ab";

fn storage_backed_core(path: &std::path::Path) -> MahoCore {
    maho_storage::sqlite::set_sqlcipher_key("vault-item-sync-test-key").expect("configure key");
    MahoCore::new().with_storage(path.to_str().expect("utf8 path"))
}

fn configure_sync(core: &mut MahoCore) {
    let seed_b64 = base64::engine::general_purpose::STANDARD.encode(SEED);
    core.configure_sync_encryption_from_bootstrap("https://relay.test", &seed_b64)
        .expect("configure sync");
}

fn metadata(title: &str) -> VaultItemPublicMetadata {
    VaultItemPublicMetadata {
        favorite: false,
        trashed_at: None,
        has_notes: false,
        title: title.to_string(),
        origins: vec![CredentialOrigin::try_from("https://sync.example.com").expect("origin")],
        username_hint: "user".to_string(),
        item_kind: VaultItemKind::Login,
        totp: None,
        passkey: None,
    }
}

fn login_input(title: &str, username: &str, secret: &str) -> VaultLoginInput {
    VaultLoginInput {
        metadata: metadata(title),
        username: username.to_string(),
        password: Zeroizing::new(secret.to_string()),
        form_details: None,
    }
}

#[test]
fn vault_items_travel_as_opaque_envelopes_between_devices() {
    let dir = tempfile::tempdir().expect("tempdir");
    let mut device_a = storage_backed_core(&dir.path().join("device-a.sqlite"));
    configure_sync(&mut device_a);
    let item = device_a
        .vault_add_login(login_input("GitHub", "alice", "hunter2-sync"))
        .expect("add login on A");
    let item_id = item.id.to_string();

    let storage_a = device_a.storage_ref().expect("storage A");
    let row_a = storage_a
        .get_vault_item(&item_id)
        .expect("read A")
        .expect("row A exists");

    let outbox = storage_a
        .load_pending_sync_entities()
        .expect("pending sync entities");
    assert!(
        outbox.iter().any(|(_, entity_type, entity_id, ..)| {
            entity_type == "vaultitem" && entity_id == &item_id
        }),
        "a local vault item mutation must enqueue a vaultitem sync push"
    );

    let snapshot = device_a.export_sync_snapshot().expect("export snapshot");
    assert!(
        !snapshot.contains("hunter2-sync"),
        "the sync snapshot must not contain plaintext vault secrets"
    );

    let mut device_b = storage_backed_core(&dir.path().join("device-b.sqlite"));
    configure_sync(&mut device_b);
    device_b
        .apply_sync_snapshot(&snapshot)
        .expect("apply snapshot on B");

    let storage_b = device_b.storage_ref().expect("storage B");
    let row_b = storage_b
        .get_vault_item(&item_id)
        .expect("read B")
        .expect("adopted row exists");
    assert_eq!(
        row_b.envelope, row_a.envelope,
        "the opaque envelope must travel verbatim"
    );
    assert_eq!(row_b.provider, row_a.provider);
    assert_eq!(row_b.item_kind, row_a.item_kind);
    assert_eq!(row_b.revision, row_a.revision);
}

#[test]
fn remote_vault_item_deletion_propagates_as_a_tombstone() {
    let dir = tempfile::tempdir().expect("tempdir");
    let mut device_a = storage_backed_core(&dir.path().join("delete-a.sqlite"));
    configure_sync(&mut device_a);
    let item = device_a
        .vault_add_login(login_input("Deleted", "bob", "secret"))
        .expect("add login on A");
    let item_id = item.id.to_string();

    let mut device_b = storage_backed_core(&dir.path().join("delete-b.sqlite"));
    configure_sync(&mut device_b);
    let first = device_a.export_sync_snapshot().expect("export initial snapshot");
    device_b
        .apply_sync_snapshot(&first)
        .expect("apply initial snapshot");
    assert!(
        device_b
            .storage_ref()
            .expect("storage B")
            .get_vault_item(&item_id)
            .expect("read B")
            .is_some(),
        "the item must reach device B before deletion"
    );

    device_a
        .vault_delete_item(item.id, item.revision)
        .expect("delete on A");
    let second = device_a
        .export_sync_snapshot()
        .expect("export post-delete snapshot");
    device_b
        .apply_sync_snapshot(&second)
        .expect("apply post-delete snapshot");

    let row_b = device_b
        .storage_ref()
        .expect("storage B")
        .get_vault_item(&item_id)
        .expect("read B")
        .expect("tombstone row exists");
    assert!(
        row_b.deleted_at.is_some(),
        "a deleted item must arrive as a tombstone, not vanish silently"
    );
}
