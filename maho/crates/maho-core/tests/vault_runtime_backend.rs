use maho_core::maho_core::MahoCore;
use maho_core::vault_manager::{VaultCrudError, VaultLoginInput};
use maho_types::vault::{
    CredentialOrigin, VaultBatchReadRequest, VaultItemCreatedRange, VaultItemKind,
    VaultItemPublicMetadata, VaultRevision, VaultSchemaVersion,
};
use zeroize::Zeroizing;

const MASTER: &[u8] = b"correct horse battery staple";
const RECOVERY: &[u8] = b"recovery-emergency-phrase-deterministic";
const SENTINEL: &str = "S3NTINEL-maho-vault-9F4C";

#[test]
fn vault_backend_secret_result_is_runtime_local_and_has_no_exposure_derives() {
    // Given the runtime implementation source for its privileged result type.
    let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("src/vault_runtime/mod.rs");
    let source = std::fs::read_to_string(path).expect("read runtime source");

    // When the local secret result declaration is inspected.
    let declaration = source
        .find("pub(crate) enum VaultSecretResult")
        .expect("runtime-local VaultSecretResult declaration");
    let attributes = &source[declaration.saturating_sub(128)..declaration];

    // Then it cannot be moved to a public contract or derive exposure traits.
    assert!(!source.contains("pub enum VaultSecretResult"));
    assert!(!attributes.contains("#[derive"));
    for trait_name in ["Debug", "Clone", "Serialize", "Deserialize"] {
        assert!(!source.contains(&format!("impl {trait_name} for VaultSecretResult")));
    }
}

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
            favorite: false,
            trashed_at: None,
            has_notes: false,
            title: title.to_string(),
            origins: vec![CredentialOrigin::try_from(origin).expect("valid test origin")],
            username_hint: String::new(),
            item_kind: VaultItemKind::Login,
            totp: None,
            passkey: None,
        },
        username: username.to_string(),
        password: Zeroizing::new(SENTINEL.to_string()),
        form_details: None,
    }
}

#[test]
fn vault_backend_batch_returns_active_items_in_request_order_and_missing_tombstones() {
    // Given active, missing, and tombstoned stable item IDs.
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    let first = core
        .vault_add_login(login("First", "https://first.example", "first"))
        .expect("add first");
    let second = core
        .vault_add_login(login("Second", "https://second.example", "second"))
        .expect("add second");
    let tombstoned = core
        .vault_add_login(login("Deleted", "https://deleted.example", "deleted"))
        .expect("add tombstoned item");
    core.vault_delete_item(tombstoned.id, VaultRevision::new(1))
        .expect("tombstone item");
    let missing = maho_types::vault::VaultItemId::new();
    let request = VaultBatchReadRequest {
        schema_version: VaultSchemaVersion::CURRENT,
        item_ids: vec![second.id, missing, tombstoned.id, first.id],
    };

    // When the ordered batch request is read.
    let result = core.vault_batch_read(&request).expect("batch read");

    // Then only active public DTOs retain request order and unavailable IDs retain theirs.
    assert_eq!(
        result.items.iter().map(|item| item.id).collect::<Vec<_>>(),
        vec![second.id, first.id]
    );
    assert_eq!(result.missing_ids, vec![missing, tombstoned.id]);
    assert!(result
        .items
        .iter()
        .all(|item| item.revision == VaultRevision::new(1)));
    assert!(result.items.iter().all(|item| item.last_used_at.is_none()));
    let public_json = serde_json::to_string(&result).expect("serialize public result");
    assert!(
        !public_json.contains(SENTINEL),
        "batch result is secret-free"
    );
}

#[test]
fn vault_backend_batch_denies_locked_before_repository_access() {
    // Given an uninitialized runtime with no configured repository.
    let core = MahoCore::new();
    let request = VaultBatchReadRequest {
        schema_version: VaultSchemaVersion::CURRENT,
        item_ids: vec![maho_types::vault::VaultItemId::new()],
    };

    // When the locked runtime receives a batch read.
    let result = core.vault_batch_read(&request);

    // Then lock denial wins before repository construction or row access.
    assert!(matches!(result, Err(VaultCrudError::Locked)));
}

