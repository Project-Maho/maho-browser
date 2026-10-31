use maho_core::maho_core::MahoCore;
use maho_core::vault_manager::{
    VaultAlternativeElement, VaultCredentialFormDetails, VaultCredentialNote, VaultCrudError,
    VaultLoginInput, VaultLoginUpdate,
};
use maho_core::vault_runtime::session::{VaultBackendSecretAction, VaultBackendSessionError};
use maho_types::vault::{
    CredentialOrigin, VaultBatchReadRequest, VaultItemId, VaultItemKind, VaultItemPublicMetadata,
    VaultRevision, VaultSchemaVersion,
};
use zeroize::Zeroizing;

const MASTER: &[u8] = b"correct horse battery staple";
const RECOVERY: &[u8] = b"recovery-emergency-phrase-deterministic";
const SENTINEL: &str = "S3NTINEL-maho-vault-9F4C";
const ANDROID_REALM: &str = "android://aGFzaA==@com.example.android";
const OTHER_ANDROID_REALM: &str = "android://b3RoZXI=@com.example.android";

fn assert_send_sync<T: Send + Sync>() {}

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

fn login_with_realm(
    title: &str,
    web_origin: &str,
    username: &str,
    signon_realm: &str,
) -> VaultLoginInput {
    let mut input = login(title, web_origin, username);
    let mut details = VaultCredentialFormDetails::default();
    details.signon_realm = signon_realm.to_string();
    details.url = format!("{web_origin}/login");
    input.form_details = Some(details);
    input
}

#[test]
fn vault_backend_session_moves_to_a_worker_and_reads_without_moving_core() {
    // Given an unlocked core with a persisted Vault item and a backend session snapshot.
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    let item = core
        .vault_add_login(login("Worker", "https://worker.example", "worker"))
        .expect("add item");
    let session = core
        .create_vault_backend_session()
        .expect("create backend session");
    let request = VaultBatchReadRequest {
        schema_version: VaultSchemaVersion::CURRENT,
        item_ids: vec![item.id],
    };

    // When the session, rather than MahoCore, is moved to a worker thread.
    assert_send_sync::<maho_core::vault_runtime::session::VaultBackendSession>();
    let result = std::thread::spawn(move || session.batch_read(&request))
        .join()
        .expect("worker must not panic")
        .expect("worker batch read");

    // Then the worker receives the same secret-free public DTO through the runtime seam.
    assert_eq!(result.items.len(), 1);
    assert_eq!(result.items[0].id, item.id);
}

#[test]
fn vault_backend_session_discovers_secret_free_identity_and_revision() {
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    let item = core
        .vault_add_login(login(
            "Discovery",
            "https://discovery.example",
            "discover@example.test",
        ))
        .expect("add login");
    let session = core
        .create_vault_backend_session()
        .expect("create backend session");

    let credentials = session
        .login_credentials(Some("https://discovery.example"), None)
        .expect("discover credentials");
    assert_eq!(1, credentials.len());
    assert_eq!(item.id, credentials[0].item_id);
    assert_eq!(item.revision, credentials[0].observed_revision);

    let serialized = serde_json::to_string(&credentials).expect("serialize discovery metadata");
    assert!(serialized.contains(&item.id.to_string()));
    assert!(serialized.contains("\"observedRevision\":\"1\""));
    assert!(!serialized.contains(SENTINEL));
    assert!(!serialized.contains("\"password\""));
    assert!(!serialized.contains("\"secret\""));
}

#[test]
fn vault_backend_session_resolves_exact_fill_revision_and_rejects_stale_or_wrong_item() {
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    let item = core
        .vault_add_login(login("Resolve", "https://resolve.example", "resolver"))
        .expect("add login");
    let session = core
        .create_vault_backend_session()
        .expect("create backend session");

    let secret = session
        .resolve_login_secret(item.id, item.revision, VaultBackendSecretAction::Fill)
        .expect("resolve exact observed revision for fill");
    assert_eq!(SENTINEL, secret.as_str());
    drop(secret);

    let stale =
        session.resolve_login_secret(item.id, item.revision, VaultBackendSecretAction::Fill);
    assert!(matches!(
        stale,
        Err(VaultBackendSessionError::Batch(
            VaultCrudError::RevisionConflict {
                expected: 1,
                actual: 2
            }
        ))
    ));

    let wrong_item = session.resolve_login_secret(
        VaultItemId::new(),
        VaultRevision::new(1),
        VaultBackendSecretAction::Fill,
    );
    assert!(matches!(
        wrong_item,
        Err(VaultBackendSessionError::Batch(
            VaultCrudError::ItemNotFound
        ))
    ));
}

