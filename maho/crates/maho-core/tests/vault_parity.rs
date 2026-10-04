use maho_core::maho_core::MahoCore;
use maho_core::vault_manager::{
    OriginMatchPolicy, VaultCrudError, VaultLoginInput, VaultLoginUpdate,
};
use maho_core::vault_runtime::session::VaultBackendSecretAction;
use maho_types::vault::{
    CredentialOrigin, VaultBatchReadRequest, VaultItemKind, VaultItemListRequest,
    VaultItemPublicMetadata, VaultItemSearchRequest, VaultSchemaVersion, VaultTrashFilter,
};
use zeroize::Zeroizing;

fn core(dir: &tempfile::TempDir) -> MahoCore {
    maho_storage::sqlite::set_sqlcipher_key("vault-parity-test-key").unwrap();
    let mut core = MahoCore::new().with_storage(dir.path().join("parity.sqlite").to_str().unwrap());
    core.initialize_vault(b"master-parity", b"recovery-parity")
        .unwrap();
    core
}

fn metadata() -> VaultItemPublicMetadata {
    VaultItemPublicMetadata {
        title: "Example".into(),
        origins: vec![CredentialOrigin::try_from("https://example.com").unwrap()],
        username_hint: String::new(),
        item_kind: VaultItemKind::Login,
        totp: None,
        passkey: None,
        favorite: false,
        trashed_at: None,
        has_notes: false,
    }
}

fn login(username: &str, password: &str) -> VaultLoginInput {
    VaultLoginInput {
        metadata: metadata(),
        username: username.into(),
        password: Zeroizing::new(password.into()),
        form_details: None,
    }
}

fn list(trash: VaultTrashFilter) -> VaultItemListRequest {
    VaultItemListRequest {
        schema_version: VaultSchemaVersion::CURRENT,
        provider: None,
        kinds: vec![],
        cursor: None,
        limit: 0,
        trash,
        favorites_only: false,
    }
}

#[test]
fn vault_parity_trash_excludes_discovery_and_existing_backend_sessions() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = core(&dir);
    let item = core.vault_add_login(login("alice", "a password")).unwrap();
    let session = core.create_vault_backend_session().unwrap();
    let trashed = core.vault_trash_item(item.id, item.revision).unwrap();
    assert!(trashed.trashed_at.is_some());
    assert_eq!(trashed.revision.value(), item.revision.value() + 1);
    assert!(core
        .vault_list_items(&list(VaultTrashFilter::Exclude))
        .unwrap()
        .is_empty());
    assert_eq!(
        core.vault_list_items(&list(VaultTrashFilter::Only))
            .unwrap()[0]
            .id,
        item.id
    );
    let search = VaultItemSearchRequest {
        schema_version: VaultSchemaVersion::CURRENT,
        origin: CredentialOrigin::try_from("https://example.com").unwrap(),
        provider: None,
        kinds: vec![],
    };
    assert!(core
        .vault_match_items(&search, OriginMatchPolicy::Exact)
        .unwrap()
        .is_empty());
    assert!(session.login_credentials(None, None).unwrap().is_empty());
    let batch = session
        .batch_read(&VaultBatchReadRequest {
            schema_version: VaultSchemaVersion::CURRENT,
            item_ids: vec![item.id],
        })
        .unwrap();
    assert!(batch.items.is_empty());
    assert_eq!(batch.missing_ids, vec![item.id]);
    assert!(session
        .resolve_login_secret(item.id, trashed.revision, VaultBackendSecretAction::Fill)
        .is_err());
    assert!(core.vault_use_login_password(item.id).is_err());
    assert!(matches!(
        core.vault_restore_item(item.id, item.revision),
        Err(VaultCrudError::RevisionConflict { .. })
    ));
}

#[test]
fn vault_parity_restore_retains_encrypted_secret_and_favorite() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = core(&dir);
    let item = core
        .vault_add_login(login("alice", "retained secret"))
        .unwrap();
    let favorite = core
        .vault_set_favorite(item.id, item.revision, true)
        .unwrap();
    let trashed = core.vault_trash_item(item.id, favorite.revision).unwrap();
    let restored = core.vault_restore_item(item.id, trashed.revision).unwrap();
    assert!(restored.trashed_at.is_none());
    assert!(restored.favorite);
    let mut request = list(VaultTrashFilter::Exclude);
    request.favorites_only = true;
    assert_eq!(core.vault_list_items(&request).unwrap().len(), 1);
    assert_eq!(
        core.vault_use_login_password(restored.id).unwrap().as_str(),
        "retained secret"
    );
}

