use std::collections::HashMap;

use maho_core::maho_core::MahoCore;
use maho_core::sync_models::{RelayAckV2, SyncEntity, SyncEntityType, SyncEnvelopeV2, SyncStatus};

const RELAY_URL: &str = "wss://relay.test";
const ACCOUNT: &str = "account-a";
const ATTACKER: &str = "account-b";

#[derive(Default)]
struct OpaqueRelayLog {
    owner_by_room: HashMap<String, String>,
    messages_by_room: HashMap<String, Vec<SyncEnvelopeV2>>,
}

impl OpaqueRelayLog {
    fn push(
        &mut self,
        account: &str,
        room_id: &str,
        envelope: SyncEnvelopeV2,
    ) -> Result<RelayAckV2, &'static str> {
        match self.owner_by_room.get(room_id) {
            Some(owner) if owner != account => return Err("room_owned_by_another_user"),
            Some(_) => {}
            None => {
                self.owner_by_room
                    .insert(room_id.to_string(), account.to_string());
            }
        }

        let messages = self
            .messages_by_room
            .entry(room_id.to_string())
            .or_default();
        if let Some(existing) = messages
            .iter()
            .find(|stored| stored.delivery_id == envelope.delivery_id)
        {
            return Ok(RelayAckV2 {
                delivery_id: envelope.delivery_id,
                seq: existing.relay_seq.expect("stored relay sequence"),
            });
        }

        let seq = messages.len() as u64 + 1;
        let mut stored = envelope;
        stored.relay_seq = Some(seq);
        messages.push(stored.clone());
        Ok(RelayAckV2 {
            delivery_id: stored.delivery_id,
            seq,
        })
    }

    fn pull(
        &self,
        account: &str,
        room_id: &str,
        after_seq: u64,
    ) -> Result<Vec<SyncEnvelopeV2>, &'static str> {
        match self.owner_by_room.get(room_id) {
            Some(owner) if owner == account => Ok(self
                .messages_by_room
                .get(room_id)
                .into_iter()
                .flatten()
                .filter(|message| message.relay_seq.unwrap_or_default() > after_seq)
                .cloned()
                .collect()),
            _ => Err("room_owned_by_another_user"),
        }
    }

    fn message_count(&self, room_id: &str) -> usize {
        self.messages_by_room.get(room_id).map_or(0, Vec::len)
    }
}

fn configured_client(path: &str, phrase: &str) -> MahoCore {
    let mut core = MahoCore::new().with_storage(path);
    core.configure_sync_encryption_for_recovery_phrase(RELAY_URL, phrase)
        .expect("configure encrypted sync");
    core.set_sync_status(SyncStatus::Synced);
    core
}

fn recovery_phrase() -> String {
    let core = MahoCore::new();
    let details: serde_json::Value =
        serde_json::from_str(&core.generate_sync_key()).expect("generate recovery phrase");
    details["recoveryPhrase"]
        .as_str()
        .expect("recovery phrase")
        .to_string()
}

fn entity(core: &mut MahoCore, id: &str, device_id: u32, deleted: bool) -> SyncEntity {
    SyncEntity {
        entity_type: SyncEntityType::Shortcut,
        entity_id: id.to_string(),
        version: core.next_hlc_ts().expect("next HLC"),
        device_id,
        schema_version: 1,
        modified_at: 1_700_000_000,
        payload_json: serde_json::json!({"action": id, "deleted": deleted}).to_string(),
        deleted,
        queue_row_id: None,
        fields_hlc_json: None,
        profile_id: None,
    }
}

fn push_and_ack(client: &mut MahoCore, relay: &mut OpaqueRelayLog, account: &str) -> RelayAckV2 {
    let room_id = client.get_sync_room_id().expect("recovery-derived room");
    let envelopes = client
        .drain_sync_outgoing_envelopes()
        .expect("lease durable outbox");
    assert_eq!(envelopes.len(), 1);
    let ack = relay
        .push(
            account,
            &room_id,
            envelopes.into_iter().next().expect("one envelope"),
        )
        .expect("store opaque envelope");
    assert!(
        client
            .ack_sync_delivery(&ack.delivery_id, ack.seq)
            .expect("persist ACK"),
        "exact ACK must complete the leased outbox row"
    );
    ack
}

fn pull_all(client: &mut MahoCore, relay: &OpaqueRelayLog, account: &str) {
    let room_id = client.get_sync_room_id().expect("recovery-derived room");
    let cursor = client
        .get_sync_receive_cursor(&room_id)
        .expect("load receive cursor");
    for envelope in relay
        .pull(account, &room_id, cursor)
        .expect("authorized pull")
    {
        client
            .apply_sync_envelope_and_advance_cursor(
                &room_id,
                &serde_json::to_string(&envelope).expect("serialize envelope"),
            )
            .expect("apply encrypted envelope");
    }
}

fn assert_version(client: &MahoCore, id: &str) {
    assert!(
        client
            .storage_ref()
            .expect("storage")
            .get_entity_version("shortcut", id)
            .expect("load version")
            .is_some(),
        "client must converge {id}"
    );
}

