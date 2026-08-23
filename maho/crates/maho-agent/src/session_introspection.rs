// Copyright 2026 Maho Browser. All rights reserved.

//! Sanitized session introspection and transcript export contracts.
//!
//! Invariant: Provider-private reasoning, raw credentials, and sensitive tokens
//! are strictly excluded from public receipts and introspection transcripts.

use serde::{Deserialize, Serialize};

/// A single sanitized turn within a public session transcript.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct SanitizedTranscriptTurn {
    pub turn_id: String,
    pub role: String,
    pub content: String,
    pub sanitized_tool_calls: Vec<serde_json::Value>,
    pub sanitized_tool_results: Vec<serde_json::Value>,
    pub timestamp: u64,
}

/// Sanitized transcript safe for public display or export.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct SanitizedTranscript {
    pub session_id: String,
    pub run_id: Option<String>,
    pub turns: Vec<SanitizedTranscriptTurn>,
    pub redaction_applied: bool,
    pub exported_at: u64,
}

/// Public completion receipt summarizing execution for auditing.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct IntrospectionReceipt {
    pub session_id: String,
    pub run_id: String,
    pub completed_steps: u32,
    pub terminal_status: String,
    pub proof_screenshot_locator: Option<String>,
    pub has_artifacts: bool,
    pub finished_at: u64,
}
