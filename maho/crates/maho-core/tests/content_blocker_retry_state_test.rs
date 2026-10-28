use maho_core::content_blocker::{ContentBlocker, FilterUpdateOutcome, TRANSPORT_FAILURE_STATUS};
use maho_types::content_blocking::FilterListUpdateResponse;

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

/// iOS sends this as a literal `1` (`MahoBridge+ContentRules.swift`), so feeding the
/// constant elsewhere cannot catch a change to it; pin the wire value itself.
#[test]
fn transport_failure_status_is_pinned_on_the_wire() {
    assert_eq!(
        TRANSPORT_FAILURE_STATUS, 1,
        "iOS sends the literal 1; changing this value silently escalates offline retries"
    );
}

/// The settings footer's Try Again state reads only `lastError`
/// (`lastDownloadFailed()`), so the offline path must record it and leave the
/// retry schedule alone.
#[test]
fn transport_failure_records_last_error_without_escalating_backoff() {
    let mut blocker = ContentBlocker::new();
    let response = FilterListUpdateResponse {
        list_id: "easylist".to_string(),
        status_code: 1,
        body: None,
        etag: None,
        last_modified: None,
        sha256: None,
    };

    assert_eq!(
        blocker.apply_update_response(response).unwrap(),
        FilterUpdateOutcome::Failed { status_code: 1 }
    );

    let state = serde_json::to_value(blocker.get_state_dto()).unwrap();
    let list = state["lists"]
        .as_array()
        .unwrap()
        .iter()
        .find(|list| list["id"] == "easylist")
        .unwrap();
    assert_eq!(list["lastError"], "network unavailable");
    assert_eq!(list["lastStatus"], 1);
    assert_eq!(list["failureCount"], 0);
    assert_eq!(list["nextRetryTimestamp"], serde_json::Value::Null);
}
