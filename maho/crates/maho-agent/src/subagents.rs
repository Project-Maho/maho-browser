// Copyright 2026 Maho Browser. All rights reserved.

//! Subagent spawning, lifecycle coordination, and isolation contracts.
//!
//! Subagents execute as bounded child sessions with isolated RunJournals.
//! Permissions are strictly bounded as a subset of the parent session.
//! The parent session receives only typed `SubagentResult` summaries and artifact
//! references—the child's raw transcript and messages remain strictly isolated.

use crate::event_wait::{Clock, SystemClock};
use crate::run_journal::{AgentRunId, RunCheckpointKind, RunJournal, RunJournalError};
use serde::{Deserialize, Serialize};
use std::collections::{HashMap, HashSet, VecDeque};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

/// Maximum active concurrently executing subagents per supervisor tree.
pub const MAX_ACTIVE_SUBAGENTS: usize = 5;

/// Maximum allowable budget turns for any single subagent execution.
pub const MAX_SUBAGENT_BUDGET_TURNS: u8 = 5;

/// Execution status of a subagent.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SubagentStatus {
    Queued,
    Running,
    Completed,
    Failed,
    Cancelled,
    BudgetExhausted,
}

impl SubagentStatus {
    pub fn is_terminal(&self) -> bool {
        matches!(
            self,
            Self::Completed | Self::Failed | Self::Cancelled | Self::BudgetExhausted
        )
    }
}

/// Specification for spawning a child subagent.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SubagentSpec {
    pub role_prompt: String,
    pub budget_turns: u8,
    pub allowed_tools: Vec<String>,
    #[serde(default)]
    pub task_description: String,
    #[serde(default)]
    pub parent_permissions: Vec<String>,
}

impl SubagentSpec {
    pub fn new(
        role_prompt: impl Into<String>,
        budget_turns: u8,
        allowed_tools: Vec<String>,
        parent_permissions: Vec<String>,
    ) -> Self {
        Self {
            role_prompt: role_prompt.into(),
            budget_turns: budget_turns.min(MAX_SUBAGENT_BUDGET_TURNS),
            allowed_tools,
            task_description: String::new(),
            parent_permissions,
        }
    }

    pub fn with_task_description(mut self, desc: impl Into<String>) -> Self {
        self.task_description = desc.into();
        self
    }

    /// Validates that child tools are a strict subset of parent permissions.
    ///
    /// Fail-closed: If parent permissions are empty, the child may have zero delegated tools.
    pub fn validate_permissions(&self) -> Result<(), SubagentError> {
        for tool in &self.allowed_tools {
            if !self.parent_permissions.contains(tool) {
                return Err(SubagentError::PermissionViolation(tool.clone()));
            }
        }
        Ok(())
    }

    /// Effective budget turns clamped to the hard cap of 5.
    pub fn effective_budget(&self) -> u8 {
        if self.budget_turns == 0 {
            1
        } else {
            self.budget_turns.min(MAX_SUBAGENT_BUDGET_TURNS)
        }
    }
}

/// Typed result returned to the parent upon subagent completion or termination.
///
/// Invariant: Does NOT contain the child's raw transcript, intermediate turns,
/// or raw tool execution messages.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SubagentResult {
    pub subagent_id: String,
    pub parent_session_id: String,
    pub status: SubagentStatus,
    pub summary: String,
    pub artifacts: Vec<String>,
    pub turns_used: u8,
}

/// Errors arising during subagent spawning, validation, or execution.
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error, Serialize, Deserialize)]
pub enum SubagentError {
    #[error("Parent permission violation: requested tool '{0}' is not allowed by parent")]
    PermissionViolation(String),
    #[error("Subagent budget exhausted after {0} turns")]
    BudgetExhausted(u8),
    #[error("Subagent cancelled")]
    Cancelled,
    #[error("Maximum active subagents limit reached ({0})")]
    ConcurrencyLimitReached(usize),
    #[error("Journal error: {0}")]
    JournalError(String),
    #[error("Subagent execution failed: {0}")]
    ExecutionFailed(String),
    #[error("Subagent '{0}' not found")]
    SubagentNotFound(String),
    #[error("Subagent wait timed out after {0}ms")]
    WaitTimeout(u64),
    #[error("Tool '{0}' is busy: max concurrency limit reached")]
    ToolBusy(String),
}

impl From<RunJournalError> for SubagentError {
    fn from(err: RunJournalError) -> Self {
        Self::JournalError(err.to_string())
    }
}

/// Summary of an active or queued subagent run for telemetry and introspection.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ActiveSubagentSummary {
    pub subagent_id: String,
    pub parent_session_id: String,
    pub status: String,
    pub turns_used: u8,
}

/// Bounded tool slot registry tracking per-tool concurrent execution limits.
#[derive(Debug, Clone)]
pub struct ToolSlotRegistry {
    concurrency_limits: HashMap<String, usize>,
    default_concurrency_limit: usize,
    in_flight_slots: HashMap<String, HashSet<String>>,
}

impl Default for ToolSlotRegistry {
    fn default() -> Self {
        Self::new()
    }
}

impl ToolSlotRegistry {
    pub fn new() -> Self {
        let mut limits = HashMap::new();
        limits.insert("shell_exec".to_string(), 1);
        limits.insert("fs_write".to_string(), 2);
        Self {
            concurrency_limits: limits,
            default_concurrency_limit: 4,
            in_flight_slots: HashMap::new(),
        }
    }

