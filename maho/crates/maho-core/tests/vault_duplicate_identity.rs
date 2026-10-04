//! Duplicate identity is shared by create and update, including non-web logins.

use maho_core::maho_core::MahoCore;
use maho_core::vault_manager::{
    VaultCredentialFormDetails, VaultCrudError, VaultLoginInput, VaultLoginUpdate,
};
use maho_types::vault::{CredentialOrigin, VaultItemKind, VaultItemPublicMetadata};
use zeroize::Zeroizing;

const WEB_REALM: &str = "https://identity.example";
const ANDROID_REALM: &str = "android://aGFzaA==@com.example.android";
const OTHER_ANDROID_REALM: &str = "android://b3RoZXI=@com.example.android";

fn unlocked_core(dir: &tempfile::TempDir) -> MahoCore {
    maho_storage::sqlite::set_sqlcipher_key("vault-duplicate-identity-test-key")
        .expect("configure SQLCipher key");
    let path = dir.path().join("vault.sqlite");
    let mut core = MahoCore::new().with_storage(path.to_str().expect("UTF-8 path"));
    core.initialize_vault(
        b"correct horse battery staple",
        b"recovery-emergency-phrase-deterministic",
    )
    .expect("initialize and unlock vault");
    core
}

fn form(realm: &str) -> VaultCredentialFormDetails {
    let mut details = VaultCredentialFormDetails::default();
    details.signon_realm = realm.to_string();
    details.username_element = "username".to_string();
    details.password_element = "password".to_string();
    details.form_data = "cGlja2xl".to_string();
    details
}

fn login(username: &str, realm: &str) -> VaultLoginInput {
    VaultLoginInput {
        metadata: VaultItemPublicMetadata {
            favorite: false,
            trashed_at: None,
            has_notes: false,
            title: "Login".to_string(),
            origins: if realm.starts_with("android://") {
                vec![]
            } else {
                vec![CredentialOrigin::try_from(WEB_REALM).expect("valid origin")]
            },
            username_hint: String::new(),
            item_kind: VaultItemKind::Login,
            totp: None,
            passkey: None,
        },
        username: username.to_string(),
        password: Zeroizing::new("original-password".to_string()),
        form_details: Some(form(realm)),
    }
}

fn update(input: VaultLoginInput) -> VaultLoginUpdate {
    VaultLoginUpdate {
        notes: None,
        metadata: input.metadata,
        username: input.username,
        password: Some(Zeroizing::new("replacement-password".to_string())),
        form_details: input.form_details,
    }
}

fn assert_distinct_forms_allow_password_update(realm: &str, retain_details: bool) {
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    // Each pair differs in exactly one identity field, not all three at once.
    for field in ["username_element", "password_element", "signon_realm"] {
        // Given two persisted forms sharing the same username and origins.
        let input = login(field, realm);
        let target = core
            .vault_add_login(login(field, realm))
            .expect("add target");
        let mut sibling_input = login(field, realm);
        let details = sibling_input.form_details.as_mut().expect("form details");
        match field {
            "username_element" => details.username_element = "other-user".to_string(),
            "password_element" => details.password_element = "other-password".to_string(),
            "signon_realm" => {
                details.signon_realm = if realm == ANDROID_REALM {
                    OTHER_ANDROID_REALM.to_string()
                } else {
                    format!("{realm}/http-auth-realm")
                };
            }
            _ => unreachable!("fixed test cases"),
        }
        sibling_input.password = Zeroizing::new("sibling-password".to_string());
        let sibling = core
            .vault_add_login(sibling_input)
            .expect("add distinct form");
        let session = core
            .create_vault_backend_session()
            .expect("backend session");
        let before = session
            .login_credentials(None, None)
            .expect("read stored forms");
        let expected_details = serde_json::to_value(&input.form_details).expect("form JSON");
        let mut replacement = update(input);
        if retain_details {
            replacement.form_details = None;
        }

        // When only the target's password changes, with supplied or retained details.
        let updated = core
            .vault_update_login(target.id, target.revision, replacement)
            .expect("password-only update must allow a distinct sibling form");

        // Then the target retains its identity and the sibling is untouched.
        assert_eq!(target.id, updated.id);
        assert_eq!(target.revision.value() + 1, updated.revision.value());
        let after = session
            .login_credentials(None, None)
            .expect("read updated forms");
        assert_eq!(before.len(), after.len());
        let target_after = after
            .iter()
            .find(|item| item.item_id == target.id)
            .expect("target");
        assert_eq!(updated.revision, target_after.observed_revision);
        assert_eq!(
            expected_details,
            serde_json::to_value(&target_after.form_details).expect("stored form JSON")
        );
        let sibling_before = before
            .iter()
            .find(|item| item.item_id == sibling.id)
            .expect("sibling");
        let sibling_after = after
            .iter()
            .find(|item| item.item_id == sibling.id)
            .expect("sibling");
        assert_eq!(
            serde_json::to_value(sibling_before).expect("sibling before JSON"),
            serde_json::to_value(sibling_after).expect("sibling after JSON")
        );
        assert_eq!(
            "replacement-password",
            core.vault_use_login_password(target.id)
                .expect("target password")
                .as_str()
        );
        assert_eq!(
            "sibling-password",
            core.vault_use_login_password(sibling.id)
                .expect("sibling password")
                .as_str()
        );
    }
}

