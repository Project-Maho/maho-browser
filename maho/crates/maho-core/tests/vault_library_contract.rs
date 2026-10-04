use maho_core::maho_core::MahoCore;
use maho_core::vault_manager::{VaultCrudError, VaultLoginInput, VaultLoginUpdate};
use maho_types::vault::{
    CredentialOrigin, VaultItemKind, VaultItemListRequest, VaultItemPublicMetadata,
    VaultItemSearchRequest, VaultSchemaVersion,
};
use serde_json::Value;
use zeroize::Zeroizing;

const MASTER: &[u8] = b"correct horse battery staple";
const RECOVERY: &[u8] = b"recovery-emergency-phrase-deterministic";
const SECRET_SENTINEL: &str = "S3NTINEL-maho-vault-library-contract";

fn unlocked_core(dir: &tempfile::TempDir, name: &str) -> MahoCore {
    maho_storage::sqlite::set_sqlcipher_key("vault-library-contract-test-key")
        .expect("configure SQLCipher key");
    let path = dir.path().join(name);
    let mut core = MahoCore::new().with_storage(path.to_str().expect("test path is UTF-8"));
    core.initialize_vault(MASTER, RECOVERY)
        .expect("initialize and unlock vault");
    core
}

fn origin(value: &str) -> CredentialOrigin {
    CredentialOrigin::try_from(value).expect("valid origin literal")
}

fn login_input(origin_value: &str, username: &str, password: &str) -> VaultLoginInput {
    VaultLoginInput {
        metadata: VaultItemPublicMetadata {
            favorite: false,
            trashed_at: None,
            has_notes: false,
            title: "Library account".to_string(),
            origins: vec![origin(origin_value)],
            username_hint: String::new(),
            item_kind: VaultItemKind::Login,
            totp: None,
            passkey: None,
        },
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
        limit: 50,
    }
}

fn search_request(origin_value: &str) -> VaultItemSearchRequest {
    VaultItemSearchRequest {
        schema_version: VaultSchemaVersion::CURRENT,
        origin: origin(origin_value),
        provider: None,
        kinds: Vec::new(),
    }
}

fn assert_metadata_only_json(value: &Value) {
    let rendered = value.to_string();
    assert!(
        !rendered.contains(SECRET_SENTINEL),
        "public Vault payload leaked a secret sentinel"
    );
    assert_no_secret_keys(value);
}

fn assert_no_secret_keys(value: &Value) {
    match value {
        Value::Object(object) => {
            for (key, nested) in object {
                assert!(
                    !matches!(
                        key.as_str(),
                        "password"
                            | "username"
                            | "secret"
                            | "encryptedPayload"
                            | "envelope"
                            | "ciphertext"
                            | "nonce"
                            | "tag"
                            | "privateKey"
                            | "seed"
                    ),
                    "public Vault payload carried a secret-bearing field"
                );
                assert_no_secret_keys(nested);
            }
        }
        Value::Array(items) => {
            for item in items {
                assert_no_secret_keys(item);
            }
        }
        Value::Null | Value::Bool(_) | Value::Number(_) | Value::String(_) => {}
    }
}

// --- Regression lock: list/search remain metadata-only ---------------------

#[test]
fn vault_library_lock_list_and_search_json_remain_metadata_only() {
    // Given a Vault login whose stored secret must never appear in list/search.
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir, "list-search-metadata-only.sqlite");
    let added = core
        .vault_add_login(login_input(
            "https://library.example",
            "person@example.test",
            SECRET_SENTINEL,
        ))
        .expect("add login through canonical Vault API");

    // When the library surface reads list and search metadata.
    let listed = core.vault_list_items(&list_all()).expect("list metadata");
    let searched = core
        .vault_search_items(&search_request("https://library.example"))
        .expect("search metadata");

    // Then the observable JSON shape is metadata-only and stable by item ID.
    assert_eq!(
        listed.iter().map(|item| item.id).collect::<Vec<_>>(),
        vec![added.id]
    );
    assert_eq!(
        searched.iter().map(|item| item.id).collect::<Vec<_>>(),
        vec![added.id]
    );
    assert_metadata_only_json(&serde_json::to_value(&listed).expect("serialize list DTO"));
    assert_metadata_only_json(&serde_json::to_value(&searched).expect("serialize search DTO"));
}

#[test]
fn vault_library_lock_secret_use_and_delete_do_not_pollute_list_search_json() {
    // Given a login that is updated with a new secret and then consumed once.
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir, "secret-use-metadata-only.sqlite");
    let added = core
        .vault_add_login(login_input(
            "https://use.example",
            "person@example.test",
            "initial",
        ))
        .expect("add login through canonical Vault API");
    let update = VaultLoginUpdate {
        notes: None,
        metadata: VaultItemPublicMetadata {
            favorite: false,
            trashed_at: None,
            has_notes: false,
            title: "Library account renamed".to_string(),
            origins: vec![origin("https://use.example")],
            username_hint: String::new(),
            item_kind: VaultItemKind::Login,
            totp: None,
            passkey: None,
        },
        username: "renamed@example.test".to_string(),
        password: Some(Zeroizing::new(SECRET_SENTINEL.to_string())),
        form_details: None,
    };
    let updated = core
        .vault_update_login(added.id, added.revision, update)
        .expect("update login through canonical Vault API");
    let used = core
        .vault_use_login_password(updated.id)
        .expect("use secret through canonical Vault API");
    assert_eq!(used.as_str(), SECRET_SENTINEL);

    // When the item is listed, searched, and then tombstoned.
    let listed_after_use = core.vault_list_items(&list_all()).expect("list after use");
    let searched_after_use = core
        .vault_search_items(&search_request("https://use.example"))
        .expect("search after use");
    let latest_revision = listed_after_use[0].revision;
    core.vault_delete_item(updated.id, latest_revision)
        .expect("delete through canonical Vault API");
    let listed_after_delete = core
        .vault_list_items(&list_all())
        .expect("list after delete");
    let searched_after_delete = core
        .vault_search_items(&search_request("https://use.example"))
        .expect("search after delete");

    // Then list/search payloads never expose the secret and tombstones disappear.
    assert_metadata_only_json(
        &serde_json::to_value(&listed_after_use).expect("serialize used list DTO"),
    );
    assert_metadata_only_json(
        &serde_json::to_value(&searched_after_use).expect("serialize used search DTO"),
    );
    assert!(listed_after_delete.is_empty());
    assert!(searched_after_delete.is_empty());
    assert!(matches!(
        core.vault_use_login_password(updated.id),
        Err(VaultCrudError::ItemNotFound)
    ));
}
