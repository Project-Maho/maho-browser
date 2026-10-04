//! Real SQLCipher fault injection at the audit-write boundary. These tests use
//! public core/session entry points and inspect ciphertext after reopening.

use maho_core::maho_core::MahoCore;
use maho_core::vault_manager::{VaultCrudError, VaultLoginInput, VaultLoginUpdate};
use maho_storage::sqlite::{EncryptedVaultItemRow, SqliteStorage, VaultAuditRow};
use maho_types::vault::{
    CredentialOrigin, VaultAgentPolicy, VaultItemKind, VaultItemPublicMetadata, VaultPolicy,
    VaultSchemaVersion,
};
use rusqlite::Connection;
use zeroize::Zeroizing;

#[path = "vault_audit_atomicity/session.rs"]
mod session;

const DATABASE_KEY: &str = "vault-audit-atomicity-test-key";
const MASTER: &[u8] = b"correct horse battery staple";
const RECOVERY: &[u8] = b"recovery-emergency-phrase-deterministic";
const SECRET: &str = "ATOMIC-AUDIT-SECRET-SENTINEL";

fn core(dir: &tempfile::TempDir) -> MahoCore {
    // Every test in this process uses the same key; no test races to replace
    // the process-global key with a different value.
    maho_storage::sqlite::set_sqlcipher_key(DATABASE_KEY).expect("configure database key");
    let path = dir.path().join("vault.sqlite");
    let mut core = MahoCore::new().with_storage(path.to_str().expect("UTF-8 path"));
    core.initialize_vault(MASTER, RECOVERY)
        .expect("initialize vault");
    core
}

fn metadata() -> VaultItemPublicMetadata {
    VaultItemPublicMetadata {
        favorite: false,
        trashed_at: None,
        has_notes: false,
        title: "Atomic audit".to_string(),
        origins: vec![CredentialOrigin::try_from("https://atomic.example").expect("origin")],
        username_hint: String::new(),
        item_kind: VaultItemKind::Login,
        totp: None,
        passkey: None,
    }
}

fn login() -> VaultLoginInput {
    VaultLoginInput {
        metadata: metadata(),
        username: "atomic-user".to_string(),
        password: Zeroizing::new(SECRET.to_string()),
        form_details: None,
    }
}

fn policy(value: VaultAgentPolicy) -> VaultPolicy {
    VaultPolicy {
        schema_version: VaultSchemaVersion::CURRENT,
        policy: value,
        item_id: None,
        origin: None,
        expires_at: None,
    }
}

fn fault_connection(core: &MahoCore) -> Connection {
    let connection = Connection::open(core.sqlite_db_path().expect("database path"))
        .expect("open fault-injection connection");
    connection
        .pragma_update(None, "key", DATABASE_KEY)
        .expect("set SQLCipher key");
    connection
}

fn reject_audit(connection: &Connection, after_insert: bool) {
    // AFTER INSERT exercises rollback after SQLite has written the audit row.
    let sql = if after_insert {
        "CREATE TRIGGER reject_vault_audit AFTER INSERT ON vault_audit_events
         BEGIN SELECT RAISE(ABORT, 'injected audit insert failure'); END;"
    } else {
        "CREATE TRIGGER reject_vault_audit BEFORE INSERT ON vault_audit_events
         BEGIN SELECT RAISE(ABORT, 'injected audit insert failure'); END;"
    };
    connection.execute_batch(sql).expect("install audit fault");
}

fn clear_fault(connection: &Connection) {
    connection
        .execute_batch("DROP TRIGGER reject_vault_audit;")
        .expect("remove audit fault");
}

fn items(core: &MahoCore) -> Vec<EncryptedVaultItemRow> {
    core.storage_ref()
        .expect("storage")
        .list_vault_items()
        .expect("items")
}

fn audits(core: &MahoCore) -> Vec<VaultAuditRow> {
    core.storage_ref()
        .expect("storage")
        .list_vault_audit_events()
        .expect("audits")
}

fn assert_chain(rows: &[VaultAuditRow]) {
    for (index, row) in rows.iter().enumerate() {
        assert_eq!(row.seq, i64::try_from(index + 1).expect("sequence fits"));
        assert_eq!(row.entry_hash.len(), 32);
        assert_eq!(
            row.prev_hash.as_deref(),
            index.checked_sub(1).map(|i| rows[i].entry_hash.as_slice())
        );
        assert!(row.item_id.is_none());
        assert!(row.item_alias.is_none());
    }
    assert!(!format!("{rows:?}").contains(SECRET));
}

