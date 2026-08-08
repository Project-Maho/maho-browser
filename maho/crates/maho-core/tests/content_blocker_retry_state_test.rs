use maho_core::content_blocker::ContentBlocker;

#[test]
fn state_json_derives_per_list_retry_timestamp_from_persisted_health() {
    let mut blocker = ContentBlocker::new();
    let healthy = serde_json::to_value(blocker.get_state_dto()).unwrap();
    let healthy_list = healthy["lists"]
        .as_array()
        .unwrap()
        .iter()
        .find(|list| list["id"] == "easylist")
        .unwrap();
    assert_eq!(healthy_list["nextRetryTimestamp"], serde_json::Value::Null);

    for expected_delay in [15 * 60, 60 * 60, 6 * 60 * 60, 24 * 60 * 60] {
        blocker.record_update_failure("easylist", 500, "failed");
        let state = serde_json::to_value(blocker.get_state_dto()).unwrap();
        let list = state["lists"]
            .as_array()
            .unwrap()
            .iter()
            .find(|list| list["id"] == "easylist")
            .unwrap();
        let last_attempt = list["lastAttemptTimestamp"].as_i64().unwrap();
        assert_eq!(
            list["nextRetryTimestamp"].as_i64(),
            Some(last_attempt + expected_delay)
        );
    }
}