#[test]
fn vault_backend_session_fails_closed_after_core_drop() {
    // Given a session snapshot created from an unlocked core.
    let dir = tempfile::tempdir().expect("temporary directory");
    let core = unlocked_core(&dir);
    let session = core
        .create_vault_backend_session()
        .expect("create backend session");
    let request = VaultBatchReadRequest {
        schema_version: VaultSchemaVersion::CURRENT,
        item_ids: vec![],
    };

    // When its owning core and runtime are dropped.
    drop(core);
    let result = session.batch_read(&request);

    // Then the weak runtime snapshot reports a typed unavailable error.
    assert!(matches!(
        result,
        Err(VaultBackendSessionError::RuntimeUnavailable)
    ));
}

#[test]
fn vault_backend_session_denies_batch_reads_after_vault_lock() {
    // Given a session snapshot created while the Vault is unlocked.
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    let session = core
        .create_vault_backend_session()
        .expect("create backend session");
    let request = VaultBatchReadRequest {
        schema_version: VaultSchemaVersion::CURRENT,
        item_ids: vec![],
    };

    // When the core locks the runtime after session creation.
    core.lock_vault().expect("lock vault");
    let result = session.batch_read(&request);

    // Then the session performs no backend access and returns the existing lock error.
    assert!(matches!(
        result,
        Err(VaultBackendSessionError::Batch(VaultCrudError::Locked))
    ));
}

// Task 4: the browser-form detail written with a login must survive the
// encrypted-envelope round-trip and reach the backend session's credential DTO.
//
// Failing-first rationale: before Task 4 `VaultRecordPayload` had no
// `form_details` field at all, so this test could not compile, and the
// credential DTO carried only realm/username/password/timestamps — every
// assertion below had no field to read.
#[test]
fn vault_backend_session_round_trips_credential_form_details() {
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    let mut input = login("Detail", "https://detail.example", "detail");
    // Field-by-field assignment (not functional-update syntax): the details type
    // is `ZeroizeOnDrop`, so it cannot be moved out of a `Default` instance.
    let mut details = VaultCredentialFormDetails::default();
    details.scheme = 1;
    details.signon_realm = "https://detail.example".to_string();
    details.url = "https://detail.example/login".to_string();
    details.action = "https://detail.example/submit".to_string();
    details.username_element = "user".to_string();
    details.password_element = "pass".to_string();
    details.blocked_by_user = true;
    details.times_used_in_html_form = 5;
    details.match_type = Some(4);
    details.in_store = 1;
    details.form_data = "cGlja2xl".to_string();
    let mut note = VaultCredentialNote::default();
    note.unique_display_name = "note".to_string();
    note.value = "note-value".to_string();
    details.notes = vec![note];
    let mut alternative = VaultAlternativeElement::default();
    alternative.value = "alt@detail.example".to_string();
    alternative.field_renderer_id = 7;
    alternative.name = "alt".to_string();
    details.all_alternative_usernames = vec![alternative];
    input.form_details = Some(details);
    core.vault_add_login(input).expect("add login");

    let session = core
        .create_vault_backend_session()
        .expect("create backend session");
    let credentials = session
        .login_credentials(Some("https://detail.example"), None)
        .expect("read credentials");

    assert_eq!(1, credentials.len());
    let details = credentials[0]
        .form_details
        .as_ref()
        .expect("form details survive the envelope");
    assert_eq!(1, details.scheme);
    assert_eq!("https://detail.example/login", details.url);
    assert_eq!("https://detail.example/submit", details.action);
    assert_eq!("user", details.username_element);
    assert_eq!("pass", details.password_element);
    assert!(details.blocked_by_user);
    assert_eq!(5, details.times_used_in_html_form);
    assert_eq!(Some(4), details.match_type);
    assert_eq!(1, details.in_store);
    assert_eq!("cGlja2xl", details.form_data);
    assert_eq!(1, details.notes.len());
    assert_eq!("note-value", details.notes[0].value);
    assert_eq!(1, details.all_alternative_usernames.len());
    assert_eq!(7, details.all_alternative_usernames[0].field_renderer_id);
    // The stored realm is authoritative for the credential's signon realm.
    assert_eq!("https://detail.example", credentials[0].signon_realm);
}

// Task 4 back-compat: a login written WITHOUT form details (every pre-Task-4
// record, plus settings/importer writes) still reads, reporting `None` rather
// than failing the decode.
//
// Failing-first rationale: this pins the `#[serde(default)]` back-compat
// decision. Without it, `deserialize_record` would reject a legacy payload that
// lacks `formDetails` and the whole read would fail closed.
#[test]
fn vault_backend_session_reads_legacy_records_without_form_details() {
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    core.vault_add_login(login("Legacy", "https://legacy.example", "legacy"))
        .expect("add login");

    let session = core
        .create_vault_backend_session()
        .expect("create backend session");
    let credentials = session
        .login_credentials(Some("https://legacy.example"), None)
        .expect("read credentials");

    assert_eq!(1, credentials.len());
    assert!(credentials[0].form_details.is_none());
    // The legacy realm fallback (first canonical origin) still applies.
    assert_eq!("https://legacy.example", credentials[0].signon_realm);
}

