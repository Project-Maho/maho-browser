use std::str::FromStr;

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
fn vault_stable_typed_ids_round_trip_without_losing_uuid_identity() {
    // Given: stable UUID values for Vault and agent-facing identifiers.
    let uuid = Uuid::parse_str("018f47a6-7c2a-7d6e-9a1b-1d2e3f4a5b6c").unwrap();
    let item_id = VaultItemId::from_uuid(uuid);
    let alias = CredentialItemAlias::from_uuid(uuid);

    // When: each identifier crosses the JSON boundary and returns.
    let item_round_trip: VaultItemId =
        serde_json::from_str(&serde_json::to_string(&item_id).unwrap()).unwrap();
    let alias_round_trip: CredentialItemAlias =
        serde_json::from_str(&serde_json::to_string(&alias).unwrap()).unwrap();

    // Then: the semantic types stay distinct while preserving the UUID value.
    assert_eq!(item_round_trip, item_id);
    assert_eq!(alias_round_trip, alias);
    assert_eq!(item_id.as_uuid(), &uuid);
    assert_eq!(VaultItemId::from_str(&uuid.to_string()).unwrap(), item_id);
}

#[test]
fn vault_unsupported_schema_version_is_rejected_inbound_and_outbound() {
    // Given: a version newer than this contract understands.
    let unsupported = 99;

    // When: the schema version is parsed directly or through a DTO.
    let direct = VaultSchemaVersion::try_from(unsupported);
    let dto = serde_json::from_value::<VaultItemSearchRequest>(json!({
        "schemaVersion": unsupported,
        "origin": "https://example.com",
        "provider": null,
        "kinds": ["login"]
    }));

    // Then: both boundaries fail closed and identify the unsupported version.
    assert_eq!(
        direct,
        Err(VaultContractError::UnsupportedSchemaVersion { found: unsupported })
    );
    assert!(dto
        .unwrap_err()
        .to_string()
        .contains("unsupported Vault schema version 99"));
    assert!(serde_json::to_value(VaultSchemaVersion::Unsupported(unsupported)).is_err());
}

#[test]
fn vault_public_update_and_agent_inputs_reject_secret_bearing_fields() {
    // Given: otherwise valid DTO inputs carrying forbidden plaintext field names.
    let public = json!({
        "schemaVersion": 1,
        "id": "018f47a6-7c2a-7d6e-9a1b-1d2e3f4a5b6c",
        "revision": 3,
        "provider": "maho_native",
        "itemKind": "login",
        "title": "Example",
        "origins": ["https://example.com"],
        "usernameHint": "u***@example.com",
        "createdAt": "2026-07-22T00:00:00Z",
        "updatedAt": "2026-07-22T00:01:00Z",
        "lastUsedAt": null,
        "totp": null,
        "passkey": null,
        "password": "S3NTINEL-maho-vault-9F4C"
    });
    let update = json!({
        "schemaVersion": 1,
        "id": "018f47a6-7c2a-7d6e-9a1b-1d2e3f4a5b6c",
        "expectedRevision": 3,
        "publicMetadata": {
            "title": "Example",
            "origins": ["https://example.com"],
            "usernameHint": "u***@example.com",
            "itemKind": "login",
            "totp": null,
            "passkey": null
        },
        "encryptedPayload": {
            "schemaVersion": 1,
            "algorithm": "aes_256_gcm",
            "purpose": "vault_item",
            "keyVersion": 2,
            "nonce": [1, 2],
            "ciphertext": [3, 4],
            "tag": [5, 6]
        },
        "secret": "S3NTINEL-maho-vault-9F4C"
    });
    let agent = json!({
        "schemaVersion": 1,
        "alias": "018f47a6-7c2a-7d6e-9a1b-1d2e3f4a5b6d",
        "itemKind": "login",
        "displayLabel": "Example",
        "usernameHint": "u***@example.com",
        "availableFields": ["username", "password"],
        "secret": "S3NTINEL-maho-vault-9F4C"
    });

    // When: each untrusted JSON object is parsed at its public boundary.
    let public_error = serde_json::from_value::<VaultItemPublicDto>(public).unwrap_err();
    let update_error = serde_json::from_value::<VaultItemUpdateRequest>(update).unwrap_err();
    let agent_error = serde_json::from_value::<AgentVaultItemDto>(agent).unwrap_err();

    // Then: plaintext password and secret fields are rejected rather than ignored.
    assert!(public_error
        .to_string()
        .contains("unknown field `password`"));
    assert!(update_error.to_string().contains("unknown field `secret`"));
    assert!(agent_error.to_string().contains("unknown field `secret`"));
}

