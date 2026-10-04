//! Todo 3 — fail-closed E2EE for generic sync.
//!
//! These integration tests compile the `maho-core` library in its normal
//! (non-`cfg(test)`) configuration, so they exercise the production receive
//! path (`MahoCore::handle_incoming_websocket_message`) exactly as the shell
//! WebSocket feed would. They prove that a keyless (or otherwise downgraded)
//! active sync session can neither ingest nor act on unauthenticated entity
//! payloads, that malformed encrypted envelopes fail without mutating state,
//! while a legitimately encrypted round trip still works — and that every
//! denial surfaces a typed, matchable `SyncTransportError`, not just a string.
//!
//! Mutation is measured by the sync-layer `entity_versions` store, not by the
//! shortcut label list: default shortcut actions (`navigate_back`, …) are
//! always present in `get_all_shortcuts_json`, so that list cannot distinguish
//! "applied" from "rejected". A recorded `entity_version` is the unambiguous
//! signal that a remote entity was accepted into sync state.

use maho_core::maho_core::MahoCore;
use maho_core::sync_crypto;
use maho_core::sync_manager::SyncTransportError;
use maho_core::sync_models::{SyncEntity, SyncEntityType, SyncMessage, SyncStatus};

fn shortcut_entity(id: &str, version: u64) -> SyncEntity {
    let payload = serde_json::json!({
        "action": id,
        "enabled": true,
        "keyCombo": {"key": "b", "modifiers": ["cmd"]}
    })
    .to_string();
    SyncEntity {
        entity_type: SyncEntityType::Shortcut,
        entity_id: id.to_string(),
        version,
        device_id: 99,
        schema_version: 1,
        modified_at: 1_700_000_000,
        payload_json: payload,
        deleted: false,
        queue_row_id: None,
        fields_hlc_json: None,
        profile_id: None,
    }
}

fn keyless_core() -> MahoCore {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let mut core = MahoCore::new().with_storage(":memory:");
    core.start_sync("wss://localhost:8080", "room-failclosed");
    core.set_sync_status(SyncStatus::Synced);
    core
}

fn encrypted_core() -> (MahoCore, String) {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let mut core = MahoCore::new().with_storage(":memory:");
    let key_info: serde_json::Value =
        serde_json::from_str(&core.generate_sync_key()).expect("key info json");
    let phrase = key_info["recoveryPhrase"]
        .as_str()
        .expect("recovery phrase")
        .to_string();
    core.configure_sync_encryption_for_recovery_phrase("wss://localhost:8080", &phrase)
        .expect("configure encryption");
    core.set_sync_status(SyncStatus::Synced);
    (core, phrase)
}

/// Unambiguous "was this remote entity accepted into sync state?" probe.
/// A rejected inbound payload never reaches `apply_remote_entity`, so no
/// `entity_versions` row is written.
fn entity_applied(core: &MahoCore, id: &str) -> bool {
    core.storage_ref()
        .expect("storage")
        .get_entity_version("shortcut", id)
        .expect("get_entity_version query must succeed")
        .is_some()
}

fn encrypt_for(phrase: &str, inner: &SyncMessage) -> String {
    let data = encrypt_raw(phrase, &serde_json::to_vec(inner).expect("serialize inner"));
    serde_json::to_string(&SyncMessage::Encrypted { data }).expect("serialize envelope")
}

/// Encrypt arbitrary bytes under the room key so tests can forge envelopes
/// whose ciphertext is authentic but whose plaintext is not a `SyncMessage`.
fn encrypt_raw(phrase: &str, plaintext: &[u8]) -> Vec<u8> {
    let seed = sync_crypto::decode_recovery_phrase(phrase).expect("decode phrase");
    let key = sync_crypto::derive_encryption_key(&seed).expect("derive key");
    sync_crypto::encrypt_update(plaintext, &key).expect("encrypt")
}

// --- Fail-closed inbound: keyless active session rejects raw plaintext ---