#[test]
fn vault_backend_session_matches_http_auth_realms_exactly_without_merging_origins() {
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    // Chromium PasswordForm::Scheme: kBasic = 1, kDigest = 2.
    for (scheme, origin) in [(1, "http://basic.example"), (2, "https://digest.example")] {
        let mut expected = Vec::new();
        // LoginHandler::GetSignonRealm appends the challenge realm verbatim,
        // so case, trailing slashes, and URL-like suffixes remain identity.
        for suffix in ["Members", "members", "Members/", "Members?view=1#section"] {
            let realm = format!("{origin}/{suffix}");
            let mut input = login_with_realm("HTTP auth", origin, "auth-user", &realm);
            input.form_details.as_mut().expect("form details").scheme = scheme;
            let item = core
                .vault_add_login(input)
                .expect("add distinct auth realm");
            expected.push((realm, item.id));
        }
        // An HTML record with identical form-detail text must not be mistaken
        // for an HTTP auth credential just because its realm matches exactly.
        let html = core
            .vault_add_login(login_with_realm(
                "HTML",
                origin,
                "html-user",
                &expected[0].0,
            ))
            .expect("add HTML login");
        let legacy = core
            .vault_add_login(login("Legacy", origin, "legacy-user"))
            .expect("add legacy login");
        let session = core
            .create_vault_backend_session()
            .expect("create backend session");

        for (realm, id) in expected {
            let credentials = session
                .login_credentials(Some(&realm), None)
                .expect("discover exact HTTP auth realm");
            assert_eq!(1, credentials.len(), "realm {realm}");
            assert_eq!(id, credentials[0].item_id);
            assert_eq!(realm, credentials[0].signon_realm);
            assert_eq!(
                scheme,
                credentials[0]
                    .form_details
                    .as_ref()
                    .expect("form details")
                    .scheme
            );
        }
        let canonical_query = if scheme == 1 {
            "http://BASIC.example:80/"
        } else {
            "https://DIGEST.example:443/"
        };
        for query in [origin, canonical_query] {
            let credentials = session
                .login_credentials(Some(query), None)
                .expect("discover canonical HTML origin");
            assert_eq!(2, credentials.len());
            assert!(credentials
                .iter()
                .any(|credential| credential.item_id == html.id));
            assert!(credentials
                .iter()
                .any(|credential| credential.item_id == legacy.id));
        }
    }
}

#[test]
fn vault_backend_session_matches_android_terminal_slash_without_normalizing_identity() {
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    let mut expected = Vec::new();
    // Chromium affiliation_utils_unittest.cc::ValidAndroidFacetURIs accepts
    // "android://hash@com.example.android/"; the stored signon realm stays exact.
    for realm in [
        ANDROID_REALM.to_string(),
        format!("{ANDROID_REALM}/"),
        format!("{OTHER_ANDROID_REALM}/"),
    ] {
        let mut input = login_with_realm("Android", "https://app.example", "android-user", &realm);
        // Match the Chromium writer: Android credentials have no web origins.
        input.metadata.origins.clear();
        input.form_details.as_mut().expect("form details").url = realm.clone();
        let item = core.vault_add_login(input).expect("add Android login");
        expected.push((realm, item.id));
    }
    let session = core
        .create_vault_backend_session()
        .expect("create backend session");
    for (realm, id) in expected {
        let credentials = session
            .login_credentials(Some(&realm), None)
            .expect("discover exact Android realm");
        assert_eq!(1, credentials.len(), "realm {realm}");
        assert_eq!(id, credentials[0].item_id);
        assert_eq!(realm, credentials[0].signon_realm);
    }
    assert!(session
        .login_credentials(Some(OTHER_ANDROID_REALM), None)
        .expect("query absent unslashed Android realm")
        .is_empty());
}

#[test]
fn vault_backend_session_matches_exact_android_realm_from_encrypted_details() {
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    core.vault_add_login(login_with_realm(
        "Android",
        "https://app.example",
        "android-user",
        ANDROID_REALM,
    ))
    .expect("add Android login");

    let session = core
        .create_vault_backend_session()
        .expect("create backend session");
    let credentials = session
        .login_credentials(Some(ANDROID_REALM), None)
        .expect("read Android credential");

    assert_eq!(1, credentials.len());
    assert_eq!(ANDROID_REALM, credentials[0].signon_realm);
    assert_eq!(
        ANDROID_REALM,
        credentials[0]
            .form_details
            .as_ref()
            .expect("stored form details")
            .signon_realm
    );
}

