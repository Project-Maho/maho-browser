use maho_types::content_blocking::FilterListMetadata;

fn metadata(failure_count: u32, last_attempt_timestamp: Option<i64>) -> FilterListMetadata {
    FilterListMetadata {
        id: "easylist".to_string(),
        name: "EasyList".to_string(),
        url: "https://easylist.to/easylist/easylist.txt".to_string(),
        enabled: true,
        rule_count: 0,
        etag: None,
        last_modified: None,
        sha256: None,
        last_attempt_timestamp,
        last_success_timestamp: None,
        failure_count,
        last_status: None,
        last_error: None,
    }
}

#[test]
fn retry_timestamp_serializes_from_failure_tier_and_last_attempt() {
    let last_attempt = 1_700_000_000;
    let tiers = [
        (0, None),
        (1, Some(last_attempt + 15 * 60)),
        (2, Some(last_attempt + 60 * 60)),
        (3, Some(last_attempt + 6 * 60 * 60)),
        (4, Some(last_attempt + 24 * 60 * 60)),
        (50, Some(last_attempt + 24 * 60 * 60)),
    ];

    for (failure_count, expected) in tiers {
        let value = serde_json::to_value(metadata(failure_count, Some(last_attempt))).unwrap();
        assert_eq!(value["nextRetryTimestamp"].as_i64(), expected);
    }
}

#[test]
fn persisted_metadata_without_retry_field_remains_backward_compatible() {
    let json = r#"{
        "id":"easylist",
        "name":"EasyList",
        "url":"https://easylist.to/easylist/easylist.txt",
        "enabled":true,
        "ruleCount":0,
        "failureCount":1,
        "lastAttemptTimestamp":1700000000
    }"#;

    let metadata: FilterListMetadata = serde_json::from_str(json).unwrap();
    assert_eq!(
        metadata.next_retry_timestamp(),
        Some(1_700_000_000 + 15 * 60)
    );
}
