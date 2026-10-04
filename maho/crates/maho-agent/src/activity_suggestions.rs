// Copyright 2026 Maho Browser. All rights reserved.

//! Routine suggestion and activity pattern candidate contracts.
//!
//! Converts observed user interaction patterns into structured routine
//! candidates for user confirmation and routine creation.

use serde::{Deserialize, Serialize};

/// Source triggering an activity suggestion.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SuggestionSource {
    MahoControlActivityService,
    TabWorkflowPattern,
    RepeatedPrompt,
}

/// Candidate browser routine proposed from observed activity.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct RoutineCandidate {
    pub id: String,
    pub suggested_name: String,
    pub description: String,
    pub trigger_pattern: String,
    pub action_sequence: Vec<serde_json::Value>,
    pub confidence_score: f32,
    pub source: SuggestionSource,
    pub created_at: u64,
}

/// Current status of a routine suggestion.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SuggestionStatus {
    Proposed,
    Dismissed,
    ConvertedToRoutine,
}

/// Decision recorded for a routine candidate.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SuggestionDecision {
    pub candidate_id: String,
    pub status: SuggestionStatus,
    pub resulting_routine_id: Option<String>,
}
