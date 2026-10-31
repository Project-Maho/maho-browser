//! Todo 11 - durable Vault CRUD, origin matching, usage metadata, fail-closed
//! persistence. Behavioral, secret-free coverage driven through the privileged
//! `MahoCore` surface against a real in-memory/on-disk SQLCipher database.
//!
//! All test names are selectable with `vault_crud`.

use maho_core::maho_core::MahoCore;
use maho_core::vault_manager::{
    OriginMatchPolicy, VaultCrudError, VaultLoginInput, VaultLoginUpdate, VaultPasskeyInput,
    VaultSecureItemInput, VaultTotpInput,
};
use maho_types::vault::{
    CredentialOrigin, TotpAlgorithm, TotpDigits, TotpMetadata, TotpPeriodSeconds, VaultItemKind,
    VaultItemListRequest, VaultItemPublicMetadata, VaultItemSearchRequest, VaultRevision,
    VaultSchemaVersion,
};
use zeroize::Zeroizing;

const MASTER: &[u8] = b"correct horse battery staple";
const RECOVERY: &[u8] = b"recovery-emergency-phrase-deterministic";
const SENTINEL: &str = "S3NTINEL-maho-vault-9F4C";

fn unlocked_core(dir: &tempfile::TempDir, name: &str) -> MahoCore {
    maho_storage::sqlite::set_sqlcipher_key("vault-crud-test-key").expect("configure key");
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

fn totp_meta() -> VaultItemPublicMetadata {
    VaultItemPublicMetadata {
        favorite: false,
        trashed_at: None,
        has_notes: false,
        title: "Example TOTP".to_string(),
        origins: origins(&["https://example.com"]),
        username_hint: String::new(),
        item_kind: VaultItemKind::Totp,
        totp: Some(TotpMetadata {
            issuer: "Example".to_string(),
            account_label: "alice".to_string(),
            algorithm: TotpAlgorithm::Sha1,
            digits: TotpDigits::Six,
            period_seconds: TotpPeriodSeconds::new(30),
            created_at: chrono::Utc::now(),
            last_used_at: None,
        }),
        passkey: None,
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

fn search(origin: &str) -> VaultItemSearchRequest {
    VaultItemSearchRequest {
        schema_version: VaultSchemaVersion::CURRENT,
        origin: CredentialOrigin::try_from(origin).expect("origin"),
        provider: None,
        kinds: Vec::new(),
    }
}

// --- add / list secret-free DTO + ciphertext-only row ----------------------

#[test]
fn vault_crud_create_list_and_exact_search_deny_list_after_lock() {
    // Given an unlocked vault with a login that has a canonicalizable origin.
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "create-query-delegation.sqlite");

    // When the login is added, listed, and searched by its exact origin.
    let added = core
        .vault_add_login(login_input(
            "Maho",
            &["https://EXAMPLE.com:443"],
            "alice",
            SENTINEL,
        ))
        .expect("add login");
    let listed = core.vault_list_items(&list_all()).expect("list items");
    let searched = core
        .vault_search_items(&search("https://example.com"))
        .expect("exact search");

    // Then every public create/query entry point observes the same active item.
    assert_eq!(
        listed.iter().map(|item| item.id).collect::<Vec<_>>(),
        vec![added.id]
    );
    assert_eq!(
        searched.iter().map(|item| item.id).collect::<Vec<_>>(),
        vec![added.id]
    );

    // When the vault is locked, list denies the request before reading rows.
    core.lock_vault().expect("lock vault");

    // Then the public list surface remains fail-closed.
    assert!(matches!(
        core.vault_list_items(&list_all()),
        Err(VaultCrudError::Locked)
    ));
}

#[test]
fn vault_crud_add_login_returns_secret_free_public_dto_and_ciphertext_only_row() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "add.sqlite");
    let dto = core
        .vault_add_login(login_input(
            "GitHub",
            &["https://github.com"],
            "octocat",
            SENTINEL,
        ))
        .expect("add login");

    assert_eq!(dto.item_kind, VaultItemKind::Login);
    assert_eq!(dto.revision, VaultRevision::new(1));
    assert_eq!(dto.origins, origins(&["https://github.com"]));
    // Masked hint present, real username absent from any public field.
    assert!(!dto.username_hint.contains("octocat") || dto.username_hint.contains('*'));
    let public_json = serde_json::to_string(&dto).unwrap();
    assert!(!public_json.contains(SENTINEL), "no secret in public DTO");

    // Raw storage row is ciphertext-only: opaque envelope, no plaintext columns.
    let row = core
        .storage_ref()
        .unwrap()
        .get_vault_item(&dto.id.to_string())
        .unwrap()
        .expect("row present");
    assert!(!row.envelope.is_empty());
    assert!(
        !contains_bytes(&row.envelope, SENTINEL.as_bytes()),
        "ciphertext envelope must not contain plaintext secret"
    );
    assert!(!row.provider.contains(SENTINEL));
    assert!(!row.item_kind.contains(SENTINEL));
    assert!(row.deleted_at.is_none());
    // list returns the same secret-free DTO.
    let listed = core.vault_list_items(&list_all()).expect("list");
    assert_eq!(listed.len(), 1);
    assert_eq!(listed[0].id, dto.id);
}