#[test]
fn vault_encrypted_and_legacy_secret_debug_output_is_redacted() {
    // Given: sentinel bytes in encrypted metadata and the isolated legacy password DTO.
    let envelope = VaultCiphertextEnvelope {
        schema_version: VaultSchemaVersion::V1,
        algorithm: VaultCipherAlgorithm::Aes256Gcm,
        purpose: VaultCiphertextPurpose::VaultItem,
        key_version: VaultKeyVersion::new(2),
        nonce: b"nonce-S3NTINEL".to_vec(),
        ciphertext: b"ciphertext-S3NTINEL".to_vec(),
        tag: b"tag-S3NTINEL".to_vec(),
    };
    #[allow(deprecated)]
    let legacy = maho_types::passwords::SavedPassword {
        id: "legacy".to_string(),
        domain: "example.com".to_string(),
        username: "user".to_string(),
        created_at: "2026-07-22T00:00:00Z".to_string(),
        last_used: None,
        password: Some("S3NTINEL-maho-vault-9F4C".to_string()),
    };

    // When: diagnostic formatting is requested.
    let envelope_debug = format!("{envelope:?}");
    let legacy_debug = format!("{legacy:?}");

    // Then: secret-bearing bytes and plaintext never appear in Debug output.
    assert!(envelope_debug.contains("[REDACTED]"));
    assert!(legacy_debug.contains("[REDACTED]"));
    assert!(!envelope_debug.contains("S3NTINEL"));
    assert!(!legacy_debug.contains("S3NTINEL"));
}

#[test]
fn vault_ciphertext_envelope_rejects_oversized_vectors_at_deserialization() {
    // Given: otherwise valid wire envelopes with each vector beyond its contract upper bound.
    let baseline = serde_json::json!({
        "schemaVersion": 1,
        "algorithm": "aes_256_gcm",
        "purpose": "vault_item",
        "keyVersion": 1,
        "nonce": [0],
        "ciphertext": [0],
        "tag": [0]
    });

    // When: each attacker-controlled vector crosses the serde boundary.
    assert!(serde_json::from_value::<VaultCiphertextEnvelope>(baseline.clone()).is_ok());
    let mut nonce = baseline.clone();
    nonce["nonce"] = serde_json::json!(vec![0; MAX_VAULT_ENVELOPE_NONCE_LEN + 1]);
    let mut ciphertext = baseline.clone();
    ciphertext["ciphertext"] = serde_json::json!(vec![0; MAX_VAULT_ENVELOPE_CIPHERTEXT_LEN + 1]);
    let mut tag = baseline;
    tag["tag"] = serde_json::json!(vec![0; MAX_VAULT_ENVELOPE_TAG_LEN + 1]);

    // Then: allocation-bounded contract parsing rejects every oversized vector.
    assert!(serde_json::from_value::<VaultCiphertextEnvelope>(nonce).is_err());
    assert!(serde_json::from_value::<VaultCiphertextEnvelope>(ciphertext).is_err());
    assert!(serde_json::from_value::<VaultCiphertextEnvelope>(tag).is_err());
}

#[test]
fn vault_credential_origins_reject_non_origin_urls() {
    // Given: a trusted origin and malformed URL-like values.
    let valid = "https://example.com:8443";

    // When: values are parsed into the origin type.
    let parsed = CredentialOrigin::from_str(valid).unwrap();

    // Then: only scheme-and-authority origins are accepted.
    assert_eq!(parsed.as_ref(), valid);
    assert!(CredentialOrigin::from_str("example.com").is_err());
    assert!(CredentialOrigin::from_str("https://example.com/path").is_err());
    assert!(CredentialOrigin::from_str("https://example.com?query=1").is_err());
}

