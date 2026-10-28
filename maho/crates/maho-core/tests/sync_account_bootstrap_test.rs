use maho_core::maho_core::MahoCore;

const RELAY_URL: &str = "wss://relay.test";

#[test]
fn account_bootstrap_seed_configures_fresh_cores_to_the_same_room() {
    let bootstrap_info: serde_json::Value =
        serde_json::from_str(&MahoCore::new().generate_sync_bootstrap())
            .expect("generate bootstrap JSON");
    let bootstrap = bootstrap_info["seed"]
        .as_str()
        .expect("bootstrap includes seed")
        .to_owned();
    let mut origin = MahoCore::new();
    let mut joiner = MahoCore::new();

    let origin_room = origin
        .configure_sync_encryption_from_bootstrap(RELAY_URL, &bootstrap)
        .expect("origin accepts valid bootstrap");
    let joiner_room = joiner
        .configure_sync_encryption_from_bootstrap(RELAY_URL, &bootstrap)
        .expect("same account bootstrap configures joining device");

    assert_eq!(origin_room, joiner_room);
    assert_eq!(bootstrap_info["roomId"], origin_room);
}

#[test]
fn malformed_account_bootstrap_fails_closed() {
    let mut core = MahoCore::new();

    let error = core
        .configure_sync_encryption_from_bootstrap(RELAY_URL, "not-base64")
        .expect_err("malformed bootstrap must be rejected");

    assert!(error.contains("Invalid sync bootstrap"));
    assert!(
        core.get_sync_key().is_none(),
        "failed bootstrap must not install a Sync key"
    );
}

// --- Client-held key mode (GAP-3) -------------------------------------------

mod client_held {
    use std::sync::{Arc, Mutex};

    use base64::Engine as _;
    use maho_core::maho_core::{MahoCore, VaultAccountEnsureOutcome};
    use maho_core::sync_models::SyncStatus;
    use maho_core::vault_manager::VaultLoginInput;
    use maho_types::vault::{
        CredentialOrigin, VaultItemKind, VaultItemPublicMetadata, VaultLockState,
    };
    use zeroize::Zeroizing;

    const RELAY: &str = "wss://relay.test";
    const SEED: &[u8; 32] = b"client-held-escrow-seed-01234567";

    type Uploads = Arc<Mutex<Vec<(String, String)>>>;

    fn core_at(path: &std::path::Path) -> MahoCore {
        maho_storage::sqlite::set_sqlcipher_key("client-held-test-key").expect("configure key");
        MahoCore::new().with_storage(path.to_str().expect("utf8 path"))
    }

    fn escrow_device(path: &std::path::Path) -> MahoCore {
        let mut core = core_at(path);
        let seed = base64::engine::general_purpose::STANDARD.encode(SEED);
        core.configure_sync_encryption_from_bootstrap(RELAY, &seed)
            .expect("escrow bootstrap");
        assert_eq!(core.vault_status().lock_state, VaultLockState::Unlocked);
        assert!(core.vault_status().account_escrowed);
        core
    }

    fn capture_uploads(core: &mut MahoCore) -> Uploads {
        let uploads: Uploads = Arc::default();
        let sink = Arc::clone(&uploads);
        core.set_sync_snapshot_sink(Some(Box::new(move |room: &str, body: &str| {
            sink.lock().unwrap().push((room.to_string(), body.to_string()));
            Ok(())
        })));
        uploads
    }

    fn login(title: &str, secret: &str) -> VaultLoginInput {
        login_as(title, "user", secret)
    }

    fn login_as(title: &str, username: &str, secret: &str) -> VaultLoginInput {
        VaultLoginInput {
            metadata: VaultItemPublicMetadata {
                favorite: false,
                trashed_at: None,
                has_notes: false,
                title: title.to_string(),
                origins: vec![CredentialOrigin::try_from("https://held.example.com").unwrap()],
                username_hint: username.to_string(),
                item_kind: VaultItemKind::Login,
                totp: None,
                passkey: None,
            },
            username: username.to_string(),
            password: Zeroizing::new(secret.to_string()),
            form_details: None,
        }
    }

