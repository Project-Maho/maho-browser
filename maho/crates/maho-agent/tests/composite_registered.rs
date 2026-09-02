//! Plan row 11 — `composite_execute` registration + dispatch integration tests.
//!
//! Exercises the REGISTERED tool path end to end: descriptor in the platform
//! tool registry → swiftide tool assembly in `run_turn` → composite runner →
//! per-step broker crossings through the browser tool bridge. The mid-step
//! failure case pins the contract's failure-isolation rule (a failed step
//! never aborts later steps) and the streaming progress events (one
//! Started/Finished pair per step over the row-1 Status event plumbing).

#[path = "browser_action_shims/support.rs"]
mod support;

use std::sync::{Arc, Mutex, MutexGuard};

use async_trait::async_trait;
use maho_agent::composite::{
    CompositeAggregateStatus, CompositeBatchResult, CompositeExecutionConfig,
    CompositeProgressPhase, CompositeStepStatus,
};
use maho_agent::{
    AgentRuntime, AgentStreamEvent, BrowserToolBridge, BrowserToolBridgeError,
    BrowserToolDescriptor, BrowserToolExecution, BrowserToolExecutionReceipt, BrowserToolPolicy,
};
use swiftide_core::chat_completion::ChatCompletion;
use support::{backend_with_profile_tools, has_tool, OneToolCallLlm};

const COMPOSITE_ARGS_STEP2_FAILS: &str = r##"{
    "batch_id": "batch-1",
    "steps": [
        { "tool": "browser_navigate", "args": { "url": "https://a.example" } },
        { "tool": "browser_click", "args": { "selector": "#submit" } },
        { "tool": "browser_snapshot", "args": {} }
    ]
}"##;

/// Scripted browser bridge: records every `execute_tool` crossing (each one
/// is a capability-broker crossing), succeeds every capability except
/// `browser_click`, which fails with a machine-consumable element error —
/// mirroring the contract's step-2 failure example.
struct ScriptedOutcomeBridge {
    calls: Mutex<Vec<(String, String)>>,
}

impl ScriptedOutcomeBridge {
    fn calls(&self) -> Vec<(String, String)> {
        self.lock().clone()
    }

    fn lock(&self) -> MutexGuard<'_, Vec<(String, String)>> {
        self.calls
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
    }
}

impl Default for ScriptedOutcomeBridge {
    fn default() -> Self {
        Self {
            calls: Mutex::new(Vec::new()),
        }
    }
}

#[async_trait]
impl BrowserToolBridge for ScriptedOutcomeBridge {
    async fn list_tool_descriptors(
        &self,
    ) -> Result<Vec<BrowserToolDescriptor>, BrowserToolBridgeError> {
        let names = ["browser_navigate", "browser_click", "browser_snapshot"];
        Ok(names
            .into_iter()
            .map(|name| BrowserToolDescriptor {
                capability_id: name.to_string(),
                name: name.to_string(),
                description: format!("Test browser tool {name}"),
                input_schema: serde_json::from_str(
                    r#"{"type":"object","properties":{}}"#,
                )
                .unwrap_or_default(),
                schema_version: 1,
                policy: BrowserToolPolicy {
                    sensitive: false,
                    permission: maho_types::tool::ToolPermission::AutoApprove,
                },
            })
            .collect())
    }

    async fn execute_tool(
        &self,
        capability_id: &str,
        args_json: &str,
    ) -> Result<BrowserToolExecution, BrowserToolBridgeError> {
        self.lock().push((capability_id.to_string(), args_json.to_string()));
        if capability_id == "browser_click" {
            return Err(BrowserToolBridgeError::new(
                "element_not_found",
                "element not found: #submit",
                false,
            ));
        }
        let output_json = if capability_id == "browser_navigate" {
            r#"{"url":"https://a.example","navigated":true}"#.to_string()
        } else {
            r#"{"text":"page text"}"#.to_string()
        };
        Ok(BrowserToolExecution {
            output_json,
            receipt: BrowserToolExecutionReceipt {
                capability_id: capability_id.to_string(),
                execution_id: "test-composite-execution".to_string(),
                metadata: serde_json::Value::Object(Default::default()),
            },
        })
    }
}

fn install_scripted_bridge(harness: &support::BrowserActionHarness) -> Arc<ScriptedOutcomeBridge> {
    let bridge = Arc::new(ScriptedOutcomeBridge::default());
    harness
        .backend
        .set_browser_tool_bridge(Arc::clone(&bridge) as Arc<dyn BrowserToolBridge>)
        .expect("bridge installs");
    bridge
}

