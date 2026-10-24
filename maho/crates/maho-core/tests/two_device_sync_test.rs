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