    #[test]
    fn client_held_phrase_join_syncs_and_decrypts_vault_items() {
        let dir = tempfile::tempdir().unwrap();
        let mut a = escrow_device(&dir.path().join("a.sqlite"));
        let before = a.vault_add_login(login("Before", "pre-switch-secret")).unwrap();
        let old_room = a.get_sync_room_id().unwrap();
        let uploads = capture_uploads(&mut a);

        let phrase = a.enable_client_held_keys().expect("enable client-held");
        assert_eq!(phrase.split_whitespace().count(), 24);
        assert_eq!(a.sync_key_mode(), "client_held");
        let status = a.vault_status();
        assert_eq!(status.lock_state, VaultLockState::Unlocked);
        assert!(!status.account_escrowed, "the account wrap must be gone");
        assert!(a.export_vault_account_escrow().unwrap().is_none());
        let new_room = a.get_sync_room_id().unwrap();
        assert_ne!(new_room, old_room);
        assert_eq!(&*a.vault_use_login_password(before.id).unwrap(), "pre-switch-secret");

        let uploads = uploads.lock().unwrap().clone();
        assert_eq!(uploads.len(), 1);
        assert_eq!(uploads[0].0, new_room, "snapshot goes to the new room");
        assert!(!uploads[0].1.contains("pre-switch-secret"));

        // Second device joins with the phrase alone: same room, vault adopted.
        let mut b = core_at(&dir.path().join("b.sqlite"));
        b.configure_sync_encryption_for_recovery_phrase(RELAY, &phrase)
            .expect("phrase join");
        assert_eq!(b.get_sync_room_id().unwrap(), new_room);
        b.apply_sync_snapshot(&uploads[0].1).expect("apply new-room snapshot");
        assert_eq!(b.sync_key_mode(), "client_held");
        assert_eq!(b.vault_status().lock_state, VaultLockState::Unlocked);
        assert_eq!(&*b.vault_use_login_password(before.id).unwrap(), "pre-switch-secret");

        // Incremental sync after the switch still decrypts on B.
        a.set_sync_status(SyncStatus::Synced);
        b.set_sync_status(SyncStatus::Synced);
        let after = a.vault_add_login(login_as("After", "other", "post-switch-secret")).unwrap();
        let envelopes = a.drain_sync_outgoing_envelopes().expect("lease outgoing");
        assert!(!envelopes.is_empty());
        for envelope in &envelopes {
            let json = serde_json::to_string(envelope).unwrap();
            b.handle_incoming_sync_envelope(&json).expect("B applies");
        }
        assert_eq!(&*b.vault_use_login_password(after.id).unwrap(), "post-switch-secret");
    }

    #[test]
    fn client_held_refuses_escrow_bootstrap_downgrade() {
        let dir = tempfile::tempdir().unwrap();
        let mut a = escrow_device(&dir.path().join("a.sqlite"));
        let item = a.vault_add_login(login("Guarded", "guarded-secret")).unwrap();
        let rev_before = a.storage_ref().unwrap().get_vault_item(&item.id.to_string()).unwrap().unwrap().revision;
        let uploads = capture_uploads(&mut a);
        a.enable_client_held_keys().expect("enable client-held");
        let rotated = a.storage_ref().unwrap().get_vault_item(&item.id.to_string()).unwrap().unwrap();
        assert_eq!(rotated.revision, rev_before + 1, "rotation bumps the item revision");
        let room = a.get_sync_room_id().unwrap();
        let body = uploads.lock().unwrap()[0].1.clone();

        let seed = base64::engine::general_purpose::STANDARD.encode(SEED);
        let err = a
            .configure_sync_encryption_from_bootstrap(RELAY, &seed)
            .expect_err("escrow bootstrap must be refused");
        assert_eq!(err, "client_held");
        let err = a
            .configure_sync_encryption_from_bootstrap_with_escrow(RELAY, &seed, None, None)
            .expect_err("escrow bootstrap with escrow must be refused");
        assert_eq!(err, "client_held");

        assert_eq!(a.get_sync_room_id().unwrap(), room, "room unchanged");
        assert_eq!(a.sync_key_mode(), "client_held");
        assert!(!a.vault_status().account_escrowed);
        // Key unchanged: the core still decrypts the new-room snapshot.
        a.apply_sync_snapshot(&body).expect("new-room key still installed");
        assert_eq!(&*a.vault_use_login_password(item.id).unwrap(), "guarded-secret");
    }

