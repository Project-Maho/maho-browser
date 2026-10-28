//! Two devices signed into the same account exchange tabs and spaces in memory
//! (no network): device A's outgoing envelopes are fed straight into device B.

use maho_core::maho_core::MahoCore;
use maho_core::sync_models::SyncStatus;
use maho_types::common::Url;
use maho_types::events::core_update::CoreUpdate;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::{SpaceId, TabId};
use maho_types::space::SpaceColor;

const RELAY: &str = "wss://relay.test";

fn account_seed() -> String {
    let bootstrap: serde_json::Value =
        serde_json::from_str(&MahoCore::new().generate_sync_bootstrap()).expect("bootstrap");
    bootstrap["seed"].as_str().expect("seed").to_string()
}

fn device(dir: &tempfile::TempDir, name: &str, seed: &str) -> MahoCore {
    let _ = maho_storage::sqlite::set_sqlcipher_key("two-device-sync-test-key");
    let path = dir.path().join(format!("{name}.db"));
    let mut core = MahoCore::new().with_storage(path.to_str().expect("utf8 path"));
    core.configure_sync_encryption_from_bootstrap(RELAY, seed)
        .expect("configure sync from bootstrap");
    core.set_sync_status(SyncStatus::Synced);
    core
}

fn create_tab(core: &mut MahoCore, space_id: SpaceId, url: &str) -> TabId {
    core.handle_event(ShellEvent::CreateTab {
        space_id,
        url: Some(Url::new(url)),
        parent_id: None,
        tab_id: None,
        window_id: None,
        is_private: false,
    })
    .into_iter()
    .find_map(|update| match update {
        CoreUpdate::TabCreated { tab } => Some(tab.id),
        _ => None,
    })
    .expect("tab created")
}

/// Deliver every queued envelope of `from` to `to`, as the relay would.
fn relay(from: &mut MahoCore, to: &mut MahoCore) -> usize {
    let envelopes = from.drain_sync_outgoing_envelopes().expect("lease outgoing");
    for envelope in &envelopes {
        let json = serde_json::to_string(envelope).expect("serialize envelope");
        to.handle_incoming_sync_envelope(&json)
            .expect("receiver applies envelope");
        assert!(from.ack_sync_delivery(&envelope.delivery_id, 1).expect("ack"));
    }
    envelopes.len()
}

/// Spaces the receiver's UI shows: those owned by its active profile.
fn active_profile_space_ids(core: &MahoCore) -> Vec<SpaceId> {
    let active = core.get_active_profile_id().cloned().expect("active profile");
    core.get_space_view_models()
        .into_iter()
        .filter(|space| space.profile_id.as_ref() == Some(&active))
        .map(|space| space.id)
        .collect()
}

fn active_profile_tab_ids(core: &MahoCore) -> Vec<TabId> {
    let spaces = active_profile_space_ids(core);
    core.get_tab_view_models()
        .into_iter()
        .filter(|tab| spaces.contains(&tab.space_id))
        .map(|tab| tab.id)
        .collect()
}

#[test]
fn default_profile_tab_and_space_reach_the_other_device() {
    let seed = account_seed();
    let dir = tempfile::tempdir().expect("tempdir");
    let mut a = device(&dir, "device-a", &seed);
    let mut b = device(&dir, "device-b", &seed);
    assert_ne!(
        a.get_active_profile_id(),
        b.get_active_profile_id(),
        "each device keeps its own local Default profile id"
    );

    // A's tab lives in its implicit default space, which no event ever creates.
    let a_space = a.get_active_space_id();
    let a_tab = create_tab(&mut a, a_space.clone(), "https://example.com/from-a");

    assert!(relay(&mut a, &mut b) > 0, "device A produced envelopes");

    let b_spaces = active_profile_space_ids(&b);
    assert!(
        b_spaces.contains(&a_space),
        "B's active profile must list A's space {a_space}; got {b_spaces:?}"
    );
    let b_tabs = active_profile_tab_ids(&b);
    assert!(
        b_tabs.contains(&a_tab),
        "B's active profile must list A's tab {a_tab}; got {b_tabs:?}"
    );
    let sidebar = b.get_sidebar_state_v2(&a_space).expect("sidebar for A's space");
    assert!(
        sidebar.contains(&a_tab.to_string()),
        "A's tab must render in B's sidebar for that space: {sidebar}"
    );
}

