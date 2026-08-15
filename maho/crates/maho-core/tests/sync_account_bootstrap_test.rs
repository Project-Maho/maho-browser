use maho_core::maho_core::MahoCore;

const RELAY_URL: &str = "wss://relay.test";

#[test]
fn account_bootstrap_seed_configures_fresh_cores_to_the_same_room() {
    let bootstrap_info: serde_json::Value =
        serde_json::from_str(&MahoCore::new().generate_sync_bootstrap())
            .expect("generate bootstrap JSON");
    let bootstrap = bootstrap_info["seed"]
        .as_str()
        .expect("bootstrap includes seed")
        .to_owned();
    let mut origin = MahoCore::new();
    let mut joiner = MahoCore::new();

    let origin_room = origin
        .configure_sync_encryption_from_bootstrap(RELAY_URL, &bootstrap)
        .expect("origin accepts valid bootstrap");
    let joiner_room = joiner
        .configure_sync_encryption_from_bootstrap(RELAY_URL, &bootstrap)
        .expect("same account bootstrap configures joining device");

    assert_eq!(origin_room, joiner_room);
    assert_eq!(bootstrap_info["roomId"], origin_room);
}

#[test]
fn malformed_account_bootstrap_fails_closed() {
    let mut core = MahoCore::new();

    let error = core
        .configure_sync_encryption_from_bootstrap(RELAY_URL, "not-base64")
        .expect_err("malformed bootstrap must be rejected");

    assert!(error.contains("Invalid sync bootstrap"));
    assert!(
        core.get_sync_key().is_none(),
        "failed bootstrap must not install a Sync key"
    );
}
