// Copyright 2026 Maho Browser. All rights reserved.

//! Composite (batch) page-execution prototype — plan row 10 spike.
//!
//! Contract pinned in `.omo/evidence/composite-execution-contract.md`:
//! one agent-invocable composite tool carries an ordered list of page
//! operations. Steps execute sequentially with strict failure isolation:
//! a failed step records its error on its own result and never aborts
//! later steps. The aggregate result carries per-step status and a tally.
//! Progress is streamed one `Started`/`Finished` event pair per step.
//!
//! Flag-gating is a runtime-config gate (fail-closed default), mirroring
//! the row-1 `runtime_config` plumbing (`parse_runtime_tier` in
//! `permission.rs`): a disabled runner refuses the batch before touching
//! any executor. Row 11 wires this gate to the session runtime_config and
//! registers the tool descriptor in `tool_registry`.

use async_trait::async_trait;
use serde::{Deserialize, Serialize};
use std::sync::Arc;

/// Default hard cap on steps per batch (fail-closed bound).
pub const DEFAULT_MAX_STEPS: usize = 8;

/// One page operation inside a batch: a tool name plus its argument object.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct CompositeStep {
    pub tool: String,
    pub args: serde_json::Value,
}

/// A composite execution request: an ordered batch of steps under one id.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct CompositeRequest {
    pub batch_id: String,
    pub steps: Vec<CompositeStep>,
}

/// Runtime-config gate for the composite tool. Mirrors the row-1
/// `runtime_config` plumbing; `enabled: false` (the default) fails closed.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct CompositeExecutionConfig {
    pub enabled: bool,
    pub max_steps: usize,
}

impl Default for CompositeExecutionConfig {
    fn default() -> Self {
        Self {
            enabled: false,
            max_steps: DEFAULT_MAX_STEPS,
        }
    }
}

/// Terminal status of a single step. There is no `Skipped` variant by
/// contract: a failed step never causes later steps to be skipped.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum CompositeStepStatus {
    Succeeded,
    Failed,
}

/// Per-step result. Exactly one of `output`/`error` is `Some`.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct CompositeStepResult {
    pub index: usize,
    pub tool: String,
    pub status: CompositeStepStatus,
    pub output: Option<serde_json::Value>,
    pub error: Option<String>,
}

/// Aggregate outcome over the whole batch.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum CompositeAggregateStatus {
    Completed,
    CompletedWithFailures,
}

/// The batch-level result: per-step results in request order plus tallies.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct CompositeBatchResult {
    pub batch_id: String,
    pub steps: Vec<CompositeStepResult>,
}

impl CompositeBatchResult {
    pub fn succeeded(&self) -> usize {
        self.steps
            .iter()
            .filter(|step| step.status == CompositeStepStatus::Succeeded)
            .count()
    }

    pub fn failed(&self) -> usize {
        self.steps
            .iter()
            .filter(|step| step.status == CompositeStepStatus::Failed)
            .count()
    }

    pub fn aggregate_status(&self) -> CompositeAggregateStatus {
        if self.failed() == 0 {
            CompositeAggregateStatus::Completed
        } else {
            CompositeAggregateStatus::CompletedWithFailures
        }
    }
}

/// Phase of a streaming progress event: emitted before and after each step.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum CompositeProgressPhase {
    Started,
    Finished,
}

/// Streaming progress event. One `Started` event fires before each step
/// begins and one `Finished` event fires after it settles; `completed` is
/// the number of settled steps at emission time, `total` the batch size.
/// Row 11 maps these onto the FFI `Status` event kind.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct CompositeProgress {
    pub batch_id: String,
    pub step_index: usize,
    pub tool: String,
    pub phase: CompositeProgressPhase,
    /// Set only on `Finished`: terminal status of the step.
    pub status: Option<CompositeStepStatus>,
    /// Set only on `Finished` for a failed step: the captured error.
    pub error: Option<String>,
    pub completed: usize,
    pub total: usize,
}

/// Errors raised by the runner before or during batch admission.
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum CompositeError {
    #[error("composite execution is disabled by runtime_config (fail-closed)")]
    Disabled,
    #[error("batch has {got} steps, exceeding the composite cap of {max}")]
    TooManySteps { max: usize, got: usize },
    #[error("batch contains no steps")]
    EmptyBatch,
}