#[test]
fn vault_provider_status_reuses_the_existing_provider_registry() {
    // Given: the canonical shared password-provider registry.
    let registry = get_provider_registry();
    let status = VaultProviderStatus {
        schema_version: VaultSchemaVersion::V1,
        selected_provider: PasswordProviderKind::OnePassword,
        effective_provider: PasswordProviderKind::MahoNative,
        providers: registry,
    };

    // When: Vault provider status is serialized.
    let serialized = serde_json::to_value(status).unwrap();

    // Then: it preserves the canonical three provider IDs without a duplicate registry.
    assert_eq!(serialized["selectedProvider"], "onepassword");
    assert_eq!(serialized["effectiveProvider"], "maho_native");
    assert_eq!(
        serialized["providers"]
            .as_array()
            .unwrap()
            .iter()
            .map(|provider| provider["providerId"].as_str().unwrap())
            .collect::<Vec<_>>(),
        vec!["maho_native", "bitwarden", "onepassword"]
    );
}

#[test]
fn vault_backend_operation_contracts_serialize_with_stable_ordered_shapes() {
    // Given: ordered item IDs, public item metadata, and a bounded creation range.
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
    let batch_request = VaultBatchReadRequest {
        schema_version: VaultSchemaVersion::V1,
        item_ids: vec![first_id, second_id],
    };
    let batch_result = VaultBatchReadResult {
        schema_version: VaultSchemaVersion::V1,
        items: vec![item],
        missing_ids: vec![second_id],
    };
    let range = VaultItemCreatedRange {
        schema_version: VaultSchemaVersion::V1,
        from_inclusive: None,
        until_exclusive: Some(timestamp(2)),
    };
    let delete_result = VaultRangeDeleteResult {
        schema_version: VaultSchemaVersion::V1,
        tombstoned_ids: vec![first_id, second_id],
        count: 2,
    };

    // When: each backend contract crosses the JSON boundary and returns.
    let request_json = serde_json::to_value(&batch_request).unwrap();
    let result_json = serde_json::to_value(&batch_result).unwrap();
    let range_json = serde_json::to_value(&range).unwrap();
    let delete_json = serde_json::to_value(&delete_result).unwrap();

    // Then: wire names, order, and values remain stable and typed.
    assert_eq!(request_json["schemaVersion"], 1);
    assert_eq!(request_json["itemIds"][0], first_id.to_string());
    assert_eq!(request_json["itemIds"][1], second_id.to_string());
    assert_eq!(result_json["items"][0]["id"], first_id.to_string());
    assert_eq!(result_json["missingIds"][0], second_id.to_string());
    assert_eq!(range_json["fromInclusive"], serde_json::Value::Null);
    assert_eq!(range_json["untilExclusive"], "2026-07-22T00:02:00Z");
    assert_eq!(delete_json["tombstonedIds"], json!([first_id, second_id]));
    assert_eq!(delete_json["count"], 2);
}

#[test]
fn vault_backend_operation_contracts_reject_unsupported_schema_versions() {
    // Given: a representative operation object with a schema version newer than V1.
    let unsupported = json!({
        "schemaVersion": 99,
        "itemIds": ["018f47a6-7c2a-7d6e-9a1b-1d2e3f4a5b6c"]
    });

    // When: the request crosses the serde boundary.
    let parsed = serde_json::from_value::<VaultBatchReadRequest>(unsupported);

    // Then: the operation fails closed with the typed schema error.
    assert!(parsed
        .unwrap_err()
        .to_string()
        .contains("unsupported Vault schema version 99"));

    let secret_result =
        json!({"schemaVersion": 1, "items": [], "missingIds": [], "secretResult": "x"});
    assert!(
        serde_json::from_value::<VaultBatchReadResult>(secret_result)
            .unwrap_err()
            .to_string()
            .contains("unknown field `secretResult`")
    );
}

#[test]
fn vault_public_metadata_decodes_legacy_payload_with_absent_fields() {
    // Given: legacy serialized public metadata without favorite, trashedAt, or hasNotes.
    let legacy = json!({
        "title": "Legacy Item",
        "origins": ["https://example.com"],
        "usernameHint": "u***@example.com",
        "itemKind": "login",
        "totp": null,
        "passkey": null
    });

    // When: decoded into VaultItemPublicMetadata.
    let decoded: VaultItemPublicMetadata = serde_json::from_value(legacy).unwrap();

    // Then: newly added fields take their default values while existing fields deserialize correctly.
    assert!(!decoded.favorite);
    assert_eq!(decoded.trashed_at, None);
    assert!(!decoded.has_notes);
    assert_eq!(decoded.title, "Legacy Item");
    assert_eq!(decoded.username_hint, "u***@example.com");
    assert_eq!(decoded.item_kind, VaultItemKind::Login);
    assert!(decoded.totp.is_none());
    assert!(decoded.passkey.is_none());
}
