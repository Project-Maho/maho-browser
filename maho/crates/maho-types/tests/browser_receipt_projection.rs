use maho_types::tool::{
    BrowserReceiptOutcomeStatus, BrowserToolExecutionReceipt, BROWSER_RECEIPT_RESULT_VERSION,
    BROWSER_RECEIPT_SCHEMA_VERSION,
};
use serde_json::json;

const SENTINEL: &str = "S3NTINEL-rust-receipt-secret";

fn receipt(state: &str, approval: &str) -> BrowserToolExecutionReceipt {
    BrowserToolExecutionReceipt {
        capability_id: "browser.page.read".to_string(),
        execution_id: "receipt-stable-1".to_string(),
        metadata: json!({
            "controller": {
                "id": "controller-1",
                "name": "maho-cli",
                "type": "automation",
                "plane": "mcp"
            },
            "target": {"tabId": 7, "origin": "https://example.test"},
            "category": "observe",
            "sensitivity": "low",
            "approval": approval,
            "state": state,
            "timestamps": {"startedAt": 10.0, "completedAt": 11.0},
            "password": SENTINEL,
            "argumentsJson": {"selector": SENTINEL},
            "pageText": SENTINEL
        }),
    }
}

#[test]
fn projects_stable_versioned_receipt_fields_and_redacts_payloads() {
    let projection = receipt("completed", "not_requested").project();
    assert_eq!(projection.schema_version, BROWSER_RECEIPT_SCHEMA_VERSION);
    assert_eq!(projection.result_version, BROWSER_RECEIPT_RESULT_VERSION);
    assert_eq!(projection.receipt_id, "receipt-stable-1");
    assert_eq!(projection.capability_id, "browser.page.read");
    assert_eq!(projection.controller.name, "maho-cli");
    assert_eq!(projection.target.tab_id, Some(7));
    assert_eq!(
        projection.target.origin.as_deref(),
        Some("https://example.test")
    );
    assert_eq!(projection.category, "observe");
    assert_eq!(projection.sensitivity, "low");
    assert_eq!(projection.approval, "not_requested");
    assert_eq!(
        projection.outcome.status,
        BrowserReceiptOutcomeStatus::Succeeded
    );
    assert_eq!(projection.timestamps.started_at, Some(10.0));
    assert_eq!(projection.timestamps.completed_at, Some(11.0));

    let serialized = serde_json::to_string(&projection).unwrap();
    assert!(!serialized.contains(SENTINEL));
    assert!(!serialized.contains("argumentsJson"));
    assert!(!serialized.contains("pageText"));
}

#[test]
fn failure_denial_disconnect_and_unknown_outcomes_are_explicit() {
    for (state, expected) in [
        ("failed", BrowserReceiptOutcomeStatus::Failed),
        ("denied", BrowserReceiptOutcomeStatus::Denied),
        ("disconnected", BrowserReceiptOutcomeStatus::Disconnected),
        ("future_state", BrowserReceiptOutcomeStatus::Unknown),
    ] {
        let approval = if state == "denied" {
            "denied"
        } else {
            "not_requested"
        };
        assert_eq!(receipt(state, approval).project().outcome.status, expected);
    }
}
