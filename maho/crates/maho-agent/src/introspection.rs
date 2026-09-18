// Copyright 2026 Maho Browser. All rights reserved.

//! Agent session introspection and execution telemetry contracts.
//!
//! Exposes read-only introspection of active runs, turn control queue depth,
//! model routing decisions, and enabled capability counts without exposing
//! raw credentials, tokens, or provider-private reasoning.

use crate::model_routing::RoutingDecision;
use crate::run_journal::RunJournal;
use crate::subagents::{ActiveSubagentSummary, SubagentSupervisor};
use crate::turn_control::TurnControlQueue;
use serde::{Deserialize, Serialize};

/// Summary of an active or recent run tracked by the journal.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ActiveRunSummary {
    pub run_id: String,
    pub session_id: String,
    pub status: String,
    pub is_terminal: bool,
    pub event_count: usize,
}

/// Telemetry summary of the most recent model routing decision.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ModelRoutingSummary {
    pub category: String,
    pub selected_model: String,
    pub reason: String,
    pub is_fallback: bool,
}

impl From<&RoutingDecision> for ModelRoutingSummary {
    fn from(d: &RoutingDecision) -> Self {
        Self {
            category: format!("{:?}", d.category),
            selected_model: d.selected_model.clone(),
            reason: d.reason.clone(),
            is_fallback: d.is_fallback,
        }
    }
}

/// Read-only snapshot of the agent execution state.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SessionIntrospectionReport {
    pub active_runs: Vec<ActiveRunSummary>,
    #[serde(default)]
    pub active_subagents: Vec<ActiveSubagentSummary>,
    pub queue_depth: usize,
    pub model_routing: Option<ModelRoutingSummary>,
    pub enabled_capabilities_count: usize,
}

/// Collects an introspection report from runtime components.
pub fn collect_introspection(
    journal: &RunJournal,
    queue: Option<&TurnControlQueue>,
    last_routing: Option<&RoutingDecision>,
    supervisor: Option<&SubagentSupervisor>,
    capabilities_count: usize,
) -> SessionIntrospectionReport {
    let mut active_runs = Vec::new();

    for record in journal.list_runs() {
        let is_term = record.terminal_kind.is_some();
        let status = if let Some(term) = record.terminal_kind {
            format!("{:?}", term)
        } else {
            "Running".to_string()
        };

        active_runs.push(ActiveRunSummary {
            run_id: record.run_id.0.clone(),
            session_id: record.session_id.clone(),
            status,
            is_terminal: is_term,
            event_count: record.entries.len(),
        });
    }

    // Deterministic order by run_id
    active_runs.sort_by(|a, b| a.run_id.cmp(&b.run_id));

    let active_subagents = supervisor
        .map(|s| s.list_active_subagents())
        .unwrap_or_default();

    let queue_depth = queue.map(|q| q.items.len()).unwrap_or(0);
    let model_routing = last_routing.map(ModelRoutingSummary::from);

    SessionIntrospectionReport {
        active_runs,
        active_subagents,
        queue_depth,
        model_routing,
        enabled_capabilities_count: capabilities_count,
    }
}

