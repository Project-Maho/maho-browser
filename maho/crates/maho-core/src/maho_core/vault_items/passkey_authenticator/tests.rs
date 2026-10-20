use base64::Engine;
use maho_types::vault::{
    PasskeyRpId, VaultItemKind, VaultItemListRequest, VaultSchemaVersion, VaultTrashFilter,
};
use p256::ecdsa::signature::Verifier;
use p256::ecdsa::{Signature, VerifyingKey};

use crate::maho_core::MahoCore;

const MASTER: &[u8] = b"correct horse battery staple";
const RECOVERY: &[u8] = b"recovery-emergency-phrase-deterministic";

fn test_core(dir: &tempfile::TempDir, name: &str) -> MahoCore {
    crate::install_test_sqlcipher_key();
    let mut core = MahoCore::new().with_storage(dir.path().join(name).to_str().unwrap());
    core.initialize_vault(MASTER, RECOVERY).unwrap();
    core
}

fn rp(value: &str) -> PasskeyRpId {
    PasskeyRpId::try_from(value).unwrap()
}

fn list(core: &MahoCore) -> Vec<maho_types::vault::VaultItemPublicDto> {
    core.vault_list_items(&VaultItemListRequest {
        trash: VaultTrashFilter::Exclude,
        favorites_only: false,
        schema_version: VaultSchemaVersion::CURRENT,
        provider: None,
        kinds: vec![VaultItemKind::Passkey],
        cursor: None,
        limit: 0,
    })
    .unwrap()
}

#[test]
fn passkey_create_assertion_signature_counter_and_reload() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = test_core(&dir, "passkey.sqlite");
    let user_handle = vec![0x17, 0x22, 0x39];
    let created = core
        .vault_passkey_create(
            rp("example.com"),
            base64::engine::general_purpose::URL_SAFE_NO_PAD.encode(&user_handle),
            "alice@example.com".into(),
            "Alice".into(),
            false,
            false,
        )
        .unwrap();
    let items = list(&core);
    let metadata = items
        .iter()
        .find(|item| item.id == created.item_id)
        .unwrap()
        .passkey
        .as_ref()
        .unwrap()
        .clone();
    assert_eq!(metadata.sign_count, 0);
    assert_eq!(metadata.user_handle, user_handle);
    let encoded = &created.cose_key;
    assert_eq!(encoded[0], 0xa5);
    let x = &encoded[10..42];
    let y = &encoded[45..77];
    let mut sec1 = vec![0x04];
    sec1.extend_from_slice(x);
    sec1.extend_from_slice(y);
    let verifying = VerifyingKey::from_sec1_bytes(&sec1).unwrap();
    let hash = [0x5a; 32];
    for expected_count in 1..=2 {
        let assertion = core
            .vault_passkey_get_assertion(
                rp("example.com"),
                hash,
                vec![created.credential_id.clone()],
                false,
                false,
            )
            .unwrap();
        assert_eq!(assertion.user_handle, user_handle);
        assert_eq!(
            u32::from_be_bytes(assertion.authenticator_data[33..37].try_into().unwrap()),
            expected_count
        );
        let mut signed = assertion.authenticator_data.clone();
        signed.extend_from_slice(&hash);
        verifying
            .verify(&signed, &Signature::from_der(&assertion.signature).unwrap())
            .unwrap();
    }
    assert_eq!(
        list(&core)
            .iter()
            .find(|item| item.id == created.item_id)
            .unwrap()
            .passkey
            .as_ref()
            .unwrap()
            .sign_count,
        2
    );
    let path = core.sqlite_db_path().unwrap().to_string();
    drop(core);

    crate::install_test_sqlcipher_key();
    let mut reopened = MahoCore::new().with_storage(&path);
    reopened.load_persisted_data().unwrap();
    reopened.unlock_vault(MASTER).unwrap();
    let assertion = reopened
        .vault_passkey_get_assertion(rp("example.com"), hash, Vec::new(), false, false)
        .unwrap();
    assert_eq!(
        u32::from_be_bytes(assertion.authenticator_data[33..37].try_into().unwrap()),
        3
    );
}

#[test]
fn passkey_rejects_rp_allow_list_and_uv_failures() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = test_core(&dir, "passkey-reject.sqlite");
    let create_denied = core.vault_passkey_create(
        rp("example.com"),
        "AQ".into(),
        "alice".into(),
        "Alice".into(),
        true,
        false,
    );
    assert!(matches!(
        create_denied,
        Err(super::PasskeyError::UserVerificationRequired)
    ));
    let created = core
        .vault_passkey_create(
            rp("example.com"),
            "AQ".into(),
            "alice".into(),
            "Alice".into(),
            false,
            false,
        )
        .unwrap();
    assert!(matches!(
        core.vault_passkey_get_assertion(rp("other.example"), [0; 32], Vec::new(), false, false),
        Err(super::PasskeyError::NotFound)
    ));
    assert!(matches!(
        core.vault_passkey_get_assertion(
            rp("example.com"),
            [0; 32],
            vec![vec![9; 16]],
            false,
            false
        ),
        Err(super::PasskeyError::NotFound)
    ));
    assert!(matches!(
        core.vault_passkey_get_assertion(
            rp("example.com"),
            [0; 32],
            vec![created.credential_id],
            true,
            false
        ),
        Err(super::PasskeyError::UserVerificationRequired)
    ));
}

#[test]
fn passkey_rejects_assertion_when_vault_locked() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = test_core(&dir, "passkey-locked.sqlite");
    let created = core
        .vault_passkey_create(
            rp("example.com"),
            "AQ".into(),
            "alice".into(),
            "Alice".into(),
            false,
            false,
        )
        .unwrap();
    core.lock_vault().unwrap();
    assert!(matches!(
        core.vault_passkey_get_assertion(
            rp("example.com"),
            [0; 32],
            vec![created.credential_id],
            false,
            false
        ),
        Err(super::PasskeyError::Locked)
    ));
}