fn contains_bytes(haystack: &[u8], needle: &[u8]) -> bool {
    haystack
        .windows(needle.len())
        .any(|window| window == needle)
}

// --- per-kind secret round trips through privileged use --------------------

#[test]
fn vault_crud_login_password_round_trips_through_use() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "login-use.sqlite");
    let dto = core
        .vault_add_login(login_input(
            "Site",
            &["https://site.example"],
            "alice",
            SENTINEL,
        ))
        .unwrap();
    let password = core.vault_use_login_password(dto.id).expect("use login");
    assert_eq!(password.as_str(), SENTINEL);
}

#[test]
fn vault_crud_totp_seed_round_trips_through_use() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "totp-use.sqlite");
    let seed = vec![1u8, 2, 3, 4, 5, 6, 7, 8, 9, 10];
    let dto = core
        .vault_add_totp(VaultTotpInput {
            metadata: totp_meta(),
            seed: Zeroizing::new(seed.clone()),
        })
        .expect("add totp");
    assert_eq!(dto.item_kind, VaultItemKind::Totp);
    let recovered = core.vault_use_totp_seed(dto.id).expect("use totp");
    assert_eq!(recovered.as_slice(), seed.as_slice());
}

#[test]
fn vault_crud_totp_code_generation_rfc6238() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "totp-gen.sqlite");
    // Standard RFC 6238 seed "12345678901234567890" ASCII bytes
    let seed = b"12345678901234567890".to_vec();
    let dto = core
        .vault_add_totp(VaultTotpInput {
            metadata: totp_meta(),
            seed: Zeroizing::new(seed),
        })
        .expect("add totp");

    let code = core
        .vault_generate_totp_code(dto.id, 59)
        .expect("generate code");
    assert_eq!(code.len(), 6);
    // RFC 6238 test vector for T=59 is "287082"
    assert_eq!(*code, "287082");
}

#[test]
fn vault_crud_totp_generation_rejects_missing_metadata_without_recording_use() {
    // Given a TOTP seed whose encrypted record has no generation metadata.
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "totp-missing-metadata.sqlite");
    let mut metadata = totp_meta();
    metadata.totp = None;
    let dto = core
        .vault_add_totp(VaultTotpInput {
            metadata,
            seed: Zeroizing::new(b"12345678901234567890".to_vec()),
        })
        .expect("add TOTP without metadata");

    // When generation attempts to consume the malformed TOTP record.
    let result = core.vault_generate_totp_code(dto.id, 59);

    // Then it fails typed and does not record a successful use.
    assert!(matches!(result, Err(VaultCrudError::MalformedPayload)));
    let listed = core.vault_list_items(&list_all()).expect("list TOTP");
    assert_eq!(listed[0].revision, VaultRevision::new(1));
    assert!(listed[0].last_used_at.is_none());
}