/// The per-step execution surface the composite runner drives. Row 11
/// binds this to the browser tool executor behind the broker; the
/// prototype defines only the contract so the runner is testable.
#[async_trait]
pub trait CompositeStepExecutor: Send + Sync {
    /// Execute one step; `Err` carries the machine-consumable error string
    /// recorded verbatim on that step's result. Must never panic the batch.
    async fn execute_step(
        &self,
        tool: &str,
        args: &serde_json::Value,
    ) -> Result<serde_json::Value, String>;
}

/// The composite (batch) runner prototype.
pub struct CompositeRunner {
    config: CompositeExecutionConfig,
}

/// Row 11 binding of [`CompositeStepExecutor`] to the real browser tool
/// executor: every step dispatches through `BrowserToolBridge::execute_tool`,
/// exactly the crossing a single tool call makes, so each step is evaluated
/// by the capability broker on its own (tier/final_confirm/origin/lease all
/// apply per step). A batch can therefore never do what its steps could not
/// do individually (contract §6). Step names are the canonical browser
/// capability ids (`capability_id == tool name` for the action set).
pub struct BridgeStepExecutor {
    bridge: Option<Arc<dyn crate::BrowserToolBridge>>,
}

impl BridgeStepExecutor {
    /// `None` when the session has no browser bridge; every step then fails
    /// in isolation with an executor-reported error.
    pub fn new(bridge: Option<Arc<dyn crate::BrowserToolBridge>>) -> Self {
        Self { bridge }
    }
}

#[async_trait]
impl CompositeStepExecutor for BridgeStepExecutor {
    async fn execute_step(
        &self,
        tool: &str,
        args: &serde_json::Value,
    ) -> Result<serde_json::Value, String> {
        let Some(bridge) = self.bridge.as_ref() else {
            return Err("browser tool bridge unavailable".to_string());
        };
        match bridge.execute_tool(tool, &args.to_string()).await {
            Ok(execution) => serde_json::from_str(&execution.output_json)
                .map_err(|error| format!("composite step output not parseable: {error}")),
            Err(error) => Err(error.to_string()),
        }
    }
}

impl CompositeRunner {
    pub fn new(config: CompositeExecutionConfig) -> Self {
        Self { config }
    }

    /// Execute the batch sequentially with failure isolation: a failed step
    /// records `Failed` plus its error and the loop continues to the next
    /// step. Emits one `Started`/`Finished` progress pair per step.
    pub async fn execute_batch(
        &self,
        request: &CompositeRequest,
        executor: &dyn CompositeStepExecutor,
        mut on_progress: impl FnMut(CompositeProgress),
    ) -> Result<CompositeBatchResult, CompositeError> {
        if !self.config.enabled {
            return Err(CompositeError::Disabled);
        }
        if request.steps.is_empty() {
            return Err(CompositeError::EmptyBatch);
        }
        if request.steps.len() > self.config.max_steps {
            return Err(CompositeError::TooManySteps {
                max: self.config.max_steps,
                got: request.steps.len(),
            });
        }

        let total = request.steps.len();
        let mut results = Vec::with_capacity(total);

        // Failure isolation lives here: the match arm for `Err` produces a
        // `Failed` result and the loop provably continues to later steps.
        for (index, step) in request.steps.iter().enumerate() {
            on_progress(CompositeProgress {
                batch_id: request.batch_id.clone(),
                step_index: index,
                tool: step.tool.clone(),
                phase: CompositeProgressPhase::Started,
                status: None,
                error: None,
                completed: results.len(),
                total,
            });

            let step_result = match executor.execute_step(&step.tool, &step.args).await {
                Ok(output) => CompositeStepResult {
                    index,
                    tool: step.tool.clone(),
                    status: CompositeStepStatus::Succeeded,
                    output: Some(output),
                    error: None,
                },
                Err(error) => CompositeStepResult {
                    index,
                    tool: step.tool.clone(),
                    status: CompositeStepStatus::Failed,
                    output: None,
                    error: Some(error),
                },
            };

            let finished = CompositeProgress {
                batch_id: request.batch_id.clone(),
                step_index: index,
                tool: step.tool.clone(),
                phase: CompositeProgressPhase::Finished,
                status: Some(step_result.status),
                error: step_result.error.clone(),
                completed: results.len() + 1,
                total,
            };
            on_progress(finished);

            results.push(step_result);
        }

        Ok(CompositeBatchResult {
            batch_id: request.batch_id.clone(),
            steps: results,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::Value;
    use std::sync::Mutex;

    /// Macro-free object constructor: the workspace clippy config bans the
    /// `Result::unwrap` inside serde_json's `json!` expansion, so tests here
    /// build `Value`s from primitives directly.
    fn jobj(pairs: &[(&str, Value)]) -> Value {
        Value::Object(
            pairs
                .iter()
                .map(|(key, value)| ((*key).to_string(), value.clone()))
                .collect(),
        )
    }

    /// Deterministic scripted executor: outcomes are consumed in request
    /// order; every executed tool name is recorded for isolation asserts.
    struct ScriptedExecutor {
        outcomes: Mutex<Vec<Result<serde_json::Value, String>>>,
        executed: Mutex<Vec<String>>,
    }

    impl ScriptedExecutor {
        fn new(outcomes: Vec<(&str, Result<serde_json::Value, String>)>) -> Self {
            Self {
                outcomes: Mutex::new(outcomes.into_iter().map(|(_, outcome)| outcome).collect()),
                executed: Mutex::new(Vec::new()),
            }
        }

        fn executed_tools(&self) -> Vec<String> {
            // Poison recovery mirrors the crate's `lock_unpoison` pattern.
            self.executed
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner())
                .clone()
        }
    }

    #[async_trait]
    impl CompositeStepExecutor for ScriptedExecutor {
        async fn execute_step(
            &self,
            tool: &str,
            _args: &serde_json::Value,
        ) -> Result<serde_json::Value, String> {
            self.executed
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner())
                .push(tool.to_string());
            let mut outcomes = self
                .outcomes
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner());
            if outcomes.is_empty() {
                panic!("no scripted outcome left for tool {tool}");
            }
            outcomes.remove(0)
        }
    }