fn assert_reopened_items(core: &MahoCore, expected: &[EncryptedVaultItemRow]) {
    let reopened = SqliteStorage::open_with_key(core.sqlite_db_path().expect("path"), DATABASE_KEY)
        .expect("reopen persisted database");
    assert_eq!(
        reopened.list_vault_items().expect("reloaded items"),
        expected
    );
}

#[test]
fn audit_insert_failure_rolls_back_create_and_cached_count() {
    let dir = tempfile::tempdir().expect("tempdir");
    let mut core = core(&dir);
    let connection = fault_connection(&core);
    let before = audits(&core);
    reject_audit(&connection, false);

    assert!(matches!(
        core.vault_add_login(login()),
        Err(VaultCrudError::Storage(_))
    ));
    assert!(
        items(&core).is_empty(),
        "failed create must not persist an item"
    );
    assert_eq!(core.vault_status().item_count, 0);
    assert_eq!(audits(&core), before);
    assert_reopened_items(&core, &[]);

    clear_fault(&connection);
    core.vault_add_login(login())
        .expect("create after fault removed");
    assert_eq!(core.vault_status().item_count, 1);
    let rows = audits(&core);
    assert_eq!(rows.len(), before.len() + 1);
    assert_eq!(rows.last().expect("create audit").operation, "item_created");
    assert_chain(&rows);
}

#[test]
fn audit_insert_failure_rolls_back_update_fill_and_delete_byte_for_byte() {
    let dir = tempfile::tempdir().expect("tempdir");
    let mut core = core(&dir);
    let item = core.vault_add_login(login()).expect("create login");
    let before_items = items(&core);
    let before_audits = audits(&core);
    let connection = fault_connection(&core);
    reject_audit(&connection, true);

    let update = core.vault_update_login(
        item.id,
        item.revision,
        VaultLoginUpdate {
            notes: None,
            metadata: metadata(),
            username: "changed-user".to_string(),
            password: Some(Zeroizing::new("changed-password".to_string())),
            form_details: None,
        },
    );
    assert!(matches!(update, Err(VaultCrudError::Storage(_))));
    assert_eq!(items(&core), before_items);
    assert!(matches!(
        core.vault_use_login_password_at_revision(item.id, item.revision),
        Err(VaultCrudError::Storage(_))
    ));
    assert_eq!(
        items(&core),
        before_items,
        "failed fill must not change last-used ciphertext or revision"
    );
    assert!(matches!(
        core.vault_delete_item(item.id, item.revision),
        Err(VaultCrudError::Storage(_))
    ));
    assert_eq!(items(&core), before_items);
    assert_eq!(core.vault_status().item_count, 1);
    assert_eq!(audits(&core), before_audits);
    assert_reopened_items(&core, &before_items);

    clear_fault(&connection);
    let secret = core
        .vault_use_login_password_at_revision(item.id, item.revision)
        .expect("original revision and password survive rollback");
    assert_eq!(secret.as_str(), SECRET);
    assert_chain(&audits(&core));
}

#[test]
fn audit_insert_failure_rolls_back_policy_change() {
    let dir = tempfile::tempdir().expect("tempdir");
    let core = core(&dir);
    core.vault_set_agent_policy(policy(VaultAgentPolicy::AskEveryUse))
        .expect("initial policy");
    let before = core
        .storage_ref()
        .expect("storage")
        .list_vault_policies()
        .expect("policies");
    let before_audits = audits(&core);
    let connection = fault_connection(&core);
    reject_audit(&connection, true);

    assert!(matches!(
        core.vault_set_agent_policy(policy(VaultAgentPolicy::WhileUnlocked)),
        Err(VaultCrudError::Storage(_))
    ));
    assert_eq!(
        core.storage_ref()
            .expect("storage")
            .list_vault_policies()
            .expect("policies"),
        before
    );
    assert_eq!(
        core.vault_agent_policy_status()
            .expect("effective policy")
            .policy,
        VaultAgentPolicy::AskEveryUse
    );
    assert_eq!(audits(&core), before_audits);
    let reopened = SqliteStorage::open_with_key(core.sqlite_db_path().expect("path"), DATABASE_KEY)
        .expect("reopen storage");
    assert_eq!(
        reopened.list_vault_policies().expect("reloaded policies"),
        before
    );

    clear_fault(&connection);
    core.vault_set_agent_policy(policy(VaultAgentPolicy::WhileUnlocked))
        .expect("policy succeeds without fault");
    let rows = audits(&core);
    assert_eq!(rows.len(), before_audits.len() + 1);
    assert_eq!(
        rows.last().expect("policy audit").operation,
        "policy_changed"
    );
    assert_chain(&rows);
}