/// Captured event sink plus the streaming callback wired into `run_turn`.
type EventSink = Arc<Mutex<Vec<AgentStreamEvent>>>;
type EventCallback = Arc<dyn Fn(AgentStreamEvent) + Send + Sync + 'static>;

fn capture_events() -> (EventSink, EventCallback) {
    let events: EventSink = Arc::new(Mutex::new(Vec::new()));
    let sink = Arc::clone(&events);
    let callback: EventCallback = Arc::new(move |event: AgentStreamEvent| {
        sink.lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .push(event);
    });
    (events, callback)
}

#[test]
fn composite_execute_is_registered_in_platform_registry() {
    let tools = maho_agent::tool_registry::get_platform_tools();
    let def = tools
        .iter()
        .find(|tool| tool.name == "composite_execute")
        .expect("composite_execute descriptor is registered in the platform registry");

    assert_eq!(
        def.parameters_schema["required"],
        serde_json::Value::from(vec!["batch_id", "steps"])
    );
    assert_eq!(def.parameters_schema["properties"]["steps"]["type"], "array");
    assert_eq!(
        def.parameters_schema["properties"]["steps"]["items"]["required"],
        serde_json::Value::from(vec!["tool", "args"])
    );
}

#[tokio::test]
async fn composite_execute_surfaces_through_backend_discovery() {
    let harness = backend_with_profile_tools(&[]);
    let names = support::tool_names(&harness.backend).await;
    assert!(
        has_tool(&names, "composite_execute"),
        "composite_execute must be discoverable through the backend tool listing: {names:?}"
    );
}

#[tokio::test]
async fn registered_composite_batch_step2_failure_isolates_and_streams_progress() {
    let harness = backend_with_profile_tools(&[]);
    let bridge = install_scripted_bridge(&harness);
    harness
        .backend
        .set_composite_config(CompositeExecutionConfig {
            enabled: true,
            max_steps: 8,
        });
    harness.backend.set_llm_override(Box::new(OneToolCallLlm::new(
        "composite_execute",
        COMPOSITE_ARGS_STEP2_FAILS,
    )) as Box<dyn ChatCompletion>);

    let (events, on_event) = capture_events();

    let result = harness
        .backend
        .run_turn(
            "session-composite-partial",
            maho_types::chat::ChatMessage::user(maho_types::chat::ChatContent::text(
                "run the batch".to_string(),
            )),
            None,
            Some(on_event),
        )
        .await;
    assert!(result.is_ok(), "turn with an isolated mid-step failure completes: {result:?}");

    // Each step crossed the bridge individually, in request order — the
    // step-2 failure did not abort step 3 (contract §5/§6).
    let calls = bridge.calls();
    let crossed_tools: Vec<String> = calls.iter().map(|(tool, _)| tool.clone()).collect();
    assert_eq!(
        crossed_tools,
        vec![
            "browser_navigate".to_string(),
            "browser_click".to_string(),
            "browser_snapshot".to_string()
        ],
        "every step must cross the bridge exactly once, in order: {calls:?}"
    );
    assert!(
        calls[0].1.contains("https://a.example"),
        "step args are carried to the executor verbatim: {calls:?}"
    );

    // The ToolResult event carries the batch result JSON: per-step statuses
    // and the CompletedWithFailures aggregate.
    let tool_results: Vec<(String, bool)> = events
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
        .iter()
        .filter_map(|event| match event {
            AgentStreamEvent::ToolResult {
                name, result, succeeded, ..
            } if name == "composite_execute" => Some((result.clone(), *succeeded)),
            _ => None,
        })
        .collect();
    assert_eq!(tool_results.len(), 1, "one composite_execute ToolResult event");
    let (result_json, succeeded) = &tool_results[0];
    assert!(succeeded, "partial failure is a successful batch execution with per-step failures");
    let batch: CompositeBatchResult =
        serde_json::from_str(result_json).expect("ToolResult carries the batch result JSON");
    assert_eq!(batch.batch_id, "batch-1");
    assert_eq!(batch.steps.len(), 3);
    assert_eq!(batch.steps[0].tool, "browser_navigate");
    assert_eq!(batch.steps[0].status, CompositeStepStatus::Succeeded);
    assert!(batch.steps[0].output.is_some());
    assert_eq!(batch.steps[1].tool, "browser_click");
    assert_eq!(batch.steps[1].status, CompositeStepStatus::Failed);
    assert_eq!(
        batch.steps[1].error.as_deref(),
        Some("element_not_found: element not found: #submit"),
        "the failed step carries the executor-reported error"
    );
    assert_eq!(batch.steps[2].tool, "browser_snapshot");
    assert_eq!(batch.steps[2].status, CompositeStepStatus::Succeeded);
    assert_eq!(batch.succeeded(), 2);
    assert_eq!(batch.failed(), 1);
    assert_eq!(
        batch.aggregate_status(),
        CompositeAggregateStatus::CompletedWithFailures
    );

    // Streaming progress over the row-1 Status event plumbing: exactly one
    // Started/Finished pair per step (2 * n events), with contract §4 payloads.
    let status_messages: Vec<String> = events
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
        .iter()
        .filter_map(|event| match event {
            AgentStreamEvent::Status { message, .. } => Some(message.clone()),
            _ => None,
        })
        .collect();
    assert_eq!(
        status_messages.len(),
        6,
        "one Started + one Finished progress event per step: {status_messages:?}"
    );
    let progress: Vec<serde_json::Value> = status_messages
        .iter()
        .map(|message| serde_json::from_str(message).expect("progress payload is JSON"))
        .collect();
    let expected_phases = [
        (0usize, CompositeProgressPhase::Started),
        (0, CompositeProgressPhase::Finished),
        (1, CompositeProgressPhase::Started),
        (1, CompositeProgressPhase::Finished),
        (2, CompositeProgressPhase::Started),
        (2, CompositeProgressPhase::Finished),
    ];
    for (event, (step_index, phase)) in progress.iter().zip(expected_phases.iter()) {
        assert_eq!(event["batch_id"], "batch-1");
        assert_eq!(event["step_index"], serde_json::Value::from(*step_index));
        let expected_phase = match phase {
            CompositeProgressPhase::Started => "started",
            CompositeProgressPhase::Finished => "finished",
        };
        assert_eq!(event["phase"], expected_phase);
        assert_eq!(event["total"], serde_json::Value::from(3));
    }
    // Started events carry null status; Finished events always carry status.
    assert_eq!(progress[0]["status"], serde_json::Value::Null);
    assert_eq!(progress[2]["status"], serde_json::Value::Null);
    assert_eq!(progress[4]["status"], serde_json::Value::Null);
    assert_eq!(progress[1]["status"], "succeeded");
    assert_eq!(progress[3]["status"], "failed");
    assert_eq!(
        progress[3]["error"],
        "element_not_found: element not found: #submit",
        "failed-step progress carries the executor-reported error only"
    );
    assert_eq!(progress[5]["status"], "succeeded");
    // `completed` counts settled steps monotonically.
    assert_eq!(progress[1]["completed"], serde_json::Value::from(1));
    assert_eq!(progress[3]["completed"], serde_json::Value::from(2));
    assert_eq!(progress[5]["completed"], serde_json::Value::from(3));
}