#[test]
fn vault_backend_range_delete_includes_lower_and_excludes_upper_exact_boundaries() {
    // Given two live items that will become the exact interval endpoints.
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    let from = core
        .vault_add_login(login("From", "https://from.example", "from"))
        .expect("add lower endpoint");
    let until = core
        .vault_add_login(login("Until", "https://until.example", "until"))
        .expect("add upper endpoint");
    assert!(from.created_at < until.created_at);
    let range = VaultItemCreatedRange {
        schema_version: VaultSchemaVersion::CURRENT,
        from_inclusive: Some(from.created_at),
        until_exclusive: Some(until.created_at),
    };

    // When the bounded half-open interval is deleted.
    let result = core
        .vault_delete_created_range(&range)
        .expect("delete range");

    // Then the lower endpoint is tombstoned and the upper endpoint remains live.
    assert_eq!(result.tombstoned_ids, vec![from.id.clone()]);
    assert_eq!(result.count, 1);
    assert!(core
        .storage_ref()
        .expect("storage")
        .get_vault_item(&from.id.to_string())
        .expect("read lower endpoint")
        .expect("lower endpoint row")
        .deleted_at
        .is_some());
    assert_eq!(
        core.storage_ref()
            .expect("storage")
            .get_vault_item(&from.id.to_string())
            .expect("read lower endpoint revision")
            .expect("lower endpoint revision row")
            .revision,
        2
    );
    assert!(core
        .storage_ref()
        .expect("storage")
        .get_vault_item(&until.id.to_string())
        .expect("read upper endpoint")
        .expect("upper endpoint row")
        .deleted_at
        .is_none());
}

#[test]
fn vault_backend_range_delete_supports_unbounded_and_empty_ranges() {
    // Given two live rows plus one pre-existing tombstone.
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    let first = core
        .vault_add_login(login("First", "https://first.example", "first"))
        .expect("add first");
    let second = core
        .vault_add_login(login("Second", "https://second.example", "second"))
        .expect("add second");
    let existing_tombstone = core
        .vault_add_login(login("Deleted", "https://deleted.example", "deleted"))
        .expect("add deleted");
    core.vault_delete_item(existing_tombstone.id, VaultRevision::new(1))
        .expect("tombstone deleted");
    let empty_from = chrono::Utc::now();

    // When an empty range and then a fully unbounded range are deleted.
    let empty = core
        .vault_delete_created_range(&VaultItemCreatedRange {
            schema_version: VaultSchemaVersion::CURRENT,
            from_inclusive: Some(empty_from),
            until_exclusive: None,
        })
        .expect("delete empty range");
    let all = core
        .vault_delete_created_range(&VaultItemCreatedRange {
            schema_version: VaultSchemaVersion::CURRENT,
            from_inclusive: None,
            until_exclusive: None,
        })
        .expect("delete unbounded range");

    // Then the empty request changes nothing and unbounded deletion skips tombstones.
    assert_eq!(empty.count, 0);
    assert!(empty.tombstoned_ids.is_empty());
    assert_eq!(
        all.tombstoned_ids,
        vec![first.id.clone(), second.id.clone()]
    );
    assert_eq!(all.count, 2);
    assert_eq!(core.vault_status().item_count, 0);
    let first_tombstone = core
        .storage_ref()
        .expect("storage")
        .get_vault_item(&first.id.to_string())
        .expect("read first tombstone")
        .expect("first tombstone row");
    let second_tombstone = core
        .storage_ref()
        .expect("storage")
        .get_vault_item(&second.id.to_string())
        .expect("read second tombstone")
        .expect("second tombstone row");
    assert_eq!(
        first_tombstone.updated_at,
        first_tombstone.deleted_at.clone().expect("deleted at")
    );
    assert_eq!(
        first_tombstone.deleted_at, second_tombstone.deleted_at,
        "one range deletion uses one tombstone timestamp"
    );
}

#[test]
fn vault_backend_range_delete_equal_bounds_are_empty() {
    // Given a live item and a range whose bounds are its exact creation time.
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    let item = core
        .vault_add_login(login("Item", "https://item.example", "item"))
        .expect("add item");
    // When the equal-bound half-open range is requested.
    let result = core.vault_delete_created_range(&VaultItemCreatedRange {
        schema_version: VaultSchemaVersion::CURRENT,
        from_inclusive: Some(item.created_at.clone()),
        until_exclusive: Some(item.created_at),
    });

    // Then the empty half-open interval changes nothing.
    let result = result.expect("equal interval is empty");
    assert_eq!(result.count, 0);
    assert!(result.tombstoned_ids.is_empty());
    assert!(core
        .storage_ref()
        .expect("storage")
        .get_vault_item(&item.id.to_string())
        .expect("read item")
        .expect("item row")
        .deleted_at
        .is_none());
    assert_eq!(core.vault_status().item_count, 1);
}

#[test]
fn vault_backend_range_delete_denies_locked_before_repository_access() {
    // Given an uninitialized runtime with no configured repository.
    let core = MahoCore::new();
    let range = VaultItemCreatedRange {
        schema_version: VaultSchemaVersion::CURRENT,
        from_inclusive: None,
        until_exclusive: None,
    };

    // When the locked runtime receives a range deletion.
    let result = core.vault_delete_created_range(&range);

    // Then lock denial wins before repository construction or row access.
    assert!(matches!(result, Err(VaultCrudError::Locked)));
}