#[test]
fn vault_parity_empty_trash_tombstones_only_trashed_items() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = core(&dir);
    let first = core.vault_add_login(login("alice", "discarded")).unwrap();
    let second = core.vault_add_login(login("bob", "retained")).unwrap();
    let trash = core.vault_trash_item(first.id, first.revision).unwrap();
    let result = core.vault_empty_trash().unwrap();
    assert_eq!(result.count, 1);
    assert_eq!(result.tombstoned_ids, vec![first.id]);
    assert!(matches!(
        core.vault_restore_item(first.id, trash.revision),
        Err(VaultCrudError::ItemNotFound)
    ));
    assert_eq!(
        core.vault_list_items(&list(VaultTrashFilter::Exclude))
            .unwrap()[0]
            .id,
        second.id
    );
    assert!(core
        .vault_list_items(&list(VaultTrashFilter::Only))
        .unwrap()
        .is_empty());
    assert_eq!(core.vault_empty_trash().unwrap().count, 0);
}

#[test]
fn vault_parity_login_notes_update_preserve_and_clear() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = core(&dir);
    let item = core.vault_add_login(login("alice", "retained")).unwrap();
    let with_notes = core
        .vault_update_login(
            item.id,
            item.revision,
            VaultLoginUpdate {
                metadata: metadata(),
                username: "alice".into(),
                password: None,
                form_details: None,
                notes: Some(Zeroizing::new("private note".into())),
            },
        )
        .unwrap();
    assert!(with_notes.has_notes);
    assert_eq!(
        core.vault_get_notes(item.id).unwrap().as_str(),
        "private note"
    );
    assert!(!serde_json::to_string(&with_notes)
        .unwrap()
        .contains("private note"));
    let preserved = core
        .vault_update_login(
            item.id,
            with_notes.revision,
            VaultLoginUpdate {
                metadata: metadata(),
                username: "alice".into(),
                password: None,
                form_details: None,
                notes: None,
            },
        )
        .unwrap();
    assert!(preserved.has_notes);
    let cleared = core
        .vault_update_login(
            item.id,
            preserved.revision,
            VaultLoginUpdate {
                metadata: metadata(),
                username: "alice".into(),
                password: None,
                form_details: None,
                notes: Some(Zeroizing::new(String::new())),
            },
        )
        .unwrap();
    assert!(!cleared.has_notes);
    assert!(core.vault_get_notes(item.id).unwrap().is_empty());
}

#[test]
fn vault_parity_get_username_returns_unmasked_value() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = core(&dir);
    let item = core.vault_add_login(login("alice@example.com", "retained")).unwrap();
    assert_ne!(item.username_hint, "alice@example.com");
    assert_eq!(
        core.vault_get_username(item.id.clone()).unwrap().as_str(),
        "alice@example.com"
    );
    let trashed = core.vault_trash_item(item.id, item.revision).unwrap();
    assert!(core.vault_get_username(trashed.id).is_err());
}

#[test]
fn vault_parity_login_added_with_notes_accepts_notes_update() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = core(&dir);
    // Mirrors the settings add path: notes arrive inside default form details.
    let mut details = maho_core::vault_manager::VaultCredentialFormDetails::default();
    let mut note = maho_core::vault_manager::VaultCredentialNote::default();
    note.value = "initial note".into();
    details.notes.push(note);
    let mut input = login("alice", "retained");
    input.form_details = Some(details);
    let item = core.vault_add_login(input).unwrap();
    assert!(item.has_notes);
    let updated = core
        .vault_update_login(
            item.id,
            item.revision,
            VaultLoginUpdate {
                metadata: metadata(),
                username: "alice".into(),
                password: None,
                form_details: None,
                notes: Some(Zeroizing::new("revised note".into())),
            },
        )
        .unwrap();
    assert!(updated.has_notes);
    assert_eq!(core.vault_get_notes(item.id).unwrap().as_str(), "revised note");
}