#[test]
fn vault_crud_totp_generation_rejects_zero_period_metadata_without_recording_use() {
    // Given a TOTP record with structurally present but unusable metadata.
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "totp-zero-period.sqlite");
    let mut metadata = totp_meta();
    metadata
        .totp
        .as_mut()
        .expect("TOTP metadata present")
        .period_seconds = TotpPeriodSeconds::new(0);
    let dto = core
        .vault_add_totp(VaultTotpInput {
            metadata,
            seed: Zeroizing::new(b"12345678901234567890".to_vec()),
        })
        .expect("add TOTP with zero period");

    // When generation attempts to consume the malformed TOTP metadata.
    let result = core.vault_generate_totp_code(dto.id, 59);

    // Then it fails typed instead of falling back to a default period.
    assert!(matches!(result, Err(VaultCrudError::MalformedPayload)));
    let listed = core.vault_list_items(&list_all()).expect("list TOTP");
    assert_eq!(listed[0].revision, VaultRevision::new(1));
    assert!(listed[0].last_used_at.is_none());
}

#[test]
fn vault_crud_totp_code_generation_honors_algorithm_digits_and_period_metadata() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "totp-metadata-gen.sqlite");
    let mut metadata = totp_meta();
    let totp = metadata.totp.as_mut().expect("TOTP metadata present");
    totp.algorithm = TotpAlgorithm::Sha256;
    totp.digits = TotpDigits::Eight;
    totp.period_seconds = TotpPeriodSeconds::new(60);
    let dto = core
        .vault_add_totp(VaultTotpInput {
            metadata,
            seed: Zeroizing::new(b"12345678901234567890123456789012".to_vec()),
        })
        .expect("add TOTP");

    let code = core
        // At 60 seconds, the configured 60-second period reaches counter 1.
        .vault_generate_totp_code(dto.id, 60)
        .expect("generate metadata-configured code");

    assert_eq!(*code, "46119246");
}

#[test]
fn vault_crud_totp_generation_persists_usage_metadata_before_returning_code() {
    // Given an active TOTP item with non-default generation metadata.
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "totp-use-metadata.sqlite");
    let mut metadata = totp_meta();
    let totp = metadata.totp.as_mut().expect("TOTP metadata present");
    totp.algorithm = TotpAlgorithm::Sha256;
    totp.digits = TotpDigits::Eight;
    totp.period_seconds = TotpPeriodSeconds::new(60);
    let dto = core
        .vault_add_totp(VaultTotpInput {
            metadata,
            seed: Zeroizing::new(b"12345678901234567890123456789012".to_vec()),
        })
        .expect("add TOTP");

    // When a code is generated through the privileged use path.
    let code = core
        .vault_generate_totp_code(dto.id, 60)
        .expect("generate metadata-configured code");

    // Then configured metadata is honored and use is durably recorded first.
    assert_eq!(*code, "46119246");
    let listed = core.vault_list_items(&list_all()).expect("list used TOTP");
    assert_eq!(listed[0].revision, VaultRevision::new(2));
    assert!(listed[0].last_used_at.is_some());
}

#[test]
fn vault_crud_passkey_private_key_round_trips_through_use() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "passkey-use.sqlite");
    let key = vec![9u8; 32];
    let mut metadata = login_meta("Passkey", &["https://webauthn.example"]);
    metadata.item_kind = VaultItemKind::Passkey;
    let dto = core
        .vault_add_passkey(VaultPasskeyInput {
            metadata,
            private_key: Zeroizing::new(key.clone()),
        })
        .expect("add passkey");
    let recovered = core.vault_use_passkey_key(dto.id).expect("use passkey");
    assert_eq!(recovered.as_slice(), key.as_slice());
}

#[test]
fn vault_crud_secure_item_bytes_round_trip_through_use() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "secure-use.sqlite");
    let bytes = SENTINEL.as_bytes().to_vec();
    let mut metadata = login_meta("Note", &["https://notes.example"]);
    metadata.item_kind = VaultItemKind::SecureItem;
    let dto = core
        .vault_add_secure_item(VaultSecureItemInput {
            metadata,
            bytes: Zeroizing::new(bytes.clone()),
            notes: Some(Zeroizing::new("private".to_string())),
        })
        .expect("add secure item");
    let recovered = core.vault_use_secure_bytes(dto.id).expect("use secure");
    assert_eq!(recovered.as_slice(), bytes.as_slice());
}

// --- duplicate detection (real username, not masked hint) ------------------

#[test]
fn vault_crud_duplicate_real_username_is_rejected() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "dup.sqlite");
    core.vault_add_login(login_input("A", &["https://dup.example"], "alice", "pw1"))
        .expect("first add");
    let dup = core.vault_add_login(login_input("B", &["https://dup.example"], "alice", "pw2"));
    assert!(matches!(dup, Err(VaultCrudError::Duplicate { .. })));
}