/// Closing a tab that arrived from the other device must close it there too.
#[test]
fn closing_a_received_tab_closes_it_on_the_sender() {
    let seed = account_seed();
    let dir = tempfile::tempdir().expect("tempdir");
    let mut a = device(&dir, "device-a", &seed);
    let mut b = device(&dir, "device-b", &seed);
    let a_space = a.get_active_space_id();
    let a_tab = create_tab(&mut a, a_space, "https://example.com/close-me");
    assert!(relay(&mut a, &mut b) > 0, "device A produced envelopes");
    assert!(active_profile_tab_ids(&b).contains(&a_tab));

    b.handle_event(ShellEvent::CloseTab {
        tab_id: a_tab.clone(),
        expected_space_id: None,
    });
    assert!(relay(&mut b, &mut a) > 0, "device B must send the close");
    let a_tabs = active_profile_tab_ids(&a);
    assert!(
        !a_tabs.contains(&a_tab),
        "A must drop the tab B closed; got {a_tabs:?}"
    );
}

/// Shells configure the same room again at the start of every sync cycle.
/// Reconfiguring must not park queued changes as offline pushes that the
/// cycle's outbox lease never sees.
#[test]
fn a_change_queued_before_reconfiguring_the_same_room_is_still_sent() {
    let seed = account_seed();
    let dir = tempfile::tempdir().expect("tempdir");
    let mut a = device(&dir, "device-a", &seed);
    let mut b = device(&dir, "device-b", &seed);
    let a_space = a.get_active_space_id();
    let a_tab = create_tab(&mut a, a_space, "https://example.com/reconfigure");
    assert!(relay(&mut a, &mut b) > 0, "device A produced envelopes");

    b.handle_event(ShellEvent::CloseTab {
        tab_id: a_tab.clone(),
        expected_space_id: None,
    });
    b.configure_sync_encryption_from_bootstrap(RELAY, &seed)
        .expect("reconfigure the same room");
    assert!(relay(&mut b, &mut a) > 0, "the close must still be leased");
    assert!(!active_profile_tab_ids(&a).contains(&a_tab));
}

/// A change queued before the app was killed is reloaded from the outbox on
/// the next launch; the first cycle must send it even though the shell has not
/// reported an active transport for the new process yet.
#[test]
fn a_change_queued_before_a_relaunch_is_sent_by_the_first_cycle() {
    let seed = account_seed();
    let dir = tempfile::tempdir().expect("tempdir");
    let mut a = device(&dir, "device-a", &seed);
    let mut b = device(&dir, "device-b", &seed);
    let a_space = a.get_active_space_id();
    let a_tab = create_tab(&mut a, a_space, "https://example.com/relaunch");
    assert!(relay(&mut a, &mut b) > 0, "device A produced envelopes");

    b.handle_event(ShellEvent::CloseTab {
        tab_id: a_tab.clone(),
        expected_space_id: None,
    });
    drop(b);

    let path = dir.path().join("device-b.db");
    let mut relaunched = MahoCore::new().with_storage(path.to_str().expect("utf8 path"));
    relaunched
        .configure_sync_encryption_from_bootstrap(RELAY, &seed)
        .expect("configure after relaunch");
    assert!(
        relay(&mut relaunched, &mut a) > 0,
        "the first cycle after relaunch must send the queued close"
    );
    assert!(!active_profile_tab_ids(&a).contains(&a_tab));
}

