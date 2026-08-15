use maho_core::maho_core::MahoCore;
use maho_core::sync_models::{
    RelayAckV2, SyncEnvelopeV2, SyncProtocolError, SyncStateResponse, SyncStatus,
    SYNC_PROTOCOL_VERSION,
};

const SHARED_V2_FIXTURE: &str = include_str!("../../../tests/fixtures/sync-v2/protocol.json");

#[test]
fn shared_v2_fixture_has_the_cross_platform_wire_contract() {
    let fixture: serde_json::Value =
        serde_json::from_str(SHARED_V2_FIXTURE).expect("parse shared V2 fixture");

    assert_eq!(fixture["protocol_version"], SYNC_PROTOCOL_VERSION);
    assert_eq!(
        fixture["push"]["messages"][0]["delivery_id"],
        fixture["ack"]["delivery_id"]
    );
    assert_eq!(
        fixture["pull_page"]["messages"][0],
        fixture["duplicate_delivery"]
    );
    assert_eq!(fixture["stale_after_delete"]["version"], 20);
    assert_eq!(fixture["newer_recreate"]["version"], 21);
}

#[test]
fn v2_envelope_round_trips_with_an_opaque_base64_payload() {
    let envelope = SyncEnvelopeV2::new(vec![0_u8, 1, 2, 255]).expect("create envelope");

    assert_eq!(envelope.protocol_version, SYNC_PROTOCOL_VERSION);
    assert!(uuid::Uuid::parse_str(&envelope.delivery_id).is_ok());
    assert_eq!(envelope.payload, "AAEC/w==");
    assert_eq!(
        envelope.payload_bytes().expect("decode payload"),
        vec![0, 1, 2, 255]
    );

    let decoded: SyncEnvelopeV2 =
        serde_json::from_str(&serde_json::to_string(&envelope).expect("serialize envelope"))
            .expect("deserialize envelope");
    assert_eq!(decoded, envelope);
}

#[test]
fn v2_envelope_rejects_wrong_version_invalid_delivery_id_and_empty_payload() {
    let invalid_version = SyncEnvelopeV2 {
        protocol_version: SYNC_PROTOCOL_VERSION + 1,
        delivery_id: uuid::Uuid::new_v4().to_string(),
        payload: "AA==".to_string(),
        relay_seq: None,
    };
    assert_eq!(
        invalid_version.validate(),
        Err(SyncProtocolError::UnsupportedProtocolVersion {
            found: SYNC_PROTOCOL_VERSION + 1
        })
    );

    let invalid_delivery_id = SyncEnvelopeV2 {
        protocol_version: SYNC_PROTOCOL_VERSION,
        delivery_id: "not-a-uuid".to_string(),
        payload: "AA==".to_string(),
        relay_seq: None,
    };
    assert_eq!(
        invalid_delivery_id.validate(),
        Err(SyncProtocolError::InvalidDeliveryId)
    );

    let empty_payload = SyncEnvelopeV2 {
        protocol_version: SYNC_PROTOCOL_VERSION,
        delivery_id: uuid::Uuid::new_v4().to_string(),
        payload: String::new(),
        relay_seq: None,
    };
    assert_eq!(
        empty_payload.validate(),
        Err(SyncProtocolError::EmptyPayload)
    );
}

#[test]
fn sync_state_response_has_a_stable_cross_platform_json_shape() {
    let response = SyncStateResponse {
        kind: SyncStatus::Synced,
        last_success_at: Some(1_726_144_000),
        pending_outbox_count: 3,
        last_error: None,
    };

    assert_eq!(
        serde_json::to_value(response).expect("serialize state"),
        serde_json::json!({
            "kind": "synced",
            "last_success_at": 1_726_144_000,
            "pending_outbox_count": 3,
            "last_error": null,
        })
    );
}

#[test]
fn receive_cursor_advances_only_after_a_valid_v2_envelope() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let mut core = MahoCore::new().with_storage(":memory:");
    let room_id = "a".repeat(32);

    let invalid = SyncEnvelopeV2 {
        protocol_version: SYNC_PROTOCOL_VERSION,
        delivery_id: uuid::Uuid::new_v4().to_string(),
        payload: "not-base64".to_string(),
        relay_seq: Some(7),
    };
    assert!(core
        .apply_sync_envelope_and_advance_cursor(
            &room_id,
            &serde_json::to_string(&invalid).expect("serialize invalid envelope"),
        )
        .is_err());
    assert_eq!(core.get_sync_receive_cursor(&room_id).expect("cursor"), 0);

    let ack = RelayAckV2 {
        delivery_id: uuid::Uuid::new_v4().to_string(),
        seq: 9,
    };
    assert!(
        !core
            .accept_sync_ack(&serde_json::to_string(&ack).expect("serialize unmatched ACK"))
            .expect("unmatched ACK is safe"),
        "an unknown ACK cannot complete an outbox row"
    );
}