#[test]
fn keyless_inbound_plaintext_entity_push_is_rejected() {
    let mut core = keyless_core();
    let msg = SyncMessage::EntityPush {
        entity: shortcut_entity("navigate_back", 10),
    };
    let json = serde_json::to_string(&msg).expect("serialize");

    let result = core.handle_incoming_websocket_message(&json);

    assert!(
        result.is_err(),
        "keyless plaintext EntityPush must be rejected, got Ok"
    );
    assert!(
        !entity_applied(&core, "navigate_back"),
        "rejected plaintext entity must NOT be applied to sync state"
    );
    assert!(
        matches!(
            core.sync_last_transport_error(),
            Some(SyncTransportError::PlaintextPayloadRejected { .. })
        ),
        "rejection must record a typed PlaintextPayloadRejected error, got {:?}",
        core.sync_last_transport_error()
    );
}

#[test]
fn keyless_inbound_plaintext_entity_batch_is_rejected() {
    let mut core = keyless_core();
    let msg = SyncMessage::EntityBatch {
        entities: vec![
            shortcut_entity("navigate_forward", 11),
            shortcut_entity("open_settings", 12),
        ],
    };
    let json = serde_json::to_string(&msg).expect("serialize");

    let result = core.handle_incoming_websocket_message(&json);

    assert!(
        result.is_err(),
        "keyless plaintext EntityBatch must be rejected, got Ok"
    );
    assert!(
        !entity_applied(&core, "navigate_forward") && !entity_applied(&core, "open_settings"),
        "rejected plaintext batch must NOT be applied to sync state"
    );
    assert_eq!(
        core.sync_last_transport_error().map(|e| e.code()),
        Some("plaintext_payload_rejected"),
        "batch rejection must record a typed error code"
    );
}

#[test]
fn keyless_inbound_plaintext_send_tab_is_rejected() {
    let mut core = keyless_core();
    let msg = SyncMessage::SendTab {
        url: "https://attacker.example.com".to_string(),
        title: "Injected".to_string(),
        sender_device: "sender-x".to_string(),
        target_device: core.sync_device_id(),
    };
    let json = serde_json::to_string(&msg).expect("serialize");

    let result = core.handle_incoming_websocket_message(&json);

    assert!(
        result.is_err(),
        "keyless plaintext SendTab must be rejected, got Ok"
    );
    assert!(
        core.drain_received_tabs().is_empty(),
        "rejected plaintext SendTab must NOT be recorded"
    );
    assert!(
        matches!(
            core.sync_last_transport_error(),
            Some(SyncTransportError::PlaintextPayloadRejected { .. })
        ),
        "rejection must record a typed PlaintextPayloadRejected error"
    );
}

// A raw plaintext entity payload is rejected even when an E2EE key IS installed:
// legitimate traffic always arrives inside the encrypted envelope, so a
// top-level plaintext payload is a downgrade attempt regardless of key state.
#[test]
fn encrypted_session_still_rejects_toplevel_plaintext_entity() {
    let (mut core, _phrase) = encrypted_core();
    let msg = SyncMessage::EntityPush {
        entity: shortcut_entity("navigate_back", 20),
    };
    let json = serde_json::to_string(&msg).expect("serialize");

    let result = core.handle_incoming_websocket_message(&json);

    assert!(
        result.is_err(),
        "top-level plaintext EntityPush must be rejected even with a key installed"
    );
    assert!(
        !entity_applied(&core, "navigate_back"),
        "top-level plaintext entity must NOT be applied even with a key installed"
    );
    assert!(
        matches!(
            core.sync_last_transport_error(),
            Some(SyncTransportError::PlaintextPayloadRejected { .. })
        ),
        "downgrade attempt must record a typed PlaintextPayloadRejected error"
    );
}

// --- Malformed encrypted envelopes fail closed without mutating state ---

// Ciphertext with a valid nonce/length but a flipped byte: AES-256-GCM
// authentication must fail, the entity must not be applied, and the failure
// must surface as a typed decrypt error.
#[test]
fn tampered_ciphertext_envelope_is_rejected_without_state_mutation() {
    let (mut core, phrase) = encrypted_core();
    let inner = SyncMessage::EntityPush {
        entity: shortcut_entity("navigate_back", 55),
    };
    let mut data = encrypt_raw(
        &phrase,
        &serde_json::to_vec(&inner).expect("serialize inner"),
    );
    let last = data.len() - 1;
    data[last] ^= 0xFF; // corrupt the authentication tag
    let json = serde_json::to_string(&SyncMessage::Encrypted { data }).expect("serialize envelope");

    let result = core.handle_incoming_websocket_message(&json);

    assert!(
        result.is_err(),
        "tampered ciphertext must be rejected (AEAD authentication failure)"
    );
    assert!(
        !entity_applied(&core, "navigate_back"),
        "tampered payload must NOT be applied to sync state"
    );
    assert!(
        matches!(
            core.sync_last_transport_error(),
            Some(SyncTransportError::Decrypt { .. })
        ),
        "tampered ciphertext must record a typed Decrypt error, got {:?}",
        core.sync_last_transport_error()
    );
}