/// The receive cursor is committed as soon as an envelope is applied, so a
/// remote space that only lives in memory is never fetched again: it must be
/// on disk before the app can be killed, without waiting for a later save.
#[test]
fn received_space_survives_a_restart_without_an_explicit_save() {
    let seed = account_seed();
    let dir = tempfile::tempdir().expect("tempdir");
    let mut a = device(&dir, "device-a", &seed);
    let b_db = dir.path().join("device-b.db");
    let b_state = dir.path().join("device-b-state");
    let mut b = MahoCore::new()
        .with_storage(b_db.to_str().expect("utf8 path"))
        .with_lmdb_storage(&b_state);
    b.load_state().expect("load fresh state");
    b.configure_sync_encryption_from_bootstrap(RELAY, &seed)
        .expect("configure sync from bootstrap");
    b.set_sync_status(SyncStatus::Synced);

    let a_space = a.get_active_space_id();
    let a_tab = create_tab(&mut a, a_space.clone(), "https://example.com/survives");
    assert!(relay(&mut a, &mut b) > 0, "device A produced envelopes");
    assert!(active_profile_space_ids(&b).contains(&a_space));
    drop(b);

    let mut restarted = MahoCore::new()
        .with_storage(b_db.to_str().expect("utf8 path"))
        .with_lmdb_storage(&b_state);
    restarted.load_state().expect("reload state");
    restarted
        .configure_sync_encryption_from_bootstrap(RELAY, &seed)
        .expect("reconfigure sync");
    let spaces = active_profile_space_ids(&restarted);
    assert!(
        spaces.contains(&a_space),
        "A's space must survive B's restart; got {spaces:?}"
    );
    assert!(active_profile_tab_ids(&restarted).contains(&a_tab));

    // Closing the received tab after the restart must still reach A.
    restarted.set_sync_status(SyncStatus::Synced);
    restarted.handle_event(ShellEvent::CloseTab {
        tab_id: a_tab.clone(),
        expected_space_id: None,
    });
    assert!(
        relay(&mut restarted, &mut a) > 0,
        "the restarted device must send the close"
    );
    assert!(!active_profile_tab_ids(&a).contains(&a_tab));
}

#[test]
fn non_default_profile_is_not_shown_on_the_other_device() {
    let seed = account_seed();
    let dir = tempfile::tempdir().expect("tempdir");
    let mut a = device(&dir, "device-a", &seed);
    let mut b = device(&dir, "device-b", &seed);

    let work = a
        .create_profile_persisted("Work".to_string())
        .expect("work profile");
    assert!(a.switch_profile(&work.id));
    let work_space = a
        .handle_event(ShellEvent::CreateSpace {
            name: "Work Space".to_string(),
            color: SpaceColor {
                hue: 30.0,
                saturation: 0.6,
                brightness: 0.8,
                grain: 0.0,
            },
            profile_id: work.id.clone(),
        })
        .into_iter()
        .find_map(|update| match update {
            CoreUpdate::SpaceCreated { space } => Some(space.id),
            _ => None,
        })
        .expect("work space created");
    let work_tab = create_tab(&mut a, work_space.clone(), "https://example.com/work");

    relay(&mut a, &mut b);

    assert!(!active_profile_space_ids(&b).contains(&work_space));
    assert!(!active_profile_tab_ids(&b).contains(&work_tab));
    if let Some(space) = b
        .get_space_view_models()
        .into_iter()
        .find(|space| space.id == work_space)
    {
        assert_eq!(
            space.profile_id,
            Some(work.id.clone()),
            "a non-default profile keeps its real id across devices"
        );
        assert_ne!(space.profile_id, b.get_active_profile_id().cloned());
    }
}

/// Re-stamp a V2 envelope with an explicit relay sequence.
fn with_relay_seq(envelope: &maho_core::sync_models::SyncEnvelopeV2, seq: u64) -> String {
    let mut stamped = envelope.clone();
    stamped.relay_seq = Some(seq);
    serde_json::to_string(&stamped).expect("serialize envelope")
}