#[test]
fn vault_parity_trashed_item_notes_are_not_readable() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = core(&dir);
    let item = core
        .vault_add_secure_note("Note".into(), Zeroizing::new("private".into()))
        .unwrap();
    let trashed = core.vault_trash_item(item.id, item.revision).unwrap();
    assert!(matches!(
        core.vault_get_notes(item.id),
        Err(VaultCrudError::ItemNotFound)
    ));
    core.vault_restore_item(item.id, trashed.revision).unwrap();
    assert_eq!(core.vault_get_notes(item.id).unwrap().as_str(), "private");
}

#[test]
fn vault_parity_secure_note_add_update_and_locked_reads() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = core(&dir);
    let item = core
        .vault_add_secure_note("Note".into(), Zeroizing::new("private".into()))
        .unwrap();
    assert_eq!(item.item_kind, VaultItemKind::SecureItem);
    assert!(item.has_notes);
    let updated = core
        .vault_update_secure_note(
            item.id,
            item.revision,
            "Renamed".into(),
            Zeroizing::new("updated".into()),
        )
        .unwrap();
    assert_eq!(updated.title, "Renamed");
    assert_eq!(core.vault_get_notes(item.id).unwrap().as_str(), "updated");
    assert!(core
        .vault_update_secure_note(
            item.id,
            item.revision,
            "Stale".into(),
            Zeroizing::new("lost".into())
        )
        .is_err());
    core.lock_vault().unwrap();
    assert!(matches!(
        core.vault_get_notes(item.id),
        Err(VaultCrudError::Locked)
    ));
    assert!(matches!(
        core.vault_health_report(),
        Err(VaultCrudError::Locked)
    ));
    assert!(matches!(
        core.vault_empty_trash(),
        Err(VaultCrudError::Locked)
    ));
}

#[test]
fn vault_parity_attached_totp_accepts_base32_and_uri_without_exposing_seed() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = core(&dir);
    let item = core.vault_add_login(login("alice", "retained")).unwrap();
    let seed = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";
    let attached = core
        .vault_set_login_totp(item.id, item.revision, seed)
        .unwrap();
    assert!(attached.totp.is_some());
    let code = core.vault_totp_code(item.id, 59).unwrap();
    assert_eq!(code.code, "287082");
    assert_eq!(code.seconds_remaining, 1);
    assert_eq!(code.period, 30);
    assert!(!serde_json::to_string(&attached).unwrap().contains(seed));
    let uri = format!("otpauth://totp/Example:alice?secret={seed}&issuer=Example&algorithm=SHA1&digits=8&period=30");
    let updated = core
        .vault_set_login_totp(item.id, attached.revision, &uri)
        .unwrap();
    assert_eq!(core.vault_totp_code(item.id, 59).unwrap().code, "94287082");
    assert_eq!(
        core.vault_totp_code(item.id, 60).unwrap().seconds_remaining,
        30
    );
    for invalid in [
        "",
        "invalid!",
        "A",
        "otpauth://hotp/a?secret=AAAA",
        "otpauth://totp/a?secret=AAAA&period=0",
    ] {
        assert!(core
            .vault_set_login_totp(item.id, updated.revision, invalid)
            .is_err());
    }
    let edited = core
        .vault_update_login(
            item.id,
            updated.revision,
            VaultLoginUpdate {
                metadata: metadata(),
                username: "alice".into(),
                password: None,
                form_details: None,
                notes: None,
            },
        )
        .unwrap();
    assert!(edited.totp.is_some());
    assert_eq!(
        core.vault_generate_totp_code(item.id, 59).unwrap().as_str(),
        "94287082"
    );
}

#[test]
fn vault_parity_health_groups_reuse_and_excludes_trash() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = core(&dir);
    let first = core.vault_add_login(login("alice", "password")).unwrap();
    let second = core.vault_add_login(login("bob", "password")).unwrap();
    let third = core.vault_add_login(login("carol", "password")).unwrap();
    core.vault_trash_item(third.id, third.revision).unwrap();
    core.vault_add_secure_note("Note".into(), Zeroizing::new("password".into()))
        .unwrap();
    let report = core.vault_health_report().unwrap();
    assert_eq!(report.total_logins, 2);
    assert_eq!(report.weak.len(), 2);
    assert!(report.weak.contains(&first.id));
    assert!(report.weak.contains(&second.id));
    assert_eq!(report.reused.len(), 1);
    assert_eq!(report.reused[0].len(), 2);
    assert!(!serde_json::to_string(&report).unwrap().contains("password"));
}