/// Helper to execute introspection and return sanitized JSON value.
pub fn execute_introspection_tool(
    journal: &RunJournal,
    queue: Option<&TurnControlQueue>,
    last_routing: Option<&RoutingDecision>,
    supervisor: Option<&SubagentSupervisor>,
    capabilities_count: usize,
) -> serde_json::Value {
    let report =
        collect_introspection(journal, queue, last_routing, supervisor, capabilities_count);
    serde_json::to_value(&report)
        .unwrap_or_else(|_| serde_json::json!({ "error": "introspection_failed" }))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model_routing::TaskCategory;
    use crate::run_journal::{AgentRunId, RunCheckpointKind};
    use crate::turn_control::{FollowUpBehavior, QueuedFollowUp};
    use std::collections::VecDeque;

    #[test]
    fn test_introspection_output_matches_state() {
        let mut journal = RunJournal::new();
        let run1 = AgentRunId::new("run-001");
        let run2 = AgentRunId::new("run-002");

        journal.start_run(run1.clone(), "session-a").unwrap();
        journal
            .append_event(&run1, "step_1", r#"{"op":"start"}"#)
            .unwrap();

        journal.start_run(run2.clone(), "session-b").unwrap();
        journal
            .append_event(&run2, "step_1", r#"{"op":"start"}"#)
            .unwrap();
        journal
            .record_terminal(&run2, RunCheckpointKind::Completed, None)
            .unwrap();

        let mut queue_items = VecDeque::new();
        queue_items.push_back(QueuedFollowUp {
            id: "msg-1".to_string(),
            session_id: "session-a".to_string(),
            message: "follow up".to_string(),
            behavior: FollowUpBehavior::Queue,
            queued_at: 100,
        });

        let queue = TurnControlQueue {
            session_id: "session-a".to_string(),
            max_queue_size: 5,
            items: queue_items,
        };

        let routing = RoutingDecision {
            category: TaskCategory::Coding,
            model: "anthropic/claude-3-7-sonnet".to_string(),
            selected_model: "anthropic/claude-3-7-sonnet".to_string(),
            fallback_model: None,
            reason: "Preferred coding model selected".to_string(),
            confidence: 0.95,
            is_fallback: false,
        };

        let report = collect_introspection(&journal, Some(&queue), Some(&routing), None, 8);

        assert_eq!(report.active_runs.len(), 2);
        assert_eq!(report.active_runs[0].run_id, "run-001");
        assert_eq!(report.active_runs[0].status, "Running");
        assert!(!report.active_runs[0].is_terminal);

        assert_eq!(report.active_runs[1].run_id, "run-002");
        assert_eq!(report.active_runs[1].status, "Completed");
        assert!(report.active_runs[1].is_terminal);

        assert_eq!(report.queue_depth, 1);
        assert_eq!(report.enabled_capabilities_count, 8);

        let routing_summary = report.model_routing.expect("routing summary present");
        assert_eq!(
            routing_summary.selected_model,
            "anthropic/claude-3-7-sonnet"
        );
        assert_eq!(routing_summary.reason, "Preferred coding model selected");
        assert!(!routing_summary.is_fallback);
    }

    #[test]
    fn test_introspection_secrets_absent_probe() {
        let mut journal = RunJournal::new();
        let run_id = AgentRunId::new("run-sec");
        journal.start_run(run_id.clone(), "session-sec").unwrap();
        // Even if some internal event payload had sensitive keys:
        journal
            .append_event(&run_id, "auth_attempt", r#"{"api_key":"sk-secret-12345"}"#)
            .unwrap();

        let routing = RoutingDecision {
            category: TaskCategory::GeneralChat,
            model: "google/gemini-3-flash-lite:free".to_string(),
            selected_model: "google/gemini-3-flash-lite:free".to_string(),
            fallback_model: None,
            reason: "Standard general chat default".to_string(),
            confidence: 1.0,
            is_fallback: false,
        };

        let report = collect_introspection(&journal, None, Some(&routing), None, 5);
        let serialized = serde_json::to_string(&report).unwrap();

        // Check for sensitive credential/token words in serialized JSON
        let forbidden = [
            "sk-secret",
            "api_key",
            "bearer",
            "authorization",
            "password",
            "private_key",
            "credential",
            "vault_secret",
        ];

        for keyword in forbidden {
            assert!(
                !serialized.to_lowercase().contains(keyword),
                "Introspection output must NOT leak sensitive keyword: {}",
                keyword
            );
        }
    }

    #[test]
    fn test_introspection_empty_and_terminal_states() {
        let empty_journal = RunJournal::new();
        let report = collect_introspection(&empty_journal, None, None, None, 0);

        assert!(report.active_runs.is_empty());
        assert!(report.active_subagents.is_empty());
        assert_eq!(report.queue_depth, 0);
        assert!(report.model_routing.is_none());
        assert_eq!(report.enabled_capabilities_count, 0);
    }

    #[test]
    fn test_introspection_enrichment_includes_active_subagents_and_secrets_absent() {
        use crate::subagents::SubagentSpec;

        let mut journal = RunJournal::new();
        let run_id = AgentRunId::new("parent-run");
        journal.start_run(run_id.clone(), "session-p").unwrap();

        let mut supervisor = SubagentSupervisor::with_max_active(1);

        // Active running subagent
        let spec1 = SubagentSpec::new(
            "Secret internal subagent prompt sk-agent-privkey-12345",
            3,
            vec!["web_search".into()],
            vec!["web_search".into()],
        );
        supervisor
            .spawn_subagent("sub-active-1", "session-p", spec1)
            .unwrap();

        // Queued subagent due to max_active = 1
        let spec2 = SubagentSpec::new(
            "Queued subagent prompt secret-bearer-token",
            3,
            vec!["web_search".into()],
            vec!["web_search".into()],
        );
        supervisor
            .spawn_subagent("sub-queued-2", "session-p", spec2)
            .unwrap();

        let report = collect_introspection(&journal, None, None, Some(&supervisor), 4);

        // Verify active subagents present
        assert_eq!(report.active_subagents.len(), 2);
        assert_eq!(report.active_subagents[0].subagent_id, "sub-active-1");
        assert_eq!(report.active_subagents[0].status, "Running");
        assert_eq!(report.active_subagents[0].parent_session_id, "session-p");

        assert_eq!(report.active_subagents[1].subagent_id, "sub-queued-2");
        assert_eq!(report.active_subagents[1].status, "Queued");
        assert_eq!(report.active_subagents[1].parent_session_id, "session-p");

        // Secrets Absent Probe: Ensure subagent internal prompts/secrets are NOT in introspection JSON
        let serialized = serde_json::to_string(&report).unwrap();
        assert!(!serialized.contains("sk-agent-privkey"));
        assert!(!serialized.contains("secret-bearer-token"));
        assert!(!serialized.contains("Secret internal subagent"));
    }
}
