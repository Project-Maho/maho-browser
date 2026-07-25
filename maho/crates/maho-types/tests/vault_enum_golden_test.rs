use maho_types::vault::*;
use serde::Serialize;
use serde_json::{json, Value};

fn json<T: Serialize>(value: T) -> Value {
    serde_json::to_value(value).unwrap()
}

#[test]
fn every_vault_enum_has_a_stable_golden_wire_shape() {
    // Given: every variant of every public Vault contract enum.
    let golden = json!({
        "schemaVersions": [1],
        "itemKinds": ["login", "totp", "passkey", "secure_item"],
        "cipherAlgorithms": ["aes_256_gcm"],
        "ciphertextPurposes": ["vault_item", "totp_seed", "passkey_private_key"],
        "lockStates": ["uninitialized", "locked", "unlocked", "auto_locked"],
        "agentPolicies": ["deny", "ask_every_use", "allow_for_task", "while_unlocked", "always_allow"],
        "credentialFields": ["username", "password", "totp", "passkey"],
        "grantStates": ["active", "exhausted", "expired", "revoked"],
        "revocationReasons": ["user", "policy_changed", "vault_locked", "session_ended", "navigation", "expired", "uses_exhausted"],
        "auditOperations": ["initialized", "unlocked", "locked", "item_created", "item_updated", "item_deleted", "grant_issued", "grant_used", "grant_revoked", "fill", "totp_fill", "passkey_used", "import_commit", "policy_changed"],
        "auditDecisions": ["allowed", "denied", "failed"],
        "totpAlgorithms": ["sha1", "sha256", "sha512"],
        "totpDigits": [6, 8],
        "passkeyTransports": ["internal", "usb", "nfc", "ble", "hybrid"],
        "passkeyUserVerification": ["required", "preferred", "discouraged"],
        "errorCodes": ["uninitialized", "locked", "invalid_credentials", "rate_limited", "provider_unavailable", "unsupported_schema_version", "revision_conflict", "invalid_origin", "invalid_grant", "grant_expired", "grant_revoked", "grant_exhausted", "secret_field_forbidden", "not_found", "storage_failure"]
    });

    // When: the complete variant matrix is serialized.
    let actual = json!({
        "schemaVersions": [json(VaultSchemaVersion::V1)],
        "itemKinds": [json(VaultItemKind::Login), json(VaultItemKind::Totp), json(VaultItemKind::Passkey), json(VaultItemKind::SecureItem)],
        "cipherAlgorithms": [json(VaultCipherAlgorithm::Aes256Gcm)],
        "ciphertextPurposes": [json(VaultCiphertextPurpose::VaultItem), json(VaultCiphertextPurpose::TotpSeed), json(VaultCiphertextPurpose::PasskeyPrivateKey)],
        "lockStates": [json(VaultLockState::Uninitialized), json(VaultLockState::Locked), json(VaultLockState::Unlocked), json(VaultLockState::AutoLocked)],
        "agentPolicies": [json(VaultAgentPolicy::Deny), json(VaultAgentPolicy::AskEveryUse), json(VaultAgentPolicy::AllowForTask), json(VaultAgentPolicy::WhileUnlocked), json(VaultAgentPolicy::AlwaysAllow)],
        "credentialFields": [json(CredentialField::Username), json(CredentialField::Password), json(CredentialField::Totp), json(CredentialField::Passkey)],
        "grantStates": [json(CredentialGrantState::Active), json(CredentialGrantState::Exhausted), json(CredentialGrantState::Expired), json(CredentialGrantState::Revoked)],
        "revocationReasons": [json(CredentialRevocationReason::User), json(CredentialRevocationReason::PolicyChanged), json(CredentialRevocationReason::VaultLocked), json(CredentialRevocationReason::SessionEnded), json(CredentialRevocationReason::Navigation), json(CredentialRevocationReason::Expired), json(CredentialRevocationReason::UsesExhausted)],
        "auditOperations": [json(VaultAuditOperation::Initialized), json(VaultAuditOperation::Unlocked), json(VaultAuditOperation::Locked), json(VaultAuditOperation::ItemCreated), json(VaultAuditOperation::ItemUpdated), json(VaultAuditOperation::ItemDeleted), json(VaultAuditOperation::GrantIssued), json(VaultAuditOperation::GrantUsed), json(VaultAuditOperation::GrantRevoked), json(VaultAuditOperation::Fill), json(VaultAuditOperation::TotpFill), json(VaultAuditOperation::PasskeyUsed), json(VaultAuditOperation::ImportCommit), json(VaultAuditOperation::PolicyChanged)],
        "auditDecisions": [json(VaultAuditDecision::Allowed), json(VaultAuditDecision::Denied), json(VaultAuditDecision::Failed)],
        "totpAlgorithms": [json(TotpAlgorithm::Sha1), json(TotpAlgorithm::Sha256), json(TotpAlgorithm::Sha512)],
        "totpDigits": [json(TotpDigits::Six), json(TotpDigits::Eight)],
        "passkeyTransports": [json(PasskeyTransport::Internal), json(PasskeyTransport::Usb), json(PasskeyTransport::Nfc), json(PasskeyTransport::Ble), json(PasskeyTransport::Hybrid)],
        "passkeyUserVerification": [json(PasskeyUserVerification::Required), json(PasskeyUserVerification::Preferred), json(PasskeyUserVerification::Discouraged)],
        "errorCodes": [json(VaultErrorCode::Uninitialized), json(VaultErrorCode::Locked), json(VaultErrorCode::InvalidCredentials), json(VaultErrorCode::RateLimited), json(VaultErrorCode::ProviderUnavailable), json(VaultErrorCode::UnsupportedSchemaVersion), json(VaultErrorCode::RevisionConflict), json(VaultErrorCode::InvalidOrigin), json(VaultErrorCode::InvalidGrant), json(VaultErrorCode::GrantExpired), json(VaultErrorCode::GrantRevoked), json(VaultErrorCode::GrantExhausted), json(VaultErrorCode::SecretFieldForbidden), json(VaultErrorCode::NotFound), json(VaultErrorCode::StorageFailure)]
    });

    // Then: every wire value matches the version-one golden contract.
    assert_eq!(actual, golden);
}
