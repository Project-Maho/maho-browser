use chrono::{TimeZone, Utc};
use maho_types::passwords::{get_provider_registry, PasswordProviderKind};
use maho_types::vault::*;
use serde_json::json;
use uuid::Uuid;

fn uuid(value: &str) -> Uuid {
    Uuid::parse_str(value).unwrap()
}

fn timestamp(minute: u32) -> chrono::DateTime<Utc> {
    Utc.with_ymd_and_hms(2026, 7, 22, 0, minute, 0)
        .single()
        .unwrap()
}

#[test]
fn vault_representative_public_surface_is_secret_free() {
    // Given: representative status, item, model grant, audit, and provider DTOs.
    let item_id = VaultItemId::from_uuid(uuid("018f47a6-7c2a-7d6e-9a1b-1d2e3f4a5b6c"));
    let alias = CredentialItemAlias::from_uuid(uuid("018f47a6-7c2a-7d6e-9a1b-1d2e3f4a5b6d"));
    let grant_handle =
        CredentialGrantHandle::from_uuid(uuid("018f47a6-7c2a-7d6e-9a1b-1d2e3f4a5b6e"));
    let audit_id = VaultAuditRecordId::from_uuid(uuid("018f47a6-7c2a-7d6e-9a1b-1d2e3f4a5b6f"));
    let origin = CredentialOrigin::try_from("https://example.com").unwrap();
    let surface = json!({
        "status": VaultStatus {
            schema_version: VaultSchemaVersion::V1,
            lock_state: VaultLockState::Unlocked,
            selected_provider: PasswordProviderKind::MahoNative,
            effective_provider: PasswordProviderKind::MahoNative,
            item_count: 1,
            agent_policy_default: VaultAgentPolicy::Deny,
            auto_lock_minutes: 15,
            failed_unlock_count: 0,
            retry_at: None,
            account_escrowed: false,
        },
        "item": VaultItemPublicDto {
            favorite: false,
            trashed_at: None,
            has_notes: false,
            schema_version: VaultSchemaVersion::V1,
            id: item_id,
            revision: VaultRevision::new(4),
            provider: PasswordProviderKind::MahoNative,
            item_kind: VaultItemKind::Login,
            title: "Example".to_string(),
            origins: vec![origin.clone()],
            username_hint: "u***@example.com".to_string(),
            created_at: timestamp(0),
            updated_at: timestamp(1),
            last_used_at: Some(timestamp(2)),
            totp: None,
            passkey: None,
        },
        "grant": CredentialGrantDto {
            schema_version: VaultSchemaVersion::V1,
            handle: grant_handle,
            item_alias: alias,
            allowed_fields: vec![CredentialField::Username],
            expires_at: timestamp(5),
            remaining_uses: 1,
            state: CredentialGrantState::Active,
        },
        "audit": VaultAuditPublicDto {
            schema_version: VaultSchemaVersion::V1,
            id: audit_id,
            timestamp: timestamp(2),
            task_id: None,
            origin: Some(origin),
            item_alias: Some(alias),
            operation: VaultAuditOperation::Fill,
            policy: Some(VaultAgentPolicy::AskEveryUse),
            decision: VaultAuditDecision::Allowed,
            reason: None,
            device_name: "Test Device".to_string(),
        },
        "provider": VaultProviderStatus {
            schema_version: VaultSchemaVersion::V1,
            selected_provider: PasswordProviderKind::MahoNative,
            effective_provider: PasswordProviderKind::MahoNative,
            providers: get_provider_registry(),
        }
    });

    // When: the model/UI-safe surface is serialized exactly as consumers receive it.
    let serialized = serde_json::to_string_pretty(&surface).unwrap();
    println!("{serialized}");

    // Then: no secret-bearing keys, ciphertext bytes, sentinel, or stable grant item ID leak.
    assert!(!serialized.contains("S3NTINEL-maho-vault-9F4C"));
    assert!(!serialized.contains("\"password\":"));
    assert!(!serialized.contains("\"secret\":"));
    assert!(!serialized.contains("\"ciphertext\":"));
    assert!(!serialized.contains("\"nonce\":"));
    assert!(!serialized.contains("\"tag\":"));
    assert!(surface["grant"].get("itemId").is_none());
}

#[test]
fn vault_backend_operation_surface_is_secret_free() {
    // Given: representative future backend batch-read and range-tombstone contracts.
    let first_id = VaultItemId::from_uuid(uuid("018f47a6-7c2a-7d6e-9a1b-1d2e3f4a5b6c"));
    let second_id = VaultItemId::from_uuid(uuid("018f47a6-7c2a-7d6e-9a1b-1d2e3f4a5b6d"));
    let item = VaultItemPublicDto {
        favorite: false,
        trashed_at: None,
        has_notes: false,
        schema_version: VaultSchemaVersion::V1,
        id: first_id,
        revision: VaultRevision::new(4),
        provider: PasswordProviderKind::MahoNative,
        item_kind: VaultItemKind::Login,
        title: "Example".to_string(),
        origins: vec![CredentialOrigin::try_from("https://example.com").unwrap()],
        username_hint: "u***@example.com".to_string(),
        created_at: timestamp(0),
        updated_at: timestamp(1),
        last_used_at: None,
        totp: None,
        passkey: None,
    };
    let surface = json!({
        "request": VaultBatchReadRequest {
            schema_version: VaultSchemaVersion::V1,
            item_ids: vec![first_id, second_id],
        },
        "result": VaultBatchReadResult {
            schema_version: VaultSchemaVersion::V1,
            items: vec![item],
            missing_ids: vec![second_id],
        },
        "range": VaultItemCreatedRange {
            schema_version: VaultSchemaVersion::V1,
            from_inclusive: None,
            until_exclusive: Some(timestamp(10)),
        },
        "deleteResult": VaultRangeDeleteResult {
            schema_version: VaultSchemaVersion::V1,
            tombstoned_ids: vec![first_id, second_id],
            count: 2,
        }
    });

    // When: the backend surface is serialized exactly as future consumers receive it.
    let serialized = serde_json::to_string_pretty(&surface)
        .unwrap()
        .to_lowercase();

    // Then: no secret-bearing field name or representative secret marker is present.
    for forbidden in [
        "password",
        "seed",
        "ciphertext",
        "nonce",
        "tag",
        "rawkey",
        "raw_key",
        "secret",
    ] {
        assert!(
            !serialized.contains(forbidden),
            "forbidden secret-bearing field {forbidden:?} leaked: {serialized}"
        );
    }
    assert!(surface["result"]["items"][0]
        .get("encryptedPayload")
        .is_none());
    assert!(surface["result"].get("secretResult").is_none());
}