    fn three_step_request(batch_id: &str) -> CompositeRequest {
        CompositeRequest {
            batch_id: batch_id.to_string(),
            steps: vec![
                CompositeStep {
                    tool: "browser_navigate".to_string(),
                    args: jobj(&[("url", Value::from("https://a.example"))]),
                },
                CompositeStep {
                    tool: "browser_click".to_string(),
                    args: jobj(&[("selector", Value::from("#submit"))]),
                },
                CompositeStep {
                    tool: "browser_snapshot".to_string(),
                    args: Value::Object(Default::default()),
                },
            ],
        }
    }

    #[test]
    fn batch_partial_failure() {
        // Batch of 3 steps where step 2 fails: steps 1 and 3 must still
        // execute (failure isolation), the aggregate must carry per-step
        // status, and the step-2 error must be captured.
        let tokio_rt = tokio::runtime::Runtime::new().expect("tokio runtime");
        let (batch, executed, progress) = tokio_rt.block_on(async {
            let executor = ScriptedExecutor::new(vec![
                (
                    "browser_navigate",
                    Ok(jobj(&[("url", Value::from("https://a.example"))])),
                ),
                (
                    "browser_click",
                    Err("element not found: #submit".to_string()),
                ),
                (
                    "browser_snapshot",
                    Ok(jobj(&[("text", Value::from("page text"))])),
                ),
            ]);
            let request = three_step_request("batch-1");
            let runner = CompositeRunner::new(CompositeExecutionConfig {
                enabled: true,
                max_steps: 8,
            });
            let mut progress: Vec<CompositeProgress> = Vec::new();
            let outcome = runner
                .execute_batch(&request, &executor, |event| progress.push(event))
                .await;
            (
                outcome.expect("batch with an isolated mid-step failure still returns results"),
                executor.executed_tools(),
                progress,
            )
        });

        // All three steps produced a per-step result, in order.
        assert_eq!(batch.steps.len(), 3);
        assert_eq!(batch.steps[0].tool, "browser_navigate");
        assert_eq!(batch.steps[1].tool, "browser_click");
        assert_eq!(batch.steps[2].tool, "browser_snapshot");

        // Step 1 succeeded and carries its output.
        assert_eq!(batch.steps[0].status, CompositeStepStatus::Succeeded);
        assert_eq!(
            batch.steps[0].output,
            Some(jobj(&[("url", Value::from("https://a.example"))]))
        );

        // Step 2 failed and its error is captured on the result.
        assert_eq!(batch.steps[1].status, CompositeStepStatus::Failed);
        assert!(batch.steps[1].output.is_none());
        assert_eq!(
            batch.steps[1].error.as_deref(),
            Some("element not found: #submit")
        );

        // Failure isolation: step 3 ran despite the step-2 failure.
        assert_eq!(batch.steps[2].status, CompositeStepStatus::Succeeded);
        assert_eq!(
            batch.steps[2].output,
            Some(jobj(&[("text", Value::from("page text"))]))
        );

        // Aggregate carries the per-step tally and mixed status.
        assert_eq!(batch.succeeded(), 2);
        assert_eq!(batch.failed(), 1);
        assert_eq!(
            batch.aggregate_status(),
            CompositeAggregateStatus::CompletedWithFailures
        );

        // The runner executed steps 1, 2, and 3 — no abort after step 2.
        assert_eq!(
            executed,
            vec![
                "browser_navigate".to_string(),
                "browser_click".to_string(),
                "browser_snapshot".to_string()
            ]
        );

        // Streaming progress: Started/Finished per step, statuses carried.
        assert_eq!(progress.len(), 6);
        assert_eq!(progress[0].phase, CompositeProgressPhase::Started);
        assert_eq!(progress[0].step_index, 0);
        assert_eq!(progress[1].phase, CompositeProgressPhase::Finished);
        assert_eq!(progress[1].status, Some(CompositeStepStatus::Succeeded));
        assert_eq!(progress[3].status, Some(CompositeStepStatus::Failed));
        assert_eq!(
            progress[3].error.as_deref(),
            Some("element not found: #submit")
        );
        assert_eq!(progress[5].status, Some(CompositeStepStatus::Succeeded));
        assert_eq!(progress[5].completed, 3);
        assert_eq!(progress[5].total, 3);
    }

