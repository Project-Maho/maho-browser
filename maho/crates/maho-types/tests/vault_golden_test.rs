use chrono::{TimeZone, Utc};
use maho_types::identifiers::{ProfileId, TabId};
use maho_types::passwords::{get_provider_registry, PasswordProviderKind};
use maho_types::vault::*;
use serde::Serialize;
use serde_json::{json, Value};
use uuid::Uuid;

fn json<T: Serialize>(value: T) -> Value {
    serde_json::to_value(value).unwrap()
}

fn uuid(value: &str) -> Uuid {
    Uuid::parse_str(value).unwrap()
}

fn timestamp(minute: u32) -> chrono::DateTime<Utc> {
    Utc.with_ymd_and_hms(2026, 7, 22, 0, minute, 0)
        .single()
        .unwrap()
}

#[test]
fn every_vault_dto_has_a_stable_golden_wire_shape() {
    // Given: representative values for every public Vault DTO.
    let item_id = VaultItemId::from_uuid(uuid("018f47a6-7c2a-7d6e-9a1b-1d2e3f4a5b6c"));
    let alias = CredentialItemAlias::from_uuid(uuid("018f47a6-7c2a-7d6e-9a1b-1d2e3f4a5b6d"));
    let handle = CredentialGrantHandle::from_uuid(uuid("018f47a6-7c2a-7d6e-9a1b-1d2e3f4a5b6e"));
    let session_id = VaultSessionId::from_uuid(uuid("018f47a6-7c2a-7d6e-9a1b-1d2e3f4a5b6f"));
    let task_id = VaultTaskId::from_uuid(uuid("018f47a6-7c2a-7d6e-9a1b-1d2e3f4a5b70"));
    let workspace_id = VaultWorkspaceId::from_uuid(uuid("018f47a6-7c2a-7d6e-9a1b-1d2e3f4a5b71"));
    let audit_id = VaultAuditRecordId::from_uuid(uuid("018f47a6-7c2a-7d6e-9a1b-1d2e3f4a5b72"));
    let origin = CredentialOrigin::try_from("https://example.com").unwrap();
    let frame_origin = CredentialOrigin::try_from("https://login.example.com").unwrap();
    let totp = TotpMetadata {
        issuer: "Example".to_string(),
        account_label: "u***@example.com".to_string(),
        algorithm: TotpAlgorithm::Sha256,
        digits: TotpDigits::Six,
        period_seconds: TotpPeriodSeconds::new(30),
        created_at: timestamp(0),
        last_used_at: None,
    };
    let passkey = PasskeyMetadata {
        rp_id: PasskeyRpId::try_from("example.com").unwrap(),
        user_name_hint: "u***@example.com".to_string(),
        display_name: "Example passkey".to_string(),
        credential_id: vec![12, 13],
        transports: vec![PasskeyTransport::Internal, PasskeyTransport::Hybrid],
        discoverable: true,
        backup_eligible: true,
        backup_state: false,
        user_verification: PasskeyUserVerification::Required,
        created_at: timestamp(0),
        last_used_at: None,
    };
    let envelope = VaultCiphertextEnvelope {
        schema_version: VaultSchemaVersion::V1,
        algorithm: VaultCipherAlgorithm::Aes256Gcm,
        purpose: VaultCiphertextPurpose::VaultItem,
        key_version: VaultKeyVersion::new(2),
        nonce: vec![1, 2],
        ciphertext: vec![3, 4],
        tag: vec![5, 6],
    };
    let metadata = VaultItemPublicMetadata {
        favorite: false,
        trashed_at: None,
        has_notes: false,
        title: "Example".to_string(),
        origins: vec![origin.clone()],
        username_hint: "u***@example.com".to_string(),
        item_kind: VaultItemKind::Login,
        totp: Some(totp.clone()),
        passkey: Some(passkey.clone()),
    };

    // When: each DTO is serialized into its externally visible JSON shape.
    let actual = json!({
        "ciphertextEnvelope": json(envelope.clone()),
        "encryptedItem": json(EncryptedVaultItemDto {
            schema_version: VaultSchemaVersion::V1,
            id: item_id,
            revision: VaultRevision::new(3),
            provider: PasswordProviderKind::MahoNative,
            item_kind: VaultItemKind::Login,
            envelope: envelope.clone(),
            created_at: timestamp(0),
            updated_at: timestamp(1),
            deleted_at: None,
        }),
        "publicMetadata": json(metadata.clone()),
        "publicItem": json(VaultItemPublicDto {
            favorite: false,
            trashed_at: None,
            has_notes: false,
            schema_version: VaultSchemaVersion::V1,
            id: item_id,
            revision: VaultRevision::new(3),
            provider: PasswordProviderKind::MahoNative,
            item_kind: VaultItemKind::Login,
            title: "Example".to_string(),
            origins: vec![origin.clone()],
            username_hint: "u***@example.com".to_string(),
            created_at: timestamp(0),
            updated_at: timestamp(1),
            last_used_at: None,
            totp: Some(totp.clone()),
            passkey: Some(passkey.clone()),
        }),
        "listRequest": json(VaultItemListRequest {
            trash: Default::default(),
            favorites_only: false,
            schema_version: VaultSchemaVersion::V1,
            provider: Some(PasswordProviderKind::MahoNative),
            kinds: vec![VaultItemKind::Login, VaultItemKind::Passkey],
            cursor: None,
            limit: 50,
        }),
        "searchRequest": json(VaultItemSearchRequest {
            schema_version: VaultSchemaVersion::V1,
            origin: origin.clone(),
            provider: None,
            kinds: vec![VaultItemKind::Login],
        }),
        "updateRequest": json(VaultItemUpdateRequest {
            schema_version: VaultSchemaVersion::V1,
            id: item_id,
            expected_revision: VaultRevision::new(3),
            public_metadata: metadata,
            encrypted_payload: envelope,
        }),
        "agentItem": json(AgentVaultItemDto {
            schema_version: VaultSchemaVersion::V1,
            alias,
            item_kind: VaultItemKind::Login,
            display_label: "Example".to_string(),
            username_hint: "u***@example.com".to_string(),
            available_fields: vec![CredentialField::Username, CredentialField::Password],
        }),
        "status": json(VaultStatus {
            schema_version: VaultSchemaVersion::V1,
            lock_state: VaultLockState::Locked,
            selected_provider: PasswordProviderKind::OnePassword,
            effective_provider: PasswordProviderKind::MahoNative,
            item_count: 2,
            agent_policy_default: VaultAgentPolicy::Deny,
            auto_lock_minutes: 15,
            failed_unlock_count: 1,
            retry_at: None,
            account_escrowed: false,
        }),
        "policy": json(VaultPolicy {
            schema_version: VaultSchemaVersion::V1,
            policy: VaultAgentPolicy::AskEveryUse,
            item_id: Some(item_id),
            origin: Some(origin.clone()),
            expires_at: Some(timestamp(5)),
        }),
        "grant": json(CredentialCapabilityGrant {
            schema_version: VaultSchemaVersion::V1,
            handle,
            session_id,
            task_id,
            profile_id: ProfileId::new("profile-1"),
            workspace_id,
            tab_id: TabId::new("tab-1"),
            tab_generation: TabGeneration::new(7),
            top_origin: origin.clone(),
            frame_origin: frame_origin.clone(),
            item_id,
            item_alias: alias,
            allowed_fields: vec![CredentialField::Username, CredentialField::Password],
            policy: VaultAgentPolicy::AllowForTask,
            issued_at: timestamp(0),
            expires_at: timestamp(5),
            max_uses: 2,
            used_count: 1,
            state: CredentialGrantState::Active,
            revoked_at: None,
            revocation_reason: None,
        }),
        "agentGrant": json(CredentialGrantDto {
            schema_version: VaultSchemaVersion::V1,
            handle,
            item_alias: alias,
            allowed_fields: vec![CredentialField::Username, CredentialField::Password],
            expires_at: timestamp(5),
            remaining_uses: 1,
            state: CredentialGrantState::Active,
        }),
        "auditRecord": json(VaultAuditRecord {
            schema_version: VaultSchemaVersion::V1,
            id: audit_id,
            timestamp: timestamp(2),
            session_id: Some(session_id),
            task_id: Some(task_id),
            profile_id: ProfileId::new("profile-1"),
            workspace_id,
            top_origin: Some(origin.clone()),
            frame_origin: Some(frame_origin),
            item_id: Some(item_id),
            item_alias: Some(alias),
            operation: VaultAuditOperation::Fill,
            policy: Some(VaultAgentPolicy::AllowForTask),
            decision: VaultAuditDecision::Allowed,
            reason: None,
            device_name: "Test Device".to_string(),
        }),
        "auditPublic": json(VaultAuditPublicDto {
            schema_version: VaultSchemaVersion::V1,
            id: audit_id,
            timestamp: timestamp(2),
            task_id: Some(task_id),
            origin: Some(origin),
            item_alias: Some(alias),
            operation: VaultAuditOperation::Fill,
            policy: Some(VaultAgentPolicy::AllowForTask),
            decision: VaultAuditDecision::Allowed,
            reason: None,
            device_name: "Test Device".to_string(),
        }),
        "totp": json(totp),
        "passkey": json(passkey),
        "providerStatus": json(VaultProviderStatus {
            schema_version: VaultSchemaVersion::V1,
            selected_provider: PasswordProviderKind::OnePassword,
            effective_provider: PasswordProviderKind::MahoNative,
            providers: get_provider_registry(),
        }),
        "error": json(VaultErrorDto {
            schema_version: VaultSchemaVersion::V1,
            code: VaultErrorCode::RateLimited,
            message: "Unlock temporarily rate limited".to_string(),
            retry_at: Some(timestamp(5)),
        })
    });

    // Then: every DTO matches the version-one golden JSON contract.
    let expected: Value =
        serde_json::from_str(include_str!("fixtures/vault_contract_v1.json")).unwrap();
    assert_eq!(actual, expected);
}