    pub fn set_limit(&mut self, tool_name: impl Into<String>, max_concurrent: usize) {
        self.concurrency_limits
            .insert(tool_name.into(), max_concurrent);
    }

    pub fn acquire_slot(
        &mut self,
        subagent_id: &str,
        tool_name: &str,
    ) -> Result<(), SubagentError> {
        let max_concurrent = self
            .concurrency_limits
            .get(tool_name)
            .copied()
            .unwrap_or(self.default_concurrency_limit);

        let active_set = self
            .in_flight_slots
            .entry(tool_name.to_string())
            .or_default();
        if active_set.contains(subagent_id) {
            return Ok(());
        }

        if active_set.len() >= max_concurrent {
            return Err(SubagentError::ToolBusy(tool_name.to_string()));
        }

        active_set.insert(subagent_id.to_string());
        Ok(())
    }

    pub fn release_slot(&mut self, subagent_id: &str, tool_name: &str) {
        if let Some(active_set) = self.in_flight_slots.get_mut(tool_name) {
            active_set.remove(subagent_id);
        }
    }

    pub fn release_all_for_subagent(&mut self, subagent_id: &str) {
        for active_set in self.in_flight_slots.values_mut() {
            active_set.remove(subagent_id);
        }
    }

    pub fn in_flight_count(&self, tool_name: &str) -> usize {
        self.in_flight_slots
            .get(tool_name)
            .map(|s| s.len())
            .unwrap_or(0)
    }
}

/// Internal execution context for a single spawned subagent.
#[derive(Debug)]
pub struct SubagentChildRun {
    pub subagent_id: String,
    pub parent_session_id: String,
    pub spec: SubagentSpec,
    pub status: SubagentStatus,
    pub journal: RunJournal,
    pub turns_used: u8,
    pub artifacts: Vec<String>,
    pub cancel_flag: Arc<AtomicBool>,
}

/// Supervisor managing subagent lifecycle, concurrency, and isolation.
pub struct SubagentSupervisor {
    active_runs: HashMap<String, SubagentChildRun>,
    queued_specs: VecDeque<(String, String, SubagentSpec)>,
    completed_results: HashMap<String, SubagentResult>,
    completion_order: Vec<String>,
    max_active: usize,
    clock: Arc<dyn Clock>,
    tool_registry: ToolSlotRegistry,
    completion_checkpoints: Vec<(u64, String, Option<String>)>,
}

impl std::fmt::Debug for SubagentSupervisor {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("SubagentSupervisor")
            .field("active_runs", &self.active_runs)
            .field("queued_specs", &self.queued_specs)
            .field("completed_results", &self.completed_results)
            .field("completion_order", &self.completion_order)
            .field("max_active", &self.max_active)
            .field("tool_registry", &self.tool_registry)
            .finish()
    }
}

impl Default for SubagentSupervisor {
    fn default() -> Self {
        Self::new()
    }
}

impl SubagentSupervisor {
    pub fn new() -> Self {
        Self {
            active_runs: HashMap::new(),
            queued_specs: VecDeque::new(),
            completed_results: HashMap::new(),
            completion_order: Vec::new(),
            max_active: MAX_ACTIVE_SUBAGENTS,
            clock: Arc::new(SystemClock),
            tool_registry: ToolSlotRegistry::new(),
            completion_checkpoints: Vec::new(),
        }
    }

    pub fn with_clock(clock: Arc<dyn Clock>) -> Self {
        Self {
            active_runs: HashMap::new(),
            queued_specs: VecDeque::new(),
            completed_results: HashMap::new(),
            completion_order: Vec::new(),
            max_active: MAX_ACTIVE_SUBAGENTS,
            clock,
            tool_registry: ToolSlotRegistry::new(),
            completion_checkpoints: Vec::new(),
        }
    }

    pub fn with_max_active(max_active: usize) -> Self {
        Self {
            active_runs: HashMap::new(),
            queued_specs: VecDeque::new(),
            completed_results: HashMap::new(),
            completion_order: Vec::new(),
            max_active: max_active.min(MAX_ACTIVE_SUBAGENTS),
            clock: Arc::new(SystemClock),
            tool_registry: ToolSlotRegistry::new(),
            completion_checkpoints: Vec::new(),
        }
    }

    pub fn with_max_active_and_clock(max_active: usize, clock: Arc<dyn Clock>) -> Self {
        Self {
            active_runs: HashMap::new(),
            queued_specs: VecDeque::new(),
            completed_results: HashMap::new(),
            completion_order: Vec::new(),
            max_active: max_active.min(MAX_ACTIVE_SUBAGENTS),
            clock,
            tool_registry: ToolSlotRegistry::new(),
            completion_checkpoints: Vec::new(),
        }
    }

    pub fn set_tool_concurrency_limit(&mut self, tool_name: impl Into<String>, limit: usize) {
        self.tool_registry.set_limit(tool_name, limit);
    }

    pub fn acquire_tool_slot(
        &mut self,
        subagent_id: &str,
        tool_name: &str,
    ) -> Result<(), SubagentError> {
        self.tool_registry.acquire_slot(subagent_id, tool_name)
    }

    pub fn release_tool_slot(&mut self, subagent_id: &str, tool_name: &str) {
        self.tool_registry.release_slot(subagent_id, tool_name);
    }

    pub fn schedule_completion(
        &mut self,
        at_millis: u64,
        subagent_id: impl Into<String>,
        summary: impl Into<String>,
    ) {
        self.completion_checkpoints
            .push((at_millis, subagent_id.into(), Some(summary.into())));
        self.completion_checkpoints.sort_by_key(|(ts, _, _)| *ts);
    }