    #[test]
    fn disabled_config_fails_closed_without_executing() {
        // The runtime-config gate is fail-closed: a disabled runner refuses
        // the batch before touching the executor.
        let tokio_rt = tokio::runtime::Runtime::new().expect("tokio runtime");
        let (outcome, executed) = tokio_rt.block_on(async {
            let executor = ScriptedExecutor::new(vec![(
                "browser_navigate",
                Ok(Value::Object(Default::default())),
            )]);
            let request = three_step_request("batch-disabled");
            let runner = CompositeRunner::new(CompositeExecutionConfig::default());
            let outcome = runner
                .execute_batch(&request, &executor, |_| {})
                .await
                .expect_err("disabled runner must refuse the batch");
            (outcome, executor.executed_tools())
        });
        assert_eq!(outcome, CompositeError::Disabled);
        assert!(executed.is_empty());
    }

    #[test]
    fn empty_batch_rejected() {
        let tokio_rt = tokio::runtime::Runtime::new().expect("tokio runtime");
        let outcome = tokio_rt.block_on(async {
            let executor = ScriptedExecutor::new(vec![]);
            let request = CompositeRequest {
                batch_id: "batch-empty".to_string(),
                steps: vec![],
            };
            let runner = CompositeRunner::new(CompositeExecutionConfig {
                enabled: true,
                max_steps: 8,
            });
            runner
                .execute_batch(&request, &executor, |_| {})
                .await
                .unwrap_err()
        });
        assert_eq!(outcome, CompositeError::EmptyBatch);
    }

    #[test]
    fn batch_over_cap_rejected() {
        let tokio_rt = tokio::runtime::Runtime::new().expect("tokio runtime");
        let outcome = tokio_rt.block_on(async {
            let executor = ScriptedExecutor::new(vec![]);
            let request = CompositeRequest {
                batch_id: "batch-cap".to_string(),
                steps: (0..3)
                    .map(|_| CompositeStep {
                        tool: "browser_snapshot".to_string(),
                        args: Value::Object(Default::default()),
                    })
                    .collect(),
            };
            let runner = CompositeRunner::new(CompositeExecutionConfig {
                enabled: true,
                max_steps: 2,
            });
            runner
                .execute_batch(&request, &executor, |_| {})
                .await
                .unwrap_err()
        });
        assert_eq!(outcome, CompositeError::TooManySteps { max: 2, got: 3 });
    }

    #[test]
    fn all_steps_succeed_aggregates_completed() {
        let tokio_rt = tokio::runtime::Runtime::new().expect("tokio runtime");
        let batch = tokio_rt.block_on(async {
            let executor = ScriptedExecutor::new(vec![
                ("browser_navigate", Ok(jobj(&[("ok", Value::from(true))]))),
                ("browser_snapshot", Ok(jobj(&[("ok", Value::from(true))]))),
            ]);
            let request = CompositeRequest {
                batch_id: "batch-ok".to_string(),
                steps: vec![
                    CompositeStep {
                        tool: "browser_navigate".to_string(),
                        args: jobj(&[("url", Value::from("https://b.example"))]),
                    },
                    CompositeStep {
                        tool: "browser_snapshot".to_string(),
                        args: Value::Object(Default::default()),
                    },
                ],
            };
            let runner = CompositeRunner::new(CompositeExecutionConfig {
                enabled: true,
                max_steps: 8,
            });
            runner
                .execute_batch(&request, &executor, |_| {})
                .await
                .expect("all-succeed batch returns results")
        });
        assert_eq!(
            batch.aggregate_status(),
            CompositeAggregateStatus::Completed
        );
        assert_eq!(batch.succeeded(), 2);
        assert_eq!(batch.failed(), 0);
    }

