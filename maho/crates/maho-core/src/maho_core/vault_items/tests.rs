//! Crate-internal unit tests for the `#[cfg(test)]` post-write item-persist
//! fault seam. These live inside the lib build (where the fault field exists) and
//! prove that an injected failure AFTER the candidate write rolls the transaction
//! back with no memory/DB divergence, including after reload. All names select
//! with `vault_crud`.

use maho_types::vault::{
    CredentialOrigin, VaultItemKind, VaultItemPublicMetadata, VaultRevision, VaultSchemaVersion,
};
use zeroize::Zeroizing;

use crate::maho_core::MahoCore;
use crate::vault_manager::{VaultCrudError, VaultLoginInput, VaultLoginUpdate};

const MASTER: &[u8] = b"correct horse battery staple";
const RECOVERY: &[u8] = b"recovery-emergency-phrase-deterministic";
const SENTINEL: &str = "S3NTINEL-maho-vault-9F4C";

fn login_meta(origin: &str) -> VaultItemPublicMetadata {
    VaultItemPublicMetadata {
        favorite: false,
        trashed_at: None,
        has_notes: false,
        title: "Site".to_string(),
        origins: vec![CredentialOrigin::try_from(origin).expect("origin")],
        username_hint: String::new(),
        item_kind: VaultItemKind::Login,
        totp: None,
        passkey: None,
    }
}

fn login_input(origin: &str, username: &str, password: &str) -> VaultLoginInput {
    VaultLoginInput {
        metadata: login_meta(origin),
        username: username.to_string(),
        password: Zeroizing::new(password.to_string()),
        form_details: None,
    }
}

fn fault_core(dir: &tempfile::TempDir, name: &str) -> MahoCore {
    crate::install_test_sqlcipher_key();
    let path = dir.path().join(name);
    let mut core = MahoCore::new().with_storage(path.to_str().expect("utf8"));
    core.initialize_vault(MASTER, RECOVERY).expect("init vault");
    core
}

#[test]
fn vault_crud_injected_persist_fault_rolls_back_and_state_is_identical_after_reload() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = fault_core(&dir, "fault.sqlite");
    let dto = core
        .vault_add_login(login_input("https://fault.example", "alice", SENTINEL))
        .expect("add login");
    let path = core.sqlite_db_path().unwrap().to_string();
    let before = core
        .storage_ref()
        .unwrap()
        .get_vault_item(&dto.id.to_string())
        .unwrap()
        .unwrap();

    core.vault_item_persist_fault = true;

    let mut update_meta = login_meta("https://fault2.example");
    update_meta.item_kind = VaultItemKind::Login;
    let update = core.vault_update_login(
        dto.id,
        VaultRevision::new(1),
        VaultLoginUpdate {
            notes: None,
            metadata: update_meta,
            username: "alice".to_string(),
            password: Some(Zeroizing::new("newpw".to_string())),
            form_details: None,
        },
    );
    assert!(matches!(update, Err(VaultCrudError::Storage(_))));
    assert!(matches!(
        core.vault_use_login_password(dto.id),
        Err(VaultCrudError::Storage(_))
    ));
    assert!(matches!(
        core.vault_delete_item(dto.id, VaultRevision::new(1)),
        Err(VaultCrudError::Storage(_))
    ));

    core.vault_item_persist_fault = false;
    let after = core
        .storage_ref()
        .unwrap()
        .get_vault_item(&dto.id.to_string())
        .unwrap()
        .unwrap();
    assert_eq!(after.envelope, before.envelope, "no in-memory divergence");
    assert_eq!(after.revision, 1);
    drop(core);

    crate::install_test_sqlcipher_key();
    let mut reopened = MahoCore::new().with_storage(&path);
    reopened.load_persisted_data().unwrap();
    reopened.unlock_vault(MASTER).unwrap();
    let reload_row = reopened
        .storage_ref()
        .unwrap()
        .get_vault_item(&dto.id.to_string())
        .unwrap()
        .unwrap();
    assert_eq!(
        reload_row.envelope, before.envelope,
        "DB identical after reload"
    );
    assert_eq!(reload_row.revision, 1);
    assert_eq!(reopened.vault_status().item_count, 1);
}

#[test]
fn vault_crud_injected_fault_on_add_persists_nothing() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = fault_core(&dir, "fault-add.sqlite");
    core.vault_item_persist_fault = true;
    let result = core.vault_add_login(login_input("https://addfail.example", "bob", SENTINEL));
    assert!(matches!(result, Err(VaultCrudError::Storage(_))));
    assert_eq!(
        core.storage_ref()
            .unwrap()
            .count_active_vault_items()
            .unwrap(),
        0,
        "no candidate row survives rollback"
    );
    assert_eq!(core.vault_status().item_count, 0, "cached count unchanged");
    let _ = VaultSchemaVersion::CURRENT;
}