/// A device offline beyond relay retention: its cursor is older than the
/// oldest retained relay entry, so it must restore from the relay snapshot
/// before resuming incremental sync.
#[test]
fn stale_cursor_recovers_full_state_from_relay_snapshot() {
    let seed = account_seed();
    let dir = tempfile::tempdir().expect("tempdir");
    let mut a = device(&dir, "device-a", &seed);
    let mut b = device(&dir, "device-b", &seed);
    let room = b.get_sync_room_id().expect("room");

    // Changes whose change-log entries (seq 1..N) the relay later pruned.
    let a_space = a.get_active_space_id();
    let pruned_tab = create_tab(&mut a, a_space.clone(), "https://example.com/pruned");
    for envelope in a.drain_sync_outgoing_envelopes().expect("lease") {
        assert!(a.ack_sync_delivery(&envelope.delivery_id, 1).expect("ack"));
    }
    let snapshot = a.export_sync_snapshot().expect("snapshot");

    // Next retained entry is far beyond B's cursor (0).
    let fresh_tab = create_tab(&mut a, a_space.clone(), "https://example.com/retained");
    let envelopes = a.drain_sync_outgoing_envelopes().expect("lease retained");
    assert!(!envelopes.is_empty());

    let served = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));
    let served_in = served.clone();
    let expected_room = room.clone();
    b.set_sync_snapshot_source(Some(Box::new(move |room_id| {
        assert_eq!(room_id, expected_room);
        served_in.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
        Ok(Some(snapshot.clone()))
    })));

    let first_seq = 500;
    for (i, envelope) in envelopes.iter().enumerate() {
        b.apply_sync_envelope_and_advance_cursor(&room, &with_relay_seq(envelope, first_seq + i as u64))
            .expect("apply after snapshot recovery");
    }
    assert_eq!(served.load(std::sync::atomic::Ordering::SeqCst), 1, "snapshot fetched once");
    let last = first_seq + envelopes.len() as u64 - 1;
    assert_eq!(b.get_sync_receive_cursor(&room).expect("cursor"), last);
    let b_tabs = active_profile_tab_ids(&b);
    assert!(b_tabs.contains(&pruned_tab), "pruned change restored from snapshot: {b_tabs:?}");
    assert!(b_tabs.contains(&fresh_tab), "retained change applied incrementally: {b_tabs:?}");
}

/// Snapshot fetch failure surfaces a sync error and loses no local data: the
/// cursor stays put so the next cycle retries.
#[test]
fn stale_cursor_snapshot_failure_surfaces_error_without_data_loss() {
    let seed = account_seed();
    let dir = tempfile::tempdir().expect("tempdir");
    let mut a = device(&dir, "device-a", &seed);
    let mut b = device(&dir, "device-b", &seed);
    let room = b.get_sync_room_id().expect("room");

    let b_space = b.get_active_space_id();
    let local_tab = create_tab(&mut b, b_space, "https://example.com/local-only");
    let a_space = a.get_active_space_id();
    create_tab(&mut a, a_space, "https://example.com/remote");
    let envelope = a
        .drain_sync_outgoing_envelopes()
        .expect("lease")
        .into_iter()
        .next()
        .expect("envelope");

    b.set_sync_snapshot_source(Some(Box::new(|_| Err("relay unreachable".to_string()))));
    let err = b
        .apply_sync_envelope_and_advance_cursor(&room, &with_relay_seq(&envelope, 500))
        .expect_err("stale cursor without snapshot must fail");
    assert!(err.contains("snapshot recovery failed"), "{err}");
    assert_eq!(
        b.sync_last_transport_error().map(|e| e.code()),
        Some("snapshot_recovery_failed")
    );
    assert_eq!(b.get_sync_state().kind, SyncStatus::Error);
    assert_eq!(b.get_sync_receive_cursor(&room).expect("cursor"), 0);
    assert!(
        b.get_tab_view_models().iter().any(|tab| tab.id == local_tab),
        "local data kept"
    );
}