#[test]
fn vault_crud_masked_hint_collision_is_not_duplicate() {
    // "alice" and "annie" both mask to "a***" but are distinct real usernames on
    // the same origin: masked-hint collision must NOT be treated as a duplicate.
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "hint.sqlite");
    core.vault_add_login(login_input("A", &["https://hint.example"], "alice", "pw1"))
        .expect("first add");
    let second = core.vault_add_login(login_input("B", &["https://hint.example"], "annie", "pw2"));
    assert!(second.is_ok(), "distinct real usernames are not duplicates");
}

// --- update: password/domain/username + revision bump ----------------------

#[test]
fn vault_crud_update_password_domain_username_bumps_revision() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "update.sqlite");
    let dto = core
        .vault_add_login(login_input(
            "Old",
            &["https://old.example"],
            "alice",
            "oldpw",
        ))
        .unwrap();

    let mut metadata = login_meta("New", &["https://new.example"]);
    metadata.item_kind = VaultItemKind::Login;
    let updated = core
        .vault_update_login(
            dto.id,
            VaultRevision::new(1),
            VaultLoginUpdate {
                notes: None,
                metadata,
                username: "bob".to_string(),
                password: Some(Zeroizing::new(SENTINEL.to_string())),
                form_details: None,
            },
        )
        .expect("update login");
    assert_eq!(updated.revision, VaultRevision::new(2));
    assert_eq!(updated.origins, origins(&["https://new.example"]));

    let password = core
        .vault_use_login_password(dto.id)
        .expect("use after update");
    assert_eq!(password.as_str(), SENTINEL);
    // use bumps revision again (payload changed via last_used_at).
    let after = core.vault_list_items(&list_all()).unwrap();
    assert_eq!(after[0].revision, VaultRevision::new(3));
}

#[test]
fn vault_crud_stale_revision_update_conflicts_and_preserves_row() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "stale.sqlite");
    let dto = core
        .vault_add_login(login_input(
            "Site",
            &["https://stale.example"],
            "alice",
            "pw",
        ))
        .unwrap();
    let before = core
        .storage_ref()
        .unwrap()
        .get_vault_item(&dto.id.to_string())
        .unwrap()
        .unwrap();

    let mut metadata = login_meta("Site", &["https://stale.example"]);
    metadata.item_kind = VaultItemKind::Login;
    let conflict = core.vault_update_login(
        dto.id,
        VaultRevision::new(99),
        VaultLoginUpdate {
            notes: None,
            metadata,
            username: "alice".to_string(),
            password: Some(Zeroizing::new("newpw".to_string())),
            form_details: None,
        },
    );
    assert!(matches!(
        conflict,
        Err(VaultCrudError::RevisionConflict {
            expected: 99,
            actual: 1
        })
    ));
    let after = core
        .storage_ref()
        .unwrap()
        .get_vault_item(&dto.id.to_string())
        .unwrap()
        .unwrap();
    assert_eq!(
        after.envelope, before.envelope,
        "prior ciphertext preserved byte-identical"
    );
    assert_eq!(after.revision, 1);
}

// --- delete tombstone: row retained, secret destroyed, excluded, count-- ----

#[test]
fn vault_crud_delete_tombstones_row_destroys_secret_and_decrements_count() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "delete.sqlite");
    let dto = core
        .vault_add_login(login_input(
            "Site",
            &["https://del.example"],
            "alice",
            SENTINEL,
        ))
        .unwrap();
    assert_eq!(core.vault_status().item_count, 1);

    core.vault_delete_item(dto.id, VaultRevision::new(1))
        .expect("delete tombstones");

    // Row is retained for sync but excluded from list/search/match and count.
    let row = core
        .storage_ref()
        .unwrap()
        .get_vault_item(&dto.id.to_string())
        .unwrap()
        .expect("tombstone row retained");
    assert!(row.deleted_at.is_some(), "row retained with deleted_at");
    assert!(
        !contains_bytes(&row.envelope, SENTINEL.as_bytes()),
        "tombstone envelope must not carry the live secret"
    );
    assert_eq!(row.revision, 2);
    assert!(core.vault_list_items(&list_all()).unwrap().is_empty());
    assert!(core
        .vault_search_items(&search("https://del.example"))
        .unwrap()
        .is_empty());
    assert_eq!(core.vault_status().item_count, 0);
    // A use on a tombstoned item is not-found.
    assert!(matches!(
        core.vault_use_login_password(dto.id),
        Err(VaultCrudError::ItemNotFound)
    ));
}