#[test]
fn every_logical_writer_reader_pair_converges_over_the_same_opaque_room() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let phrase = recovery_phrase();
    let desktop_db = tempfile::NamedTempFile::new().expect("desktop database");
    let ios_db = tempfile::NamedTempFile::new().expect("iOS database");
    let android_db = tempfile::NamedTempFile::new().expect("Android database");
    let mut desktop = configured_client(desktop_db.path().to_str().expect("path"), &phrase);
    let mut ios = configured_client(ios_db.path().to_str().expect("path"), &phrase);
    let mut android = configured_client(android_db.path().to_str().expect("path"), &phrase);
    let mut relay = OpaqueRelayLog::default();

    let writer_reader_pairs = [
        ("desktop-to-ios", 1_u32, 0_usize, 1_usize),
        ("desktop-to-android", 1, 0, 2),
        ("ios-to-desktop", 2, 1, 0),
        ("ios-to-android", 2, 1, 2),
        ("android-to-desktop", 3, 2, 0),
        ("android-to-ios", 3, 2, 1),
    ];
    let clients = [&mut desktop, &mut ios, &mut android];

    for (id, device_id, writer, reader) in writer_reader_pairs {
        let update = entity(clients[writer], id, device_id, false);
        clients[writer].sync_push_entity_persisted(update);
        push_and_ack(clients[writer], &mut relay, ACCOUNT);
        pull_all(clients[reader], &relay, ACCOUNT);
        assert_version(clients[reader], id);
    }
}

#[test]
fn lost_ack_restart_retries_the_same_delivery_without_duplicate_relay_log() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let phrase = recovery_phrase();
    let desktop_db = tempfile::NamedTempFile::new().expect("desktop database");
    let path = desktop_db.path().to_str().expect("path").to_string();
    let mut desktop = configured_client(&path, &phrase);
    let mut relay = OpaqueRelayLog::default();

    let offline_update = entity(&mut desktop, "offline-before-ack", 1, false);
    desktop.sync_push_entity_persisted(offline_update);
    let room_id = desktop.get_sync_room_id().expect("room");
    let first = desktop
        .drain_sync_outgoing_envelopes()
        .expect("lease first")
        .into_iter()
        .next()
        .expect("one envelope");
    let first_ack = relay
        .push(ACCOUNT, &room_id, first.clone())
        .expect("store first");
    assert_eq!(relay.message_count(&room_id), 1);

    drop(desktop);
    let mut restarted = configured_client(&path, &phrase);
    let retried = restarted
        .drain_sync_outgoing_envelopes()
        .expect("lease retry")
        .into_iter()
        .next()
        .expect("retry envelope");
    assert_eq!(retried.delivery_id, first.delivery_id);
    let retry_ack = relay
        .push(ACCOUNT, &room_id, retried)
        .expect("idempotent retry");
    assert_eq!(retry_ack.seq, first_ack.seq);
    assert_eq!(relay.message_count(&room_id), 1);
    assert!(restarted
        .ack_sync_delivery(&retry_ack.delivery_id, retry_ack.seq)
        .expect("ACK retry"));
}

#[test]
fn delete_tombstone_blocks_stale_replay_but_allows_newer_recreate_and_blocks_other_account() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let phrase = recovery_phrase();
    let desktop_db = tempfile::NamedTempFile::new().expect("desktop database");
    let ios_db = tempfile::NamedTempFile::new().expect("iOS database");
    let mut desktop = configured_client(desktop_db.path().to_str().expect("path"), &phrase);
    let mut ios = configured_client(ios_db.path().to_str().expect("path"), &phrase);
    let mut relay = OpaqueRelayLog::default();

    let initial = entity(&mut desktop, "deleted-shortcut", 1, false);
    desktop.sync_push_entity_persisted(initial.clone());
    push_and_ack(&mut desktop, &mut relay, ACCOUNT);
    pull_all(&mut ios, &relay, ACCOUNT);

    let deleted = entity(&mut desktop, "deleted-shortcut", 1, true);
    desktop.sync_push_entity_persisted(deleted.clone());
    push_and_ack(&mut desktop, &mut relay, ACCOUNT);
    pull_all(&mut ios, &relay, ACCOUNT);
    assert!(ios
        .storage_ref()
        .expect("storage")
        .get_sync_tombstone("shortcut", "deleted-shortcut")
        .expect("read tombstone")
        .is_some());

    let stale = SyncEntity {
        version: deleted.version.saturating_sub(1),
        device_id: 99,
        deleted: false,
        ..initial.clone()
    };
    assert!(
        ios.apply_sync_remote_entities(vec![stale]).is_empty(),
        "stale entity must not resurrect a tombstone"
    );

    let recreate = entity(&mut desktop, "deleted-shortcut", 1, false);
    desktop.sync_push_entity_persisted(recreate);
    push_and_ack(&mut desktop, &mut relay, ACCOUNT);
    pull_all(&mut ios, &relay, ACCOUNT);
    assert_version(&ios, "deleted-shortcut");

    let room_id = desktop.get_sync_room_id().expect("room");
    let rejected = relay.pull(ATTACKER, &room_id, 0);
    assert_eq!(rejected, Err("room_owned_by_another_user"));
}
