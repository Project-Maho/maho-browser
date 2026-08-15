use std::sync::Arc;

use async_trait::async_trait;
use maho_agent::{
    AgentRuntime, BrowserToolBridge, BrowserToolBridgeError, BrowserToolDescriptor,
    BrowserToolExecution, BrowserToolExecutionReceipt, BrowserToolPolicy,
};
use maho_types::tool::ToolPermission;

#[path = "browser_action_shims/support.rs"]
mod support;

const SENTINEL: &str = "S3NTINEL-dynamic-bridge-secret";

struct DynamicBridge {
    descriptors: Result<Vec<BrowserToolDescriptor>, BrowserToolBridgeError>,
}

#[async_trait]
impl BrowserToolBridge for DynamicBridge {
    async fn list_tool_descriptors(
        &self,
    ) -> Result<Vec<BrowserToolDescriptor>, BrowserToolBridgeError> {
        self.descriptors.clone()
    }

    async fn execute_tool(
        &self,
        capability_id: &str,
        _args_json: &str,
    ) -> Result<BrowserToolExecution, BrowserToolBridgeError> {
        if capability_id == "browser.fail" {
            return Err(BrowserToolBridgeError::new(
                "execution_denied",
                "policy denied execution",
                false,
            ));
        }
        Ok(BrowserToolExecution {
            output_json: r#"{"ok":true}"#.to_string(),
            receipt: BrowserToolExecutionReceipt {
                capability_id: capability_id.to_string(),
                execution_id: "receipt-1".to_string(),
                metadata: serde_json::json!({"secret": SENTINEL, "status": "completed"}),
            },
        })
    }
}

fn descriptor(capability_id: &str, name: &str) -> BrowserToolDescriptor {
    BrowserToolDescriptor {
        capability_id: capability_id.to_string(),
        name: name.to_string(),
        description: format!("Dynamic {name}"),
        input_schema: serde_json::json!({
            "type": "object",
            "additionalProperties": false,
            "properties": {}
        }),
        schema_version: 1,
        policy: BrowserToolPolicy {
            sensitive: false,
            permission: ToolPermission::AutoApprove,
        },
    }
}

#[tokio::test]
async fn dynamic_bridge_discovery_exposes_valid_rejects_malformed_and_preserves_local_tools() {
    let harness = support::backend_with_profile_tools(&[]);
    let bridge = Arc::new(DynamicBridge {
        descriptors: Ok(vec![
            descriptor("browser.tabs.list", "dynamic_list_tabs"),
            descriptor("browser.page.read", "dynamic_read_page"),
            BrowserToolDescriptor {
                input_schema: serde_json::json!(["not", "a", "schema"]),
                ..descriptor("browser.bad", "dynamic_bad")
            },
        ]),
    });
    harness
        .backend
        .set_browser_tool_bridge(bridge as Arc<dyn BrowserToolBridge>)
        .expect("bridge installs");

    let tools = harness
        .backend
        .list_tools()
        .await
        .expect("discovery succeeds");
    let names: Vec<_> = tools.iter().map(|tool| tool.name.as_str()).collect();

    assert!(names.contains(&"dynamic_list_tabs"));
    assert!(names.contains(&"dynamic_read_page"));
    assert!(!names.contains(&"dynamic_bad"));
    assert!(names.contains(&"fs_read"));
    assert!(names.contains(&"fs_write"));
    assert!(names.contains(&"shell_exec"));
    assert!(tools
        .iter()
        .filter(|tool| tool.name.starts_with("dynamic_"))
        .all(|tool| tool.provenance == maho_types::tool::ToolProvenance::BuiltinBrowser));
}

#[tokio::test]
async fn unavailable_or_default_discovery_fails_closed_without_hiding_local_tools() {
    let unavailable = support::backend_with_profile_tools(&[]);
    unavailable
        .backend
        .set_browser_tool_bridge(Arc::new(DynamicBridge {
            descriptors: Err(BrowserToolBridgeError::new(
                "bridge_offline",
                "browser process unavailable",
                true,
            )),
        }))
        .expect("bridge installs");
    let unavailable_names = support::tool_names(&unavailable.backend).await;
    assert!(support::has_tool(&unavailable_names, "fs_read"));
    assert!(!unavailable_names
        .iter()
        .any(|name| name.starts_with("dynamic_")));

    #[derive(Default)]
    struct NoDiscoveryBridge;
    #[async_trait]
    impl BrowserToolBridge for NoDiscoveryBridge {}

    let defaulted = support::backend_with_profile_tools(&[]);
    defaulted
        .backend
        .set_browser_tool_bridge(Arc::new(NoDiscoveryBridge))
        .expect("bridge installs");
    let defaulted_names = support::tool_names(&defaulted.backend).await;
    assert!(support::has_tool(&defaulted_names, "fs_read"));
    assert!(!defaulted_names
        .iter()
        .any(|name| name.starts_with("browser_")));
}

#[tokio::test]
async fn descriptor_validation_rejects_duplicates_missing_policy_and_workspace_filter_regressions()
{
    let duplicate = descriptor("browser.tabs.list", "duplicate_name");
    let harness = support::backend_with_profile_tools(&["dynamic_list_tabs"]);
    harness
        .backend
        .set_browser_tool_bridge(Arc::new(DynamicBridge {
            descriptors: Ok(vec![
                descriptor("browser.tabs.list", "dynamic_list_tabs"),
                duplicate,
                descriptor("browser.page.read", "dynamic_read_page"),
            ]),
        }))
        .expect("bridge installs");
    let names = support::tool_names(&harness.backend).await;
    assert_eq!(names, vec!["dynamic_list_tabs".to_string()]);

    let missing_policy = serde_json::json!({
        "capabilityId": "browser.tabs.list",
        "name": "missing_policy",
        "description": "invalid",
        "inputSchema": {"type": "object"},
        "schemaVersion": 1
    });
    assert!(serde_json::from_value::<BrowserToolDescriptor>(missing_policy).is_err());
}

#[tokio::test]
async fn typed_execution_returns_structured_error_and_redacted_receipt() {
    let bridge = DynamicBridge {
        descriptors: Ok(Vec::new()),
    };
    let execution = bridge
        .execute_tool("browser.tabs.list", "{}")
        .await
        .expect("execution succeeds");
    let redacted = execution.receipt.redacted();
    let serialized = serde_json::to_string(&redacted).expect("receipt serializes");
    assert!(!serialized.contains(SENTINEL));
    assert!(serialized.contains("[REDACTED]"));

    let error = bridge
        .execute_tool("browser.fail", "{}")
        .await
        .expect_err("execution fails");
    assert_eq!(error.code, "execution_denied");
    assert!(!error.retryable);
}