// --- use persists last_used_at + revision; locked/failed use unchanged ------

#[test]
fn vault_crud_use_persists_last_used_and_bumps_revision() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "use-meta.sqlite");
    let dto = core
        .vault_add_login(login_input(
            "Site",
            &["https://use.example"],
            "alice",
            SENTINEL,
        ))
        .unwrap();
    assert!(dto.last_used_at.is_none());
    let _ = core.vault_use_login_password(dto.id).unwrap();
    let listed = core.vault_list_items(&list_all()).unwrap();
    assert_eq!(listed[0].revision, VaultRevision::new(2));
    assert!(listed[0].last_used_at.is_some(), "last_used persisted");
}

#[test]
fn vault_crud_locked_denies_before_any_item_operation() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "locked.sqlite");
    let dto = core
        .vault_add_login(login_input(
            "Site",
            &["https://lock.example"],
            "alice",
            SENTINEL,
        ))
        .unwrap();
    core.lock_vault().expect("lock");

    assert!(matches!(
        core.vault_use_login_password(dto.id),
        Err(VaultCrudError::Locked)
    ));
    assert!(matches!(
        core.vault_list_items(&list_all()),
        Err(VaultCrudError::Locked)
    ));
    assert!(matches!(
        core.vault_add_login(login_input("X", &["https://x.example"], "bob", "pw")),
        Err(VaultCrudError::Locked)
    ));
}

#[test]
fn vault_crud_wrong_kind_use_is_rejected() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "wrong-kind.sqlite");
    let dto = core
        .vault_add_login(login_input(
            "Site",
            &["https://kind.example"],
            "alice",
            SENTINEL,
        ))
        .unwrap();
    assert!(matches!(
        core.vault_use_totp_seed(dto.id),
        Err(VaultCrudError::ItemKindMismatch)
    ));
}

#[test]
fn vault_crud_tampered_ciphertext_is_malformed() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "tamper.sqlite");
    let dto = core
        .vault_add_login(login_input(
            "Site",
            &["https://tamper.example"],
            "alice",
            SENTINEL,
        ))
        .unwrap();
    let mut row = core
        .storage_ref()
        .unwrap()
        .get_vault_item(&dto.id.to_string())
        .unwrap()
        .unwrap();
    // Flip a byte inside the stored envelope to simulate tampering.
    let last = row.envelope.len() - 1;
    row.envelope[last] ^= 0xFF;
    core.storage_ref().unwrap().save_vault_item(&row).unwrap();
    assert!(matches!(
        core.vault_use_login_password(dto.id),
        Err(VaultCrudError::MalformedPayload)
    ));
}

// --- origin normalization / exact / subdomain matrix -----------------------

#[test]
fn vault_crud_invalid_origins_are_rejected_not_stripped() {
    // The maho-types `CredentialOrigin` contract already rejects path/query/
    // fragment at construction (defense in depth before the core boundary).
    for contract_rejected in [
        "https://example.com/path",
        "https://example.com?q=1",
        "https://example.com#frag",
    ] {
        assert!(
            CredentialOrigin::try_from(contract_rejected).is_err(),
            "contract must reject {contract_rejected}"
        );
    }
    // Origins that pass the contract but the core trust boundary must reject
    // (userinfo present, unsupported scheme) - rejected, never silently stripped.
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "origins.sqlite");
    for bad in [
        "https://user@example.com",
        "https://user:pass@example.com",
        "ftp://example.com",
    ] {
        let result = core.vault_add_login(login_input("Bad", &[bad], "alice", "pw"));
        assert!(
            matches!(result, Err(VaultCrudError::InvalidOrigin { .. })),
            "origin {bad} must be rejected, got {result:?}"
        );
    }
}

