// Copyright 2026 Maho Browser. All rights reserved.

//! Subagent spawning, lifecycle coordination, and concurrency management contracts.
//!
//! Subagents run as isolated child sessions with permissions bounded as a strict
//! subset of their parent. Maximum 5 concurrent subagents (6th queues).

use serde::{Deserialize, Serialize};

/// Maximum number of concurrently executing subagents per session tree.
pub const MAX_ACTIVE_SUBAGENTS: usize = 5;

/// Execution status of a spawned subagent.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SubagentStatus {
    Queued,
    Running,
    Completed,
    Failed,
    Cancelled,
}

/// Handle tracking a spawned child subagent.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SubagentHandle {
    pub subagent_id: String,
    pub parent_session_id: String,
    pub child_session_id: String,
    pub task_description: String,
    pub status: SubagentStatus,
    pub spawned_at: u64,
}

/// Configuration used to spawn a subagent.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SubagentSpawnConfig {
    pub task_description: String,
    pub allowed_tools: Vec<String>,
    pub max_turns: u32,
    pub timeout_ms: u64,
    /// Invariant: Child permissions must be a strict subset of the parent.
    pub parent_permissions_subset: Vec<String>,
}

/// Result received when joining on or resolving a completed subagent.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SubagentJoinResult {
    pub subagent_id: String,
    pub status: SubagentStatus,
    pub result_summary: String,
    pub artifacts: Vec<String>,
    pub execution_duration_ms: u64,
}