    /// Recording browser bridge: succeeds every capability except
    /// `browser_click`, which fails with the contract's element error.
    /// Records each `execute_tool` crossing in order.
    struct RecordingBridge {
        calls: Mutex<Vec<String>>,
    }

    #[async_trait]
    impl crate::BrowserToolBridge for RecordingBridge {
        async fn execute_tool(
            &self,
            capability_id: &str,
            args_json: &str,
        ) -> Result<crate::BrowserToolExecution, crate::BrowserToolBridgeError> {
            self.calls
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner())
                .push(format!("{capability_id}{args_json}"));
            if capability_id == "browser_click" {
                return Err(crate::BrowserToolBridgeError::new(
                    "element_not_found",
                    "element not found: #submit",
                    false,
                ));
            }
            Ok(crate::BrowserToolExecution {
                output_json: format!("{{\"tool\":\"{capability_id}\"}}"),
                receipt: crate::BrowserToolExecutionReceipt {
                    capability_id: capability_id.to_string(),
                    execution_id: "test-execution".to_string(),
                    metadata: Value::Object(Default::default()),
                },
            })
        }
    }

    #[tokio::test]
    async fn bridge_executor_crosses_broker_per_step() {
        // Each step must cross the bridge (i.e. the capability broker) as its
        // own capability request, in request order — including the step after
        // a failure. A batch can never do what its steps could not do
        // individually (contract §6).
        let bridge = Arc::new(RecordingBridge {
            calls: Mutex::new(Vec::new()),
        });
        let executor = BridgeStepExecutor::new(Some(
            Arc::clone(&bridge) as Arc<dyn crate::BrowserToolBridge>
        ));
        let request = three_step_request("batch-bridge");
        let runner = CompositeRunner::new(CompositeExecutionConfig {
            enabled: true,
            max_steps: 8,
        });
        let batch = runner
            .execute_batch(&request, &executor, |_| {})
            .await
            .expect("batch with an isolated mid-step failure still returns results");

        // Three individual crossings, args carried verbatim per step.
        let calls = bridge
            .calls
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .clone();
        assert_eq!(
            calls,
            vec![
                "browser_navigate{\"url\":\"https://a.example\"}".to_string(),
                "browser_click{\"selector\":\"#submit\"}".to_string(),
                "browser_snapshot{}".to_string(),
            ]
        );

        // Per-step statuses carry the bridge outcome; the failed step's error
        // is the executor-reported string.
        assert_eq!(batch.steps[0].status, CompositeStepStatus::Succeeded);
        assert_eq!(batch.steps[1].status, CompositeStepStatus::Failed);
        assert_eq!(
            batch.steps[1].error.as_deref(),
            Some("element_not_found: element not found: #submit")
        );
        assert_eq!(batch.steps[2].status, CompositeStepStatus::Succeeded);
        assert_eq!(
            batch.aggregate_status(),
            CompositeAggregateStatus::CompletedWithFailures
        );
    }

    #[tokio::test]
    async fn bridge_executor_without_bridge_fails_every_step_in_isolation() {
        // No bridge installed: every step fails with an executor-reported
        // error, isolation still runs all steps, and the batch result carries
        // per-step failures (never an abort).
        let executor = BridgeStepExecutor::new(None);
        let request = three_step_request("batch-no-bridge");
        let runner = CompositeRunner::new(CompositeExecutionConfig {
            enabled: true,
            max_steps: 8,
        });
        let batch = runner
            .execute_batch(&request, &executor, |_| {})
            .await
            .expect("batch returns per-step results even without a bridge");
        assert_eq!(batch.steps.len(), 3);
        assert!(batch
            .steps
            .iter()
            .all(|step| step.error.as_deref() == Some("browser tool bridge unavailable")));
        assert_eq!(
            batch.aggregate_status(),
            CompositeAggregateStatus::CompletedWithFailures
        );
    }
}