/// Holes made only by this device's own acknowledged pushes (the relay does
/// not echo them back) are not a retention gap.
#[test]
fn own_acked_sequences_are_not_a_retention_gap() {
    use maho_core::sync_manager::relay_log_gap;
    assert!(!relay_log_gap(4, 5, 0));
    assert!(!relay_log_gap(4, 7, 2));
    assert!(relay_log_gap(4, 7, 1));
    assert!(relay_log_gap(0, 500, 0));
}

/// Regression (verifier F1): a rowless SendTab this device pushed is stored by
/// the relay at its own seq and never echoed back, so it must not look like a
/// retention gap. Ack it at seq 5, apply a peer envelope at seq 6 from cursor 4,
/// and the snapshot source must never be called.
#[test]
fn own_rowless_send_tab_seq_is_not_a_retention_gap() {
    let seed = account_seed();
    let dir = tempfile::tempdir().expect("tempdir");
    let mut a = device(&dir, "device-a", &seed);
    let mut b = device(&dir, "device-b", &seed);
    let room = b.get_sync_room_id().expect("room");

    b.send_tab_to_device("https://example.com/sent", "Sent", "device-a");
    let rowless = b.drain_sync_outgoing_envelopes().expect("lease send-tab");
    assert_eq!(rowless.len(), 1, "one rowless SendTab delivery");
    assert!(b.ack_sync_delivery(&rowless[0].delivery_id, 5).expect("ack rowless"));
    b.set_sync_receive_cursor(&room, 4).expect("cursor 4");

    let calls = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));
    let calls_in = calls.clone();
    b.set_sync_snapshot_source(Some(Box::new(move |_| {
        calls_in.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
        Err("snapshot source must not be called".to_string())
    })));

    let a_space = a.get_active_space_id();
    let peer_tab = create_tab(&mut a, a_space, "https://example.com/peer");
    let envelope = a
        .drain_sync_outgoing_envelopes()
        .expect("lease")
        .into_iter()
        .next()
        .expect("envelope");
    b.apply_sync_envelope_and_advance_cursor(&room, &with_relay_seq(&envelope, 6))
        .expect("contiguous apply");
    assert_eq!(calls.load(std::sync::atomic::Ordering::SeqCst), 0, "no snapshot fetch");
    assert_eq!(b.get_sync_receive_cursor(&room).expect("cursor"), 6);
    assert!(b.get_tab_view_models().iter().any(|tab| tab.id == peer_tab));
}

/// Verifier F3: no snapshot uploaded for the room yet (404) is not an error;
/// sync warns and continues applying the incremental envelope, like iOS.
#[test]
fn stale_cursor_without_relay_snapshot_continues_incrementally() {
    let seed = account_seed();
    let dir = tempfile::tempdir().expect("tempdir");
    let mut a = device(&dir, "device-a", &seed);
    let mut b = device(&dir, "device-b", &seed);
    let room = b.get_sync_room_id().expect("room");

    let a_space = a.get_active_space_id();
    let fresh_tab = create_tab(&mut a, a_space, "https://example.com/retained");
    let envelopes = a.drain_sync_outgoing_envelopes().expect("lease");
    assert!(!envelopes.is_empty());

    let calls = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));
    let calls_in = calls.clone();
    b.set_sync_snapshot_source(Some(Box::new(move |_| {
        calls_in.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
        Ok(None)
    })));
    for (i, envelope) in envelopes.iter().enumerate() {
        b.apply_sync_envelope_and_advance_cursor(&room, &with_relay_seq(envelope, 500 + i as u64))
            .expect("missing snapshot does not halt sync");
    }
    assert_eq!(calls.load(std::sync::atomic::Ordering::SeqCst), 1);
    assert_eq!(
        b.get_sync_receive_cursor(&room).expect("cursor"),
        500 + envelopes.len() as u64 - 1
    );
    assert!(b.sync_last_transport_error().is_none());
    assert!(active_profile_tab_ids(&b).contains(&fresh_tab));
}