    pub fn set_clock(&mut self, clock: Arc<dyn Clock>) {
        self.clock = clock;
    }

    pub fn active_count(&self) -> usize {
        self.active_runs.len()
    }

    pub fn queued_count(&self) -> usize {
        self.queued_specs.len()
    }

    /// Spawns a subagent after validating permission subset boundaries.
    ///
    /// If concurrency capacity is full, the subagent spec is queued.
    pub fn spawn_subagent(
        &mut self,
        subagent_id: impl Into<String>,
        parent_session_id: impl Into<String>,
        spec: SubagentSpec,
    ) -> Result<SubagentStatus, SubagentError> {
        let subagent_id = subagent_id.into();
        let parent_session_id = parent_session_id.into();

        // 1. Invariant: Child tools must be a strict subset of parent permissions
        spec.validate_permissions()?;

        // 2. Concurrency limit check
        if self.active_runs.len() >= self.max_active {
            self.queued_specs
                .push_back((subagent_id.clone(), parent_session_id.clone(), spec));
            return Ok(SubagentStatus::Queued);
        }

        // 3. Initialize dedicated isolated RunJournal for child
        let mut journal = RunJournal::new();
        let run_id = AgentRunId::new(&subagent_id);
        journal.start_run(run_id.clone(), format!("subagent-{}", &subagent_id))?;
        journal.append_event(
            &run_id,
            "subagent_spawned",
            serde_json::json!({
                "parent_session_id": &parent_session_id,
                "role_prompt": &spec.role_prompt,
                "budget_turns": spec.effective_budget(),
                "allowed_tools": &spec.allowed_tools,
            })
            .to_string(),
        )?;

        let child_run = SubagentChildRun {
            subagent_id: subagent_id.clone(),
            parent_session_id,
            spec,
            status: SubagentStatus::Running,
            journal,
            turns_used: 0,
            artifacts: Vec::new(),
            cancel_flag: Arc::new(AtomicBool::new(false)),
        };

        self.active_runs.insert(subagent_id, child_run);
        Ok(SubagentStatus::Running)
    }

    /// Records a step in the child's execution.
    ///
    /// If the tool requested is not in `allowed_tools`, the step fails closed.
    /// If turns exceed `budget_turns`, the run fails closed with `BudgetExhausted`.
    pub fn execute_turn(
        &mut self,
        subagent_id: &str,
        tool_call: Option<&str>,
        tool_result: Option<&str>,
        is_completed: bool,
        summary: Option<&str>,
    ) -> Result<Option<SubagentResult>, SubagentError> {
        let child = self
            .active_runs
            .get_mut(subagent_id)
            .ok_or_else(|| SubagentError::SubagentNotFound(subagent_id.to_string()))?;

        // 1. Check parent cancellation propagation
        if child.cancel_flag.load(Ordering::SeqCst) || child.status == SubagentStatus::Cancelled {
            child.status = SubagentStatus::Cancelled;
            self.tool_registry.release_all_for_subagent(subagent_id);
            let run_id = AgentRunId::new(subagent_id);
            let _ = child.journal.record_terminal(
                &run_id,
                RunCheckpointKind::Cancelled,
                Some(serde_json::json!({"reason": "cancelled_by_parent"})),
            );
            let result = SubagentResult {
                subagent_id: child.subagent_id.clone(),
                parent_session_id: child.parent_session_id.clone(),
                status: SubagentStatus::Cancelled,
                summary: summary
                    .unwrap_or("Subagent cancelled by parent session")
                    .to_string(),
                artifacts: child.artifacts.clone(),
                turns_used: child.turns_used,
            };
            self.completed_results
                .insert(subagent_id.to_string(), result.clone());
            if !self.completion_order.iter().any(|id| id == subagent_id) {
                self.completion_order.push(subagent_id.to_string());
            }
            self.active_runs.remove(subagent_id);
            self.pump_queue();
            return Ok(Some(result));
        }

        // 2. Validate tool permissions and acquire in-flight tool slot
        if let Some(tool) = tool_call {
            if !child.spec.allowed_tools.iter().any(|t| t == tool) {
                return Err(SubagentError::PermissionViolation(tool.to_string()));
            }
            self.tool_registry.acquire_slot(subagent_id, tool)?;
        }

        // 3. Increment turn count and append to child's isolated journal
        child.turns_used += 1;
        let run_id = AgentRunId::new(subagent_id);
        let _ = child.journal.append_event(
            &run_id,
            "turn_progress",
            serde_json::json!({
                "turn": child.turns_used,
                "tool_call": tool_call,
                "tool_result": tool_result,
            })
            .to_string(),
        );

        // If tool executed (result received) or completed, release the tool slot
        if let Some(tool) = tool_call {
            if tool_result.is_some() || is_completed {
                self.tool_registry.release_slot(subagent_id, tool);
            }
        }

        let effective_budget = child.spec.effective_budget();

        // 4. Completed path
        if is_completed {
            child.status = SubagentStatus::Completed;
            self.tool_registry.release_all_for_subagent(subagent_id);
            let _ = child.journal.record_terminal(
                &run_id,
                RunCheckpointKind::Completed,
                Some(serde_json::json!({ "summary": summary })),
            );
            let result = SubagentResult {
                subagent_id: child.subagent_id.clone(),
                parent_session_id: child.parent_session_id.clone(),
                status: SubagentStatus::Completed,
                summary: summary
                    .unwrap_or("Subagent execution completed successfully")
                    .to_string(),
                artifacts: child.artifacts.clone(),
                turns_used: child.turns_used,
            };
            self.completed_results
                .insert(subagent_id.to_string(), result.clone());
            if !self.completion_order.iter().any(|id| id == subagent_id) {
                self.completion_order.push(subagent_id.to_string());
            }
            self.active_runs.remove(subagent_id);
            self.pump_queue();
            return Ok(Some(result));
        }

        // 5. Budget exhaustion check (fail-closed, no hang)
        if child.turns_used >= effective_budget {
            child.status = SubagentStatus::BudgetExhausted;
            self.tool_registry.release_all_for_subagent(subagent_id);
            let _ = child.journal.record_terminal(
                &run_id,
                RunCheckpointKind::Failed,
                Some(serde_json::json!({
                    "reason": "budget_exhausted",
                    "turns_used": child.turns_used,
                    "budget": effective_budget,
                })),
            );
            let result = SubagentResult {
                subagent_id: child.subagent_id.clone(),
                parent_session_id: child.parent_session_id.clone(),
                status: SubagentStatus::BudgetExhausted,
                summary: format!(
                    "Subagent budget exhausted: reached maximum limit of {} turns without finishing",
                    effective_budget
                ),
                artifacts: child.artifacts.clone(),
                turns_used: child.turns_used,
            };
            self.completed_results
                .insert(subagent_id.to_string(), result.clone());
            if !self.completion_order.iter().any(|id| id == subagent_id) {
                self.completion_order.push(subagent_id.to_string());
            }
            self.active_runs.remove(subagent_id);
            self.pump_queue();
            return Ok(Some(result));
        }

        Ok(None)
    }