#[test]
fn vault_backend_session_rejects_distinct_android_realm() {
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    core.vault_add_login(login_with_realm(
        "Android",
        "https://app.example",
        "android-user",
        ANDROID_REALM,
    ))
    .expect("add Android login");

    let session = core
        .create_vault_backend_session()
        .expect("create backend session");
    let credentials = session
        .login_credentials(Some(OTHER_ANDROID_REALM), None)
        .expect("query distinct Android realm");

    assert!(credentials.is_empty());
}

#[test]
fn vault_backend_session_does_not_match_legacy_record_for_android_realm() {
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    core.vault_add_login(login("Legacy", "https://app.example", "legacy-user"))
        .expect("add legacy login");

    let session = core
        .create_vault_backend_session()
        .expect("create backend session");
    let credentials = session
        .login_credentials(Some(ANDROID_REALM), None)
        .expect("query Android realm");

    assert!(credentials.is_empty());
}

#[test]
fn vault_backend_session_keeps_web_matching_metadata_origin_based() {
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    core.vault_add_login(login_with_realm(
        "Android",
        "https://app.example",
        "android-user",
        ANDROID_REALM,
    ))
    .expect("add Android login");

    let session = core
        .create_vault_backend_session()
        .expect("create backend session");
    let credentials = session
        .login_credentials(Some("https://APP.example:443"), None)
        .expect("query canonical web origin");

    assert_eq!(1, credentials.len());
    assert_eq!(ANDROID_REALM, credentials[0].signon_realm);
    assert!(session
        .login_credentials(Some("https://other.example"), None)
        .expect("query other web origin")
        .is_empty());
}

#[test]
fn vault_backend_session_rejects_malformed_android_queries() {
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    core.vault_add_login(login_with_realm(
        "Android",
        "https://app.example",
        "android-user",
        ANDROID_REALM,
    ))
    .expect("add Android login");

    let session = core
        .create_vault_backend_session()
        .expect("create backend session");
    for malformed in [
        "",
        "android://",
        "android://@com.example.android",
        "android://aGFzaA@com.example.android",
        "android://aGFzaA==@com.example.android/path",
        "android://aGFzaA==@com.example.android/path/",
        "android://aGFzaA==@com.example.android//",
        "android://aGFzaA==@com.example.android/?",
        "android://aGFzaA==@com.example.android/?query",
        "android://aGFzaA==@com.example.android/#",
        "android://aGFzaA==@com.example.android/#fragment",
    ] {
        // Reject malformed queries even if encrypted form details contain an
        // exact match; absence of a matching record is not validation coverage.
        core.vault_add_login(login_with_realm(
            "Malformed Android",
            "https://app.example",
            "malformed-user",
            malformed,
        ))
        .expect("store malformed form details");
        assert!(
            session
                .login_credentials(Some(malformed), None)
                .expect("malformed Android query must fail closed")
                .is_empty(),
            "malformed realm {malformed} must not match"
        );
    }
}

// Task 4: an update that carries no form detail must PRESERVE the stored one; a
// settings-originated rename would otherwise silently strip every field the
// store round-trip depends on.
//
// Failing-first rationale: `VaultLoginUpdate` had no `form_details` field before
// Task 4, so an update rewrote the record without it and the assertion below
// (details still present after update) had nothing to preserve.
#[test]
fn vault_update_without_form_details_preserves_stored_details() {
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    let mut input = login("Preserve", "https://preserve.example", "preserve");
    let mut details = VaultCredentialFormDetails::default();
    details.username_element = "kept-user-field".to_string();
    input.form_details = Some(details);
    let item = core.vault_add_login(input).expect("add login");

    let updated = core
        .vault_update_login(
            item.id,
            item.revision,
            VaultLoginUpdate {
                notes: None,
                metadata: VaultItemPublicMetadata {
                    favorite: false,
                    trashed_at: None,
                    has_notes: false,
                    title: "Renamed".to_string(),
                    origins: vec![CredentialOrigin::try_from("https://preserve.example")
                        .expect("valid test origin")],
                    username_hint: String::new(),
                    item_kind: VaultItemKind::Login,
                    totp: None,
                    passkey: None,
                },
                username: "preserve".to_string(),
                password: None,
                form_details: None,
            },
        )
        .expect("update login");
    assert_eq!(item.revision.value() + 1, updated.revision.value());

    let session = core
        .create_vault_backend_session()
        .expect("create backend session");
    let credentials = session
        .login_credentials(Some("https://preserve.example"), None)
        .expect("read credentials");
    assert_eq!(1, credentials.len());
    assert_eq!(
        "kept-user-field",
        credentials[0]
            .form_details
            .as_ref()
            .expect("form details preserved across update")
            .username_element
    );
}