    #[test]
    fn client_held_old_seed_cannot_decrypt_new_data() {
        let dir = tempfile::tempdir().unwrap();
        let mut a = escrow_device(&dir.path().join("a.sqlite"));
        let item = a.vault_add_login(login("Rotated", "rotated-secret")).unwrap();
        let (old_kdf, old_wrap) = a.export_vault_account_escrow().unwrap().unwrap();
        let old_row = a.storage_ref().unwrap().get_vault_item(&item.id.to_string()).unwrap().unwrap();
        let uploads = capture_uploads(&mut a);
        a.enable_client_held_keys().expect("enable client-held");
        let new_row = a.storage_ref().unwrap().get_vault_item(&item.id.to_string()).unwrap().unwrap();
        assert_ne!(new_row.envelope, old_row.envelope, "items are re-encrypted");

        // A device holding only the old seed (and the old escrow record).
        let mut old = core_at(&dir.path().join("old.sqlite"));
        let outcome = old
            .ensure_vault_for_account_with_escrow(SEED, Some(&old_kdf), Some(&old_wrap))
            .expect("old escrow still opens the retired key");
        assert_eq!(outcome, VaultAccountEnsureOutcome::Adopted);
        old.storage_ref().unwrap().save_vault_item(&new_row).unwrap();
        assert!(
            old.vault_use_login_password(item.id).is_err(),
            "the retired DEK must not decrypt rotated items"
        );

        // The old sync seed cannot read the new room's snapshot either.
        let seed = base64::engine::general_purpose::STANDARD.encode(SEED);
        old.configure_sync_encryption_from_bootstrap(RELAY, &seed).unwrap();
        let body = uploads.lock().unwrap()[0].1.clone();
        assert!(old.apply_sync_snapshot(&body).is_err());
    }

    #[test]
    fn client_held_failed_snapshot_upload_leaves_escrow_mode_working() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("a.sqlite");
        let mut a = escrow_device(&path);
        let item = a.vault_add_login(login("Kept", "escrow-secret")).unwrap();
        let room = a.get_sync_room_id().unwrap();
        let row = a.storage_ref().unwrap().get_vault_item(&item.id.to_string()).unwrap().unwrap();
        a.set_sync_snapshot_sink(Some(Box::new(|_: &str, _: &str| {
            Err("injected upload failure".to_string())
        })));

        let error = a.enable_client_held_keys().expect_err("upload failure aborts");
        assert!(error.contains("snapshot upload failed"), "{error}");
        assert_eq!(a.sync_key_mode(), "escrow");
        assert_eq!(a.get_sync_room_id().unwrap(), room);
        assert!(a.vault_status().account_escrowed);
        assert_eq!(a.vault_status().lock_state, VaultLockState::Unlocked);
        let unchanged = a.storage_ref().unwrap().get_vault_item(&item.id.to_string()).unwrap().unwrap();
        assert_eq!(unchanged.envelope, row.envelope, "no item was rewritten");
        assert_eq!(&*a.vault_use_login_password(item.id).unwrap(), "escrow-secret");
        drop(a);

        let mut reopened = core_at(&path);
        reopened.load_persisted_data().expect("load");
        assert_eq!(
            reopened.ensure_vault_for_account(SEED).expect("escrow unwrap"),
            VaultAccountEnsureOutcome::Unwrapped
        );
        assert_eq!(&*reopened.vault_use_login_password(item.id).unwrap(), "escrow-secret");
    }
}