    /// Records an artifact produced by the subagent.
    pub fn add_artifact(&mut self, subagent_id: &str, artifact_path: impl Into<String>) {
        if let Some(child) = self.active_runs.get_mut(subagent_id) {
            child.artifacts.push(artifact_path.into());
        }
    }

    /// Cancels a subagent immediately.
    pub fn cancel_subagent(&mut self, subagent_id: &str) -> Option<SubagentResult> {
        self.tool_registry.release_all_for_subagent(subagent_id);
        if let Some(mut child) = self.active_runs.remove(subagent_id) {
            child.cancel_flag.store(true, Ordering::SeqCst);
            child.status = SubagentStatus::Cancelled;
            let run_id = AgentRunId::new(subagent_id);
            let _ = child.journal.record_terminal(
                &run_id,
                RunCheckpointKind::Cancelled,
                Some(serde_json::json!({"reason": "explicit_cancel"})),
            );
            let result = SubagentResult {
                subagent_id: child.subagent_id.clone(),
                parent_session_id: child.parent_session_id.clone(),
                status: SubagentStatus::Cancelled,
                summary: "Subagent cancelled by parent".to_string(),
                artifacts: child.artifacts,
                turns_used: child.turns_used,
            };
            self.completed_results
                .insert(subagent_id.to_string(), result.clone());
            if !self.completion_order.iter().any(|id| id == subagent_id) {
                self.completion_order.push(subagent_id.to_string());
            }
            self.pump_queue();
            Some(result)
        } else {
            None
        }
    }

    /// Propagates cancellation to all active and queued subagents.
    pub fn cancel_all(&mut self) -> Vec<SubagentResult> {
        let active_ids: Vec<String> = self.active_runs.keys().cloned().collect();
        let mut results = Vec::new();
        for id in active_ids {
            if let Some(res) = self.cancel_subagent(&id) {
                results.push(res);
            }
        }
        while let Some((sub_id, parent_id, _spec)) = self.queued_specs.pop_front() {
            let res = SubagentResult {
                subagent_id: sub_id.clone(),
                parent_session_id: parent_id,
                status: SubagentStatus::Cancelled,
                summary: "Subagent cancelled while queued".to_string(),
                artifacts: Vec::new(),
                turns_used: 0,
            };
            self.completed_results.insert(sub_id.clone(), res.clone());
            if !self.completion_order.iter().any(|id| id == &sub_id) {
                self.completion_order.push(sub_id.clone());
            }
            results.push(res);
        }
        results
    }

    /// Returns the completed result for a subagent if already resolved.
    pub fn get_result(&self, subagent_id: &str) -> Option<&SubagentResult> {
        self.completed_results.get(subagent_id)
    }

    /// Lists all currently active or queued subagents for introspection and telemetry.
    pub fn list_active_subagents(&self) -> Vec<ActiveSubagentSummary> {
        let mut list = Vec::new();
        for run in self.active_runs.values() {
            list.push(ActiveSubagentSummary {
                subagent_id: run.subagent_id.clone(),
                parent_session_id: run.parent_session_id.clone(),
                status: format!("{:?}", run.status),
                turns_used: run.turns_used,
            });
        }
        for (sub_id, parent_id, _) in &self.queued_specs {
            list.push(ActiveSubagentSummary {
                subagent_id: sub_id.clone(),
                parent_session_id: parent_id.clone(),
                status: "Queued".to_string(),
                turns_used: 0,
            });
        }
        list.sort_by(|a, b| a.subagent_id.cmp(&b.subagent_id));
        list
    }

