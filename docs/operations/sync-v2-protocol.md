# Sync Protocol V2 Operations

## Scope

Sync V2 transports only opaque, base64-encoded encrypted envelopes. The Relay
validates envelope metadata and account ownership, but cannot decrypt message
payloads.

Every V2 envelope contains:

```json
{
  "protocol_version": 2,
  "delivery_id": "550e8400-e29b-41d4-a716-446655440000",
  "payload": "AA==",
  "relay_seq": 5
}
```

- `delivery_id` is a client-minted UUID. It is idempotent per Relay room.
- `relay_seq` is assigned by Relay and advances the receiver's durable cursor.
- The room ID is derived from the recovery seed. It is never the Relay
  device-record room ID.

## Delivery and recovery

1. Clients persist a local outbox row before attempting transport.
2. Relay assigns (or reuses) a sequence for the `(room_id, delivery_id)` pair.
3. Only the exact returned ACK removes the row from the outbox.
4. If a client is interrupted before ACK, it resends the same delivery ID after
   restart. Relay returns the original sequence without appending a duplicate.
5. A receiver applies each envelope and advances its cursor through the core
   persistence boundary. Failed application leaves the cursor unchanged.

Tombstones use the `(HLC version, device ID)` ordering tuple. A stale replay
cannot recreate a deleted entity; only a strictly newer user edit can recreate
it.

## Deployment order

1. Deploy Relay V2 decoder, idempotent delivery storage, and explicit room
   ownership checks.
2. Deploy V2 readers to desktop, iOS, and Android.
3. Enable V2 writing with the server-controlled compatibility setting only when
   all supported clients have decoder support.
4. Monitor the metrics below during the rollout.

## Rollback

Disable V2 writing only. Do not remove V2 readers, delivery IDs, receive
cursors, or Relay log rows: existing V2 payloads must remain readable after a
rollback. Re-enable writing only after investigating the operational signals.

## Support diagnostics

For a report, collect only these non-secret values:

- client platform and app version;
- sync state JSON: `kind`, `last_success_at`, `pending_outbox_count`, and
  `last_error`;
- delivery retry count and current receive cursor;
- Relay `invalid_v2_envelope`, `room_owned_by_another_user`, and auth-refresh
  failures;
- relay sequence lag and ACK success rate.

Never collect a recovery phrase, encryption key, bearer token, envelope
payload, or plaintext synchronized entity. Use the delivery ID and relay
sequence to correlate an incident.

## Verification

### Mobile physical-device requirement

Run iOS and Android user-surface, integration, smoke, and three-client
convergence QA only on USB-connected physical devices. Simulators and emulators
may run automated unit tests, but are not evidence for mobile sync validation.

The shared contract fixture is
`maho/tests/fixtures/sync-v2/protocol.json`. Rust core, Relay, iOS, and Android
tests consume it. The core three-client contract harness proves all
desktop/iOS/Android logical writer-reader permutations, ACK-loss restart,
tombstone ordering, and cross-account room rejection.