// A structurally valid, authentically encrypted envelope whose plaintext is
// NOT a `SyncMessage`: decryption succeeds, but inner parsing must fail closed
// with a distinct typed error and no state mutation.
#[test]
fn authenticated_envelope_with_invalid_inner_json_is_rejected() {
    let (mut core, phrase) = encrypted_core();
    // Authentic ciphertext, but the plaintext is a JSON object that does not
    // match the tagged `SyncMessage` schema.
    let data = encrypt_raw(&phrase, br#"{"type":"totally-bogus","x":1}"#);
    let json = serde_json::to_string(&SyncMessage::Encrypted { data }).expect("serialize envelope");

    let result = core.handle_incoming_websocket_message(&json);

    assert!(
        result.is_err(),
        "valid-AEAD envelope with invalid inner JSON must be rejected"
    );
    assert!(
        core.drain_received_tabs().is_empty(),
        "no tab may be recorded from an unparseable inner message"
    );
    assert_eq!(
        core.sync_last_transport_error().map(|e| e.code()),
        Some("malformed_envelope_contents"),
        "authenticated-but-unparseable inner message must record a distinct typed code, got {:?}",
        core.sync_last_transport_error()
    );
}

// --- Encrypted round trips still work (characterization / no regression) ---

#[test]
fn encrypted_inbound_entity_push_is_applied() {
    let (mut core, phrase) = encrypted_core();
    let inner = SyncMessage::EntityPush {
        entity: shortcut_entity("navigate_back", 30),
    };
    let json = encrypt_for(&phrase, &inner);

    core.handle_incoming_websocket_message(&json)
        .expect("encrypted EntityPush must be accepted");

    assert!(
        entity_applied(&core, "navigate_back"),
        "legitimately decrypted entity must be applied to sync state"
    );
}

#[test]
fn encrypted_inbound_send_tab_is_recorded() {
    let (mut core, phrase) = encrypted_core();
    let inner = SyncMessage::SendTab {
        url: "https://trusted.example.com".to_string(),
        title: "Trusted".to_string(),
        sender_device: "peer-1".to_string(),
        target_device: core.sync_device_id(),
    };
    let json = encrypt_for(&phrase, &inner);

    core.handle_incoming_websocket_message(&json)
        .expect("encrypted SendTab must be accepted");

    let tabs = core.drain_received_tabs();
    assert_eq!(
        tabs.len(),
        1,
        "legitimately decrypted SendTab must be recorded"
    );
    assert_eq!(tabs[0].url, "https://trusted.example.com");
}

// --- Startup path proof ---

// A freshly started keyless session (start_sync installs no key) must not be a
// bypass: the very first inbound plaintext entity is rejected and dropped.
#[test]
fn startup_keyless_session_cannot_ingest_plaintext_entity() {
    let mut core = keyless_core();
    assert!(
        core.get_sync_state().kind != SyncStatus::Error,
        "precondition: fresh keyless session is not already errored"
    );

    let msg = SyncMessage::EntityPush {
        entity: shortcut_entity("navigate_back", 40),
    };
    let json = serde_json::to_string(&msg).expect("serialize");
    let result = core.handle_incoming_websocket_message(&json);

    assert!(result.is_err(), "startup keyless ingest must fail closed");
    assert!(
        !entity_applied(&core, "navigate_back"),
        "startup keyless plaintext entity must NOT be applied to sync state"
    );
}

// Data-surface probe for the manual-QA artifact: prints outgoing/sent/pending
// counts and inbound results for keyless vs encrypted. Run with `-- --nocapture`.
#[test]
fn probe_keyless_vs_encrypted_surface() {
    fn pending_count(core: &MahoCore) -> usize {
        core.storage_ref()
            .unwrap()
            .load_pending_sync_entities()
            .unwrap()
            .len()
    }

    let mut keyless = keyless_core();
    keyless.sync_push_entity_persisted(shortcut_entity("probe_out_keyless", 70));
    let keyless_out = keyless.drain_sync_outgoing();
    let keyless_pending = pending_count(&keyless);
    println!(
        "PROBE keyless_outgoing: messages={} pending_after={} typed_err={:?} (expect messages=0, pending>=1)",
        keyless_out.len(),
        keyless_pending,
        keyless.sync_last_transport_error().map(|e| e.code())
    );

    let mut keyless_in = keyless_core();
    let inbound = SyncMessage::EntityPush {
        entity: shortcut_entity("probe_in_keyless", 77),
    };
    let inbound_json = serde_json::to_string(&inbound).unwrap();
    let keyless_err = keyless_in.handle_incoming_websocket_message(&inbound_json);
    println!(
        "PROBE keyless_inbound_plaintext: result={:?} applied={} typed_err={:?}",
        keyless_err,
        entity_applied(&keyless_in, "probe_in_keyless"),
        keyless_in.sync_last_transport_error().map(|e| e.code())
    );

    let (mut enc, _phrase) = encrypted_core();
    enc.sync_push_entity_persisted(shortcut_entity("probe_out_enc", 80));
    let enc_out = enc.drain_sync_outgoing();
    let enc_encrypted = !enc_out.is_empty()
        && enc_out
            .iter()
            .all(|m| matches!(m, SyncMessage::Encrypted { .. }));
    let enc_pending = pending_count(&enc);
    println!(
        "PROBE encrypted_outgoing: messages={} all_encrypted={} pending_after_sent={} (expect messages>=1, all_encrypted=true, pending=0)",
        enc_out.len(),
        enc_encrypted,
        enc_pending
    );

    let (mut enc_in, phrase2) = encrypted_core();
    let enc_inner = SyncMessage::EntityPush {
        entity: shortcut_entity("probe_in_enc", 88),
    };
    let enc_json = encrypt_for(&phrase2, &enc_inner);
    let enc_res = enc_in.handle_incoming_websocket_message(&enc_json);
    println!(
        "PROBE encrypted_inbound: result={:?} applied={}",
        enc_res,
        entity_applied(&enc_in, "probe_in_enc")
    );

    assert!(keyless_out.is_empty(), "keyless outgoing must be empty");
    assert!(keyless_pending >= 1, "keyless push must remain pending");
    assert!(
        keyless_err.is_err(),
        "keyless inbound plaintext must be rejected"
    );
    assert!(enc_encrypted, "encrypted outgoing present + all encrypted");
    assert!(enc_res.is_ok(), "encrypted inbound must apply");
}

#[test]
fn keyless_inbound_plaintext_vault_item_sync_is_rejected() {
    let mut core = keyless_core();
    let vault_entity = SyncEntity {
        entity_type: SyncEntityType::VaultItem,
        entity_id: "vitem_123".to_string(),
        version: 1,
        device_id: 42,
        schema_version: 1,
        modified_at: 1_700_000_000,
        payload_json: r#"{"id":"vitem_123","schemaVersion":1,"revision":1,"provider":"maho_native","itemKind":"login","title":"Synced Credential","origins":["https://example.com"],"usernameHint":"alice","createdAt":"2024-01-01T00:00:00Z","updatedAt":"2024-01-01T00:00:00Z","lastUsedAt":null,"totp":null,"passkey":null}"#.to_string(),
        deleted: false,
        queue_row_id: None,
        fields_hlc_json: None,
        profile_id: None,
    };
    let msg = SyncMessage::EntityPush {
        entity: vault_entity,
    };
    let json = serde_json::to_string(&msg).expect("serialize");

    let result = core.handle_incoming_websocket_message(&json);

    assert!(
        result.is_err(),
        "keyless plaintext VaultItem sync must be rejected fail-closed"
    );
    assert!(
        matches!(
            core.sync_last_transport_error(),
            Some(SyncTransportError::PlaintextPayloadRejected { .. })
        ),
        "rejection must record PlaintextPayloadRejected error"
    );
}