#[test]
fn vault_crud_exact_and_subdomain_match_matrix() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "match.sqlite");
    // Stored canonical origin: default port + uppercase host collapse.
    core.vault_add_login(login_input(
        "Site",
        &["https://EXAMPLE.com:443"],
        "alice",
        "pw",
    ))
    .expect("add");

    // Exact match after canonicalization.
    assert_eq!(
        core.vault_match_items(&search("https://example.com"), OriginMatchPolicy::Exact)
            .unwrap()
            .len(),
        1
    );
    // Exact policy rejects a subdomain page.
    assert!(core
        .vault_match_items(
            &search("https://login.example.com"),
            OriginMatchPolicy::Exact
        )
        .unwrap()
        .is_empty());
    // Subdomain policy accepts a label-boundary child.
    assert_eq!(
        core.vault_match_items(
            &search("https://login.example.com"),
            OriginMatchPolicy::Subdomain
        )
        .unwrap()
        .len(),
        1
    );
    // Public-suffix / sibling attacks are rejected under subdomain policy.
    for attacker in [
        "https://example.com.evil.com",
        "https://notexample.com",
        "http://example.com",
    ] {
        assert!(
            core.vault_match_items(&search(attacker), OriginMatchPolicy::Subdomain)
                .unwrap()
                .is_empty(),
            "attacker origin {attacker} must not match"
        );
    }
}

#[test]
fn vault_crud_idn_origin_normalizes_for_match() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "idn.sqlite");
    core.vault_add_login(login_input("IDN", &["https://münchen.de"], "alice", "pw"))
        .expect("add idn");
    assert_eq!(
        core.vault_search_items(&search("https://münchen.de"))
            .unwrap()
            .len(),
        1,
        "IDN forms must canonicalize identically"
    );
    assert_eq!(
        core.vault_search_items(&search("https://xn--mnchen-3ya.de"))
            .unwrap()
            .len(),
        1,
        "punycode and unicode IDN must match the same item"
    );
}

// --- pagination / filter determinism + reload stability --------------------

#[test]
fn vault_crud_list_filter_and_pagination_are_deterministic() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = unlocked_core(&dir, "page.sqlite");
    for i in 0..5 {
        core.vault_add_login(login_input(
            &format!("S{i}"),
            &[&format!("https://s{i}.example")],
            &format!("user{i}"),
            "pw",
        ))
        .unwrap();
    }
    let mut req = list_all();
    req.limit = 2;
    let page1 = core.vault_list_items(&req).unwrap();
    assert_eq!(page1.len(), 2);
    // Deterministic: two identical requests yield identical ids in order.
    let page1_again = core.vault_list_items(&req).unwrap();
    assert_eq!(
        page1.iter().map(|d| d.id).collect::<Vec<_>>(),
        page1_again.iter().map(|d| d.id).collect::<Vec<_>>()
    );
    // kind filter with no logins-of-other-kind still returns all 5 logins.
    let mut kinds_req = list_all();
    kinds_req.kinds = vec![VaultItemKind::Login];
    assert_eq!(core.vault_list_items(&kinds_req).unwrap().len(), 5);
    let mut totp_req = list_all();
    totp_req.kinds = vec![VaultItemKind::Totp];
    assert!(core.vault_list_items(&totp_req).unwrap().is_empty());
}

#[test]
fn vault_crud_reload_preserves_items_and_item_count() {
    let dir = tempfile::tempdir().unwrap();
    let path = {
        let mut core = unlocked_core(&dir, "reload.sqlite");
        core.vault_add_login(login_input("A", &["https://a.example"], "alice", SENTINEL))
            .unwrap();
        core.vault_add_login(login_input("B", &["https://b.example"], "bob", "pw"))
            .unwrap();
        core.sqlite_db_path().unwrap().to_string()
    };

    let mut reopened = MahoCore::new().with_storage(&path);
    reopened.load_persisted_data().expect("reload");
    // item_count is refreshed while LOCKED without reading any envelope.
    assert_eq!(reopened.vault_status().item_count, 2);
    reopened.unlock_vault(MASTER).expect("unlock after reload");
    let listed = reopened.vault_list_items(&list_all()).unwrap();
    assert_eq!(listed.len(), 2);
    // Secrets still round-trip after reopen.
    let target = listed.iter().find(|d| d.title == "A").unwrap();
    assert_eq!(
        reopened
            .vault_use_login_password(target.id)
            .unwrap()
            .as_str(),
        SENTINEL
    );
}