#[test]
fn web_password_update_allows_each_distinct_form_identity() {
    assert_distinct_forms_allow_password_update(WEB_REALM, false);
}

#[test]
fn web_password_update_uses_stored_identity_when_details_are_absent() {
    assert_distinct_forms_allow_password_update(WEB_REALM, true);
}

#[test]
fn android_password_update_allows_each_distinct_form_identity_without_origins() {
    assert_distinct_forms_allow_password_update(ANDROID_REALM, false);
}

#[test]
fn android_password_update_uses_stored_identity_without_origins() {
    assert_distinct_forms_allow_password_update(ANDROID_REALM, true);
}

fn assert_identical_form_rejected(realm: &str, is_update: bool) {
    let dir = tempfile::tempdir().expect("temporary directory");
    let mut core = unlocked_core(&dir);
    // Given an existing identity (Android inputs deliberately have no web origins).
    let existing = core
        .vault_add_login(login("alice", realm))
        .expect("add existing");
    let target = if is_update {
        Some(
            core.vault_add_login(login("bob", realm))
                .expect("add update target"),
        )
    } else {
        None
    };
    let session = core
        .create_vault_backend_session()
        .expect("backend session");
    let before = session
        .login_credentials(None, None)
        .expect("read before collision");

    // When another create or update would acquire exactly the existing identity.
    let result = match &target {
        Some(target) => {
            let mut replacement = update(login(" alice ", realm));
            // Duplicate detection must use bob's stored details, not treat None
            // as an identity-less update that can evade the Android collision.
            replacement.form_details = None;
            core.vault_update_login(target.id, target.revision, replacement)
        }
        None => core.vault_add_login(login(" alice ", realm)),
    };

    // Then the conflict names the existing item and neither record changes.
    assert!(matches!(result, Err(VaultCrudError::Duplicate { existing: id }) if id == existing.id));
    let after = session
        .login_credentials(None, None)
        .expect("read after collision");
    assert_eq!(before.len(), after.len());
    for item in &before {
        let stored = after
            .iter()
            .find(|stored| stored.item_id == item.item_id)
            .expect("unchanged item");
        assert_eq!(
            serde_json::to_value(item).expect("before JSON"),
            serde_json::to_value(stored).expect("after JSON")
        );
        assert_eq!(
            "original-password",
            core.vault_use_login_password(item.item_id)
                .expect("unchanged password")
                .as_str()
        );
    }
}

#[test]
fn identical_android_form_create_is_rejected_without_origins() {
    assert_identical_form_rejected(ANDROID_REALM, false);
}

#[test]
fn identical_android_form_update_is_rejected_using_stored_details_without_origins() {
    assert_identical_form_rejected(ANDROID_REALM, true);
}

#[test]
fn identical_web_form_create_is_rejected() {
    assert_identical_form_rejected(WEB_REALM, false);
}

#[test]
fn identical_web_form_update_is_rejected() {
    assert_identical_form_rejected(WEB_REALM, true);
}