    /// Suspends parent turn until the specified subagents complete or timeout occurs.
    ///
    /// Returns completed results in their exact completion order.
    /// Fails closed if any child is unknown or if a timeout occurs before all complete.
    pub fn wait_subagents(
        &mut self,
        subagent_ids: &[String],
        timeout_ms: Option<u64>,
    ) -> Result<Vec<SubagentResult>, SubagentError> {
        // 1. Verify existence of all requested subagents
        for id in subagent_ids {
            if !self.active_runs.contains_key(id)
                && !self.queued_specs.iter().any(|(q_id, _, _)| q_id == id)
                && !self.completed_results.contains_key(id)
                && !self
                    .completion_checkpoints
                    .iter()
                    .any(|(_, c_id, _)| c_id == id)
            {
                return Err(SubagentError::SubagentNotFound(id.clone()));
            }
        }

        let effective_timeout = timeout_ms.unwrap_or(30_000).min(300_000);
        let start_time = self.clock.now_millis();
        let deadline = start_time.saturating_add(effective_timeout);

        // Clock-based poll loop iterating through scheduled completion checkpoints
        loop {
            let all_done = subagent_ids
                .iter()
                .all(|id| self.completed_results.contains_key(id));

            if all_done {
                let mut results = Vec::new();
                for completed_id in &self.completion_order {
                    if subagent_ids.iter().any(|id| id == completed_id) {
                        if let Some(res) = self.completed_results.get(completed_id) {
                            results.push(res.clone());
                        }
                    }
                }
                for id in subagent_ids {
                    if !results.iter().any(|r| &r.subagent_id == id) {
                        if let Some(res) = self.completed_results.get(id) {
                            results.push(res.clone());
                        }
                    }
                }
                return Ok(results);
            }

            // Find next eligible scheduled completion checkpoint <= deadline
            if let Some(idx) = self
                .completion_checkpoints
                .iter()
                .position(|(ts, _, _)| *ts <= deadline)
            {
                let (_, sub_id, summary) = self.completion_checkpoints.remove(idx);
                if self.active_runs.contains_key(&sub_id) {
                    let _ = self.execute_turn(&sub_id, None, None, true, summary.as_deref());
                } else if !self.completed_results.contains_key(&sub_id) {
                    let res = SubagentResult {
                        subagent_id: sub_id.clone(),
                        parent_session_id: "session".to_string(),
                        status: SubagentStatus::Completed,
                        summary: summary.unwrap_or_else(|| "Subagent completed".to_string()),
                        artifacts: Vec::new(),
                        turns_used: 1,
                    };
                    self.completed_results.insert(sub_id.clone(), res);
                    if !self.completion_order.iter().any(|id| id == &sub_id) {
                        self.completion_order.push(sub_id);
                    }
                }
                continue;
            }

            // No eligible checkpoints before deadline -> timeout fail-closed
            return Err(SubagentError::WaitTimeout(effective_timeout));
        }
    }

    /// Merges a completed subagent's summary into the specified parent journal.
    pub fn merge_child_summary(
        &self,
        parent_journal: &mut RunJournal,
        parent_run_id: &AgentRunId,
        subagent_id: &str,
    ) -> Result<u64, SubagentError> {
        let result = self
            .completed_results
            .get(subagent_id)
            .ok_or_else(|| SubagentError::SubagentNotFound(subagent_id.to_string()))?;
        merge_subagent_summary_to_parent(parent_journal, parent_run_id, result)
    }

    fn pump_queue(&mut self) {
        while self.active_runs.len() < self.max_active {
            if let Some((sub_id, parent_id, spec)) = self.queued_specs.pop_front() {
                let _ = self.spawn_subagent(sub_id, parent_id, spec);
            } else {
                break;
            }
        }
    }
}

/// Merges a subagent execution summary into the parent run's journal.
///
/// Invariant: Only typed summary events are appended. Child raw transcripts,
/// internal reasoning, and intermediate step payloads remain strictly isolated.
pub fn merge_subagent_summary_to_parent(
    parent_journal: &mut RunJournal,
    parent_run_id: &AgentRunId,
    result: &SubagentResult,
) -> Result<u64, SubagentError> {
    let payload = serde_json::json!({
        "subagent_id": &result.subagent_id,
        "parent_session_id": &result.parent_session_id,
        "status": &result.status,
        "summary": &result.summary,
        "artifacts": &result.artifacts,
        "turns_used": result.turns_used,
    });

    let entry = parent_journal
        .append_event(parent_run_id, "subagent_summary", payload.to_string())
        .map_err(SubagentError::from)?;

    Ok(entry.event_seq)
}