#[tokio::test]
async fn registered_composite_tool_disabled_gate_refuses_without_crossing_bridge() {
    let harness = backend_with_profile_tools(&[]);
    let bridge = install_scripted_bridge(&harness);
    // No set_composite_config call: the default gate is disabled (fail-closed).
    harness.backend.set_llm_override(Box::new(OneToolCallLlm::new(
        "composite_execute",
        COMPOSITE_ARGS_STEP2_FAILS,
    )) as Box<dyn ChatCompletion>);

    let (events, on_event) = capture_events();

    let result = harness
        .backend
        .run_turn(
            "session-composite-disabled",
            maho_types::chat::ChatMessage::user(maho_types::chat::ChatContent::text(
                "run the batch".to_string(),
            )),
            None,
            Some(on_event),
        )
        .await;
    assert!(result.is_ok(), "the turn still completes when the gate refuses: {result:?}");

    assert!(
        bridge.calls().is_empty(),
        "fail-closed: a disabled runner must not touch the executor"
    );

    let tool_results: Vec<(String, bool)> = events
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
        .iter()
        .filter_map(|event| match event {
            AgentStreamEvent::ToolResult {
                name, result, succeeded, ..
            } if name == "composite_execute" => Some((result.clone(), *succeeded)),
            _ => None,
        })
        .collect();
    assert_eq!(tool_results.len(), 1);
    let (result_json, succeeded) = &tool_results[0];
    assert!(!succeeded, "the disabled gate refuses the batch");
    // The refusal reason travels to the model through the tool-error channel;
    // the stream event payload is fail-closed redacted per the row-2 denial
    // redaction contract (non-JSON payloads become the sentinel).
    assert_eq!(result_json, "\"[REDACTED_INVALID_RESULT]\"");
    // No progress events fired for a refused batch.
    let status_count = events
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
        .iter()
        .filter(|event| matches!(event, AgentStreamEvent::Status { .. }))
        .count();
    assert_eq!(status_count, 0, "a refused batch emits no progress events");
}
