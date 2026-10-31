use maho_core::maho_core::MahoCore;
use maho_core::vault_manager::{VaultCrudError, VaultLoginInput, VaultLoginUpdate};
use maho_types::vault::{
    CredentialOrigin, VaultAuditDecision, VaultAuditOperation, VaultErrorCode, VaultItemKind,
    VaultItemListRequest, VaultItemPublicMetadata, VaultRevision, VaultSchemaVersion,
};
use zeroize::Zeroizing;

const MASTER: &[u8] = b"correct horse battery staple";
const RECOVERY: &[u8] = b"recovery-emergency-phrase-deterministic";
const SENTINEL: &str = "S3NTINEL-maho-vault-9F4C";

fn unlocked_core(dir: &tempfile::TempDir, name: &str) -> MahoCore {
    maho_storage::sqlite::set_sqlcipher_key("vault-audit-test-key").expect("configure key");
    let path = dir.path().join(name);
    let mut core = MahoCore::new().with_storage(path.to_str().expect("utf8 path"));
    core.initialize_vault(MASTER, RECOVERY)
        .expect("initialize_vault must persist and unlock");
    core
}

fn origins(values: &[&str]) -> Vec<CredentialOrigin> {
    values
        .iter()
        .map(|value| CredentialOrigin::try_from(*value).expect("valid origin literal"))
        .collect()
}

fn login_meta(title: &str, origin_values: &[&str]) -> VaultItemPublicMetadata {
    VaultItemPublicMetadata {
        favorite: false,
        trashed_at: None,
        has_notes: false,
        title: title.to_string(),
        origins: origins(origin_values),
        username_hint: String::new(),
        item_kind: VaultItemKind::Login,
        totp: None,
        passkey: None,
    }
}

fn login_input(
    title: &str,
    origin_values: &[&str],
    username: &str,
    password: &str,
) -> VaultLoginInput {
    VaultLoginInput {
        metadata: login_meta(title, origin_values),
        username: username.to_string(),
        password: Zeroizing::new(password.to_string()),
        form_details: None,
    }
}

fn list_all() -> VaultItemListRequest {
    VaultItemListRequest {
        trash: Default::default(),
        favorites_only: false,
        schema_version: VaultSchemaVersion::CURRENT,
        provider: None,
        kinds: Vec::new(),
        cursor: None,
        limit: 0,
    }
}

fn audit_rows(core: &MahoCore) -> Vec<maho_storage::sqlite::VaultAuditRow> {
    core.storage_ref()
        .expect("storage configured")
        .list_vault_audit_events()
        .expect("read vault audit rows")
}

fn audit_token<T: serde::Serialize>(value: T) -> String {
    serde_json::to_value(value)
        .expect("audit token serializes")
        .as_str()
        .expect("audit token is string")
        .to_string()
}

fn assert_audit_rows_are_payload_safe(
    rows: &[maho_storage::sqlite::VaultAuditRow],
    sentinel: &str,
    forbidden_item_ids: &[String],
) {
    let rendered = format!("{rows:?}");
    assert!(
        !rendered.contains(sentinel),
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
fn vault_audit_records_successful_library_operations_without_item_ids_or_secret_values() {
    // Given an unlocked vault and one login containing a unique secret sentinel.
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "audit-success.sqlite");

    // When the library surface creates, updates, uses, and deletes the item.
    let added = core
        .vault_add_login(login_input(
            "Audit",
            &["https://audit.example"],
            "alice",
            SENTINEL,
        ))
        .expect("add audited login");
    let mut metadata = login_meta("Audit renamed", &["https://audit.example"]);
    metadata.item_kind = VaultItemKind::Login;
    let updated = core
        .vault_update_login(
            added.id,
            added.revision,
            VaultLoginUpdate {
                notes: None,
                metadata,
                username: "alice".to_string(),
                password: Some(Zeroizing::new(SENTINEL.to_string())),
                form_details: None,
            },
        )
        .expect("update audited login");
    let _password = core
        .vault_use_login_password(updated.id)
        .expect("use audited login");
    let latest_revision = core.vault_list_items(&list_all()).unwrap()[0].revision;
    core.vault_delete_item(updated.id, latest_revision)
        .expect("delete audited login");

    // Then each successful operation emits one safe audit row.
    let rows = audit_rows(&core);
    assert_eq!(
        rows.iter()
            .map(|row| row.operation.as_str())
            .collect::<Vec<_>>(),
        vec!["item_created", "item_updated", "fill", "item_deleted"]
    );
    assert!(rows.iter().all(|row| row.decision == "allowed"));
    assert_audit_rows_are_payload_safe(&rows, SENTINEL, &[added.id.to_string()]);
}

#[test]
fn vault_audit_records_failed_and_denied_attempts_without_item_ids_or_secret_values() {
    // Given an unlocked vault with one login.
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "audit-failed-denied.sqlite");
    let added = core
        .vault_add_login(login_input(
            "Audit",
            &["https://audit-failed.example"],
            "alice",
            SENTINEL,
        ))
        .expect("add audited login");

    // When a stale update and wrong-kind secret use fail, then a locked add is denied.
    let stale_update = core.vault_update_login(
        added.id,
        VaultRevision::new(99),
        VaultLoginUpdate {
            notes: None,
            metadata: login_meta("Audit", &["https://audit-failed.example"]),
            username: "alice".to_string(),
            password: Some(Zeroizing::new("new-secret".to_string())),
            form_details: None,
        },
    );
    assert!(matches!(
        stale_update,
        Err(VaultCrudError::RevisionConflict { .. })
    ));
    assert!(matches!(
        core.vault_use_totp_seed(added.id),
        Err(VaultCrudError::ItemKindMismatch)
    ));
    core.lock_vault().expect("lock vault");
    assert!(matches!(
        core.vault_add_login(login_input(
            "Locked",
            &["https://locked-audit.example"],
            "bob",
            "locked-secret",
        )),
        Err(VaultCrudError::Locked)
    ));

    // Then failed and denied attempts are represented with safe status/reason only.
    let rows = audit_rows(&core);
    assert_eq!(
        rows.iter()
            .map(|row| row.operation.as_str())
            .collect::<Vec<_>>(),
        vec!["item_created", "item_updated", "totp_fill", "item_created"]
    );
    assert_eq!(
        rows.iter()
            .map(|row| row.decision.as_str())
            .collect::<Vec<_>>(),
        vec!["allowed", "failed", "failed", "denied"]
    );
    assert_eq!(
        rows[1].reason.as_deref(),
        Some(audit_token(VaultErrorCode::RevisionConflict).as_str())
    );
    assert_eq!(
        rows[2].reason.as_deref(),
        Some(audit_token(VaultErrorCode::SecretFieldForbidden).as_str())
    );
    assert_eq!(
        rows[3].reason.as_deref(),
        Some(audit_token(VaultErrorCode::Locked).as_str())
    );
    assert_eq!(
        rows[0].operation,
        audit_token(VaultAuditOperation::ItemCreated)
    );
    assert_eq!(rows[3].decision, audit_token(VaultAuditDecision::Denied));
    assert_audit_rows_are_payload_safe(&rows, SENTINEL, &[added.id.to_string()]);
}