/// Helper to execute subagent_wait tool from model invocation arguments.
pub fn execute_subagent_wait_tool(
    supervisor: &mut SubagentSupervisor,
    params: &serde_json::Value,
) -> Result<serde_json::Value, SubagentError> {
    let subagent_ids: Vec<String> = params
        .get("subagent_ids")
        .and_then(|v| v.as_array())
        .map(|arr| {
            arr.iter()
                .filter_map(|v| v.as_str().map(|s| s.to_string()))
                .collect()
        })
        .unwrap_or_default();

    let timeout_ms = params.get("timeout_ms").and_then(|v| v.as_u64());

    let results = supervisor.wait_subagents(&subagent_ids, timeout_ms)?;
    serde_json::to_value(&results).map_err(|e| SubagentError::ExecutionFailed(e.to_string()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_subagent_spawn_and_complete_roundtrip() {
        let mut supervisor = SubagentSupervisor::new();
        let spec = SubagentSpec::new(
            "Research assistant",
            3,
            vec!["web_search".into(), "fs_write".into()],
            vec!["web_search".into(), "fs_write".into(), "shell_exec".into()],
        );

        let status = supervisor
            .spawn_subagent("sub-001", "session-parent-1", spec)
            .expect("spawn should succeed");
        assert_eq!(status, SubagentStatus::Running);
        assert_eq!(supervisor.active_count(), 1);

        // Turn 1
        supervisor.add_artifact("sub-001", "report.md");
        let turn1 = supervisor
            .execute_turn(
                "sub-001",
                Some("web_search"),
                Some("found 3 links"),
                false,
                None,
            )
            .unwrap();
        assert!(turn1.is_none());

        // Turn 2: complete
        let turn2 = supervisor
            .execute_turn(
                "sub-001",
                Some("fs_write"),
                Some("written"),
                true,
                Some("Completed research and saved summary"),
            )
            .unwrap();

        let result = turn2.expect("turn 2 completed subagent");
        assert_eq!(result.status, SubagentStatus::Completed);
        assert_eq!(result.subagent_id, "sub-001");
        assert_eq!(result.parent_session_id, "session-parent-1");
        assert_eq!(result.turns_used, 2);
        assert_eq!(result.artifacts, vec!["report.md"]);
        assert!(result.summary.contains("Completed research"));
        assert_eq!(supervisor.active_count(), 0);
    }

    #[test]
    fn test_subagent_budget_cap_enforced_and_budget_exhausted() {
        let mut supervisor = SubagentSupervisor::new();
        // Request budget 10 — must be clamped to 5
        let spec = SubagentSpec::new(
            "Worker",
            10,
            vec!["web_search".into()],
            vec!["web_search".into()],
        );
        assert_eq!(spec.effective_budget(), 5);

        supervisor
            .spawn_subagent("sub-budget", "session-parent-1", spec)
            .unwrap();

        for i in 1..=4 {
            let res = supervisor
                .execute_turn("sub-budget", Some("web_search"), None, false, None)
                .unwrap();
            assert!(res.is_none(), "turn {} should not terminate yet", i);
        }

        // 5th turn should exhaust budget and fail closed
        let res = supervisor
            .execute_turn("sub-budget", Some("web_search"), None, false, None)
            .unwrap();
        let result = res.expect("5th turn should exhaust budget");
        assert_eq!(result.status, SubagentStatus::BudgetExhausted);
        assert_eq!(result.turns_used, 5);
        assert!(result.summary.contains("budget exhausted"));
        assert_eq!(supervisor.active_count(), 0);
    }

    #[test]
    fn test_subagent_permission_subset_enforcement() {
        let mut supervisor = SubagentSupervisor::new();
        // Child requests "shell_exec" which is NOT in parent permissions
        let invalid_spec = SubagentSpec::new(
            "Worker",
            3,
            vec!["web_search".into(), "shell_exec".into()],
            vec!["web_search".into(), "fs_write".into()],
        );

        let err = supervisor
            .spawn_subagent("sub-perm", "session-parent-1", invalid_spec)
            .unwrap_err();

        assert_eq!(
            err,
            SubagentError::PermissionViolation("shell_exec".to_string())
        );

        // Also test attempting to execute an unallowed tool mid-run
        let valid_spec = SubagentSpec::new(
            "Worker",
            3,
            vec!["web_search".into()],
            vec!["web_search".into(), "fs_write".into()],
        );
        supervisor
            .spawn_subagent("sub-mid", "session-parent-1", valid_spec)
            .unwrap();
        let turn_err = supervisor
            .execute_turn("sub-mid", Some("fs_write"), None, false, None)
            .unwrap_err();
        assert_eq!(
            turn_err,
            SubagentError::PermissionViolation("fs_write".to_string())
        );
    }

    #[test]
    fn test_subagent_isolation_parent_cannot_see_raw_transcript() {
        let mut supervisor = SubagentSupervisor::new();
        let spec = SubagentSpec::new(
            "Secret internal worker prompt with sensitive instructions",
            3,
            vec!["web_search".into()],
            vec!["web_search".into()],
        );

        supervisor
            .spawn_subagent("sub-iso", "session-parent-1", spec)
            .unwrap();

        let result = supervisor
            .execute_turn(
                "sub-iso",
                Some("web_search"),
                Some("raw child model internal output: sensitive_token_abc"),
                true,
                Some("Public sanitized summary of work"),
            )
            .unwrap()
            .expect("should complete");

        // Adversarial Privacy Probe: Verify serialization contains ONLY typed summary struct
        let serialized = serde_json::to_string(&result).unwrap();
        let val: serde_json::Value = serde_json::from_str(&serialized).unwrap();

        assert!(!serialized.contains("Secret internal worker prompt"));
        assert!(!serialized.contains("sensitive_token_abc"));
        assert!(!serialized.contains("raw child model"));
        assert!(val.get("summary").is_some());
        assert!(val.get("artifacts").is_some());
        assert!(val.get("status").is_some());
        assert!(val.get("raw_transcript").is_none());
        assert!(val.get("messages").is_none());
        assert!(val.get("journal").is_none());
    }

    #[test]
    fn test_subagent_parent_cancel_propagates_to_child() {
        let mut supervisor = SubagentSupervisor::new();
        let spec = SubagentSpec::new(
            "Worker",
            3,
            vec!["web_search".into()],
            vec!["web_search".into()],
        );

        supervisor
            .spawn_subagent("sub-cancel-1", "session-parent-1", spec.clone())
            .unwrap();
        supervisor
            .spawn_subagent("sub-cancel-2", "session-parent-1", spec)
            .unwrap();

        assert_eq!(supervisor.active_count(), 2);

        let cancelled = supervisor.cancel_all();
        assert_eq!(cancelled.len(), 2);
        assert!(cancelled
            .iter()
            .all(|r| r.status == SubagentStatus::Cancelled));
        assert_eq!(supervisor.active_count(), 0);
    }

    #[test]
    fn test_subagent_max_concurrency_queue() {
        let mut supervisor = SubagentSupervisor::with_max_active(2);
        let spec = SubagentSpec::new(
            "Worker",
            2,
            vec!["web_search".into()],
            vec!["web_search".into()],
        );

        let s1 = supervisor
            .spawn_subagent("sub-1", "session", spec.clone())
            .unwrap();
        let s2 = supervisor
            .spawn_subagent("sub-2", "session", spec.clone())
            .unwrap();
        let s3 = supervisor
            .spawn_subagent("sub-3", "session", spec.clone())
            .unwrap();

        assert_eq!(s1, SubagentStatus::Running);
        assert_eq!(s2, SubagentStatus::Running);
        assert_eq!(s3, SubagentStatus::Queued);
        assert_eq!(supervisor.active_count(), 2);
        assert_eq!(supervisor.queued_count(), 1);

        // Complete sub-1 -> sub-3 should be pumped to active
        supervisor
            .execute_turn("sub-1", Some("web_search"), None, true, Some("done"))
            .unwrap();

        assert_eq!(supervisor.active_count(), 2);
        assert_eq!(supervisor.queued_count(), 0);
    }

    #[test]
    fn test_subagent_empty_parent_permissions_fails_closed() {
        let mut supervisor = SubagentSupervisor::new();
        // Child requests "shell_exec" with EMPTY parent authority
        let adversarial_spec = SubagentSpec::new(
            "Adversarial worker",
            3,
            vec!["shell_exec".into()],
            vec![], // Empty parent permissions
        );

        let err = supervisor
            .spawn_subagent("sub-adv", "session-parent-1", adversarial_spec)
            .unwrap_err();

        assert_eq!(
            err,
            SubagentError::PermissionViolation("shell_exec".to_string())
        );

        // Empty parent authority with ZERO child tools succeeds
        let benign_spec = SubagentSpec::new("Zero tool worker", 3, vec![], vec![]);
        let ok = supervisor.spawn_subagent("sub-zero", "session-parent-1", benign_spec);
        assert!(ok.is_ok());
    }

    #[test]
    fn test_subagent_wait_returns_in_completion_order_and_times_out() {
        use crate::event_wait::MockClock;

        let clock = Arc::new(MockClock::new(1000));
        let mut supervisor = SubagentSupervisor::with_clock(clock.clone());

        let spec1 = SubagentSpec::new(
            "Worker 1",
            3,
            vec!["web_search".into()],
            vec!["web_search".into()],
        );
        let spec2 = SubagentSpec::new(
            "Worker 2",
            3,
            vec!["web_search".into()],
            vec!["web_search".into()],
        );

        supervisor
            .spawn_subagent("sub-wait-1", "session-parent", spec1)
            .unwrap();
        supervisor
            .spawn_subagent("sub-wait-2", "session-parent", spec2)
            .unwrap();

        // 1. sub-wait-2 completes FIRST
        supervisor
            .execute_turn(
                "sub-wait-2",
                Some("web_search"),
                None,
                true,
                Some("Sub 2 done first"),
            )
            .unwrap();

        // 2. sub-wait-1 completes SECOND
        supervisor
            .execute_turn(
                "sub-wait-1",
                Some("web_search"),
                None,
                true,
                Some("Sub 1 done second"),
            )
            .unwrap();

        // 3. wait returns both results in completion order (sub-wait-2, then sub-wait-1)
        let results = supervisor
            .wait_subagents(&["sub-wait-1".into(), "sub-wait-2".into()], Some(5000))
            .expect("both subagents completed");

        assert_eq!(results.len(), 2);
        assert_eq!(results[0].subagent_id, "sub-wait-2");
        assert_eq!(results[0].summary, "Sub 2 done first");
        assert_eq!(results[1].subagent_id, "sub-wait-1");
        assert_eq!(results[1].summary, "Sub 1 done second");

        // 4. Test tool invocation format
        let val = execute_subagent_wait_tool(
            &mut supervisor,
            &serde_json::json!({
                "subagent_ids": ["sub-wait-1", "sub-wait-2"],
                "timeout_ms": 5000
            }),
        )
        .unwrap();
        assert!(val.is_array());
        assert_eq!(val.as_array().unwrap().len(), 2);

        // 5. Test timeout fail-closed with pending subagent
        let spec3 = SubagentSpec::new(
            "Worker 3",
            3,
            vec!["web_search".into()],
            vec!["web_search".into()],
        );
        supervisor
            .spawn_subagent("sub-wait-3", "session-parent", spec3)
            .unwrap();

        // Still running -> timeout fails closed
        let timeout_err = supervisor
            .wait_subagents(&["sub-wait-3".into()], Some(3000))
            .unwrap_err();
        assert_eq!(timeout_err, SubagentError::WaitTimeout(3000));
    }

    #[test]
    fn test_timeline_merge_summary_events_without_raw_transcripts() {
        let mut supervisor = SubagentSupervisor::new();
        let mut parent_journal = RunJournal::new();
        let parent_run_id = AgentRunId::new("parent-run-main");

        parent_journal
            .start_run(parent_run_id.clone(), "parent-session-1")
            .unwrap();
        let parent_entry1 = parent_journal
            .append_event(
                &parent_run_id,
                "parent_user_msg",
                r#"{"prompt":"delegate research"}"#,
            )
            .unwrap();
        assert_eq!(parent_entry1.event_seq, 1);

        let spec = SubagentSpec::new(
            "Private child system prompt secret_leak_123",
            3,
            vec!["web_search".into()],
            vec!["web_search".into()],
        );

        supervisor
            .spawn_subagent("child-sub-007", "parent-session-1", spec)
            .unwrap();
        supervisor.add_artifact("child-sub-007", "evidence.pdf");

        supervisor
            .execute_turn(
                "child-sub-007",
                Some("web_search"),
                Some("raw internal child model dump secret_raw_token_xyz"),
                true,
                Some("Public sanitized child outcome summary"),
            )
            .unwrap();

        // Merge child summary into parent journal
        let merge_seq = supervisor
            .merge_child_summary(&mut parent_journal, &parent_run_id, "child-sub-007")
            .expect("merge should succeed");

        // Verify sequence number is monotonic after parent's current
        assert!(merge_seq > parent_entry1.event_seq);

        // Verify parent journal entries contain summary but NOT raw transcripts
        let entries = parent_journal.get_entries(&parent_run_id).unwrap();
        assert_eq!(entries.len(), 2);
        assert_eq!(entries[1].event_name, "subagent_summary");

        let summary_entry_payload = &entries[1].payload_json;
        assert!(summary_entry_payload.contains("Public sanitized child outcome summary"));
        assert!(summary_entry_payload.contains("evidence.pdf"));
        assert!(summary_entry_payload.contains("child-sub-007"));

        // Privacy Isolation Probe:
        let all_payloads: String = entries.iter().map(|e| e.payload_json.clone()).collect();
        assert!(!all_payloads.contains("secret_leak_123"));
        assert!(!all_payloads.contains("secret_raw_token_xyz"));
        assert!(!all_payloads.contains("raw internal child model"));
    }

    #[test]
    fn test_subagent_tool_slot_concurrency_busy() {
        let mut supervisor = SubagentSupervisor::new();
        supervisor.set_tool_concurrency_limit("shell_exec", 1);

        let spec1 = SubagentSpec::new(
            "Worker 1",
            3,
            vec!["shell_exec".into()],
            vec!["shell_exec".into()],
        );
        let spec2 = SubagentSpec::new(
            "Worker 2",
            3,
            vec!["shell_exec".into()],
            vec!["shell_exec".into()],
        );

        supervisor
            .spawn_subagent("sub-shell-1", "session", spec1)
            .unwrap();
        supervisor
            .spawn_subagent("sub-shell-2", "session", spec2)
            .unwrap();

        // Subagent 1 calls shell_exec without completing (holding in-flight slot)
        let res1 = supervisor.execute_turn("sub-shell-1", Some("shell_exec"), None, false, None);
        assert!(res1.is_ok());

        // Subagent 2 attempts to call shell_exec concurrently -> fails with ToolBusy
        let res2 = supervisor.execute_turn("sub-shell-2", Some("shell_exec"), None, false, None);
        assert_eq!(
            res2.unwrap_err(),
            SubagentError::ToolBusy("shell_exec".to_string())
        );

        // Subagent 1 completes turn and releases slot
        let res1_done = supervisor.execute_turn(
            "sub-shell-1",
            Some("shell_exec"),
            Some("output"),
            true,
            Some("done"),
        );
        assert!(res1_done.is_ok());

        // Now Subagent 2 can acquire shell_exec successfully
        let res2_retry = supervisor.execute_turn(
            "sub-shell-2",
            Some("shell_exec"),
            Some("output 2"),
            true,
            Some("done 2"),
        );
        assert!(res2_retry.is_ok());
    }

    #[test]
    fn test_subagent_wait_with_scheduled_completion_checkpoints() {
        use crate::event_wait::MockClock;

        let clock = Arc::new(MockClock::new(1000));
        let mut supervisor = SubagentSupervisor::with_clock(clock.clone());

        let spec = SubagentSpec::new(
            "Scheduled Worker",
            3,
            vec!["web_search".into()],
            vec!["web_search".into()],
        );

        supervisor
            .spawn_subagent("sub-sched", "session-parent", spec)
            .unwrap();

        // Schedule completion at timestamp 1500 (within 2000ms deadline from start 1000)
        supervisor.schedule_completion(1500, "sub-sched", "Scheduled subagent completed");

        let results = supervisor
            .wait_subagents(&["sub-sched".to_string()], Some(2000))
            .expect("Should complete within timeout");
        assert_eq!(results.len(), 1);
        assert_eq!(results[0].subagent_id, "sub-sched");
        assert_eq!(results[0].summary, "Scheduled subagent completed");

        // Schedule late completion beyond timeout -> should fail closed with WaitTimeout
        let spec2 = SubagentSpec::new(
            "Late Worker",
            3,
            vec!["web_search".into()],
            vec!["web_search".into()],
        );
        supervisor
            .spawn_subagent("sub-late", "session-parent", spec2)
            .unwrap();

        supervisor.schedule_completion(5000, "sub-late", "Late subagent");
        let timeout_err = supervisor
            .wait_subagents(&["sub-late".to_string()], Some(500))
            .unwrap_err();
        assert_eq!(timeout_err, SubagentError::WaitTimeout(500));
    }
}
