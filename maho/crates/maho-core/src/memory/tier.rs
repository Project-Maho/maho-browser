//! Memory tier models and definitions (L0 - L3).
//!
//! - **L0 Working Memory:** Turn / session active context and short-lived state.
//! - **L1 Prompt Memory:** Hot / warm / cold pinned blocks budgeted for prompt injection.
//! - **L2 Semantic Memory:** Deduplicated, confidence-scored, vector-indexed facts.
//! - **L3 Episodic Memory:** Raw source ledger (sessions, turns, sources, maintenance jobs).

use serde::{Deserialize, Serialize};

/// Canonical label for L1 hot briefing block injected into agent baseline prompt.
pub const L1_BRIEFING_LABEL: &str = "briefing";

/// Four canonical memory tiers according to the Maho architecture specification.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum MemoryTier {
    /// L0: Turn and session working memory (ephemeral task context).
    L0Working,
    /// L1: Pinned and prompt block memory (user-inspectable preferences/rules).
    L1Prompt,
    /// L2: Deduplicated semantic memory (dense HNSW + sparse FTS facts).
    L2Semantic,
    /// L3: Episodic ledger and provenance evidence (turns, sessions, jobs).
    L3Episodic,
}

/// Prompt block tiering for L1 memory budgeting.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum PromptBlockTier {
    Hot,
    Warm,
    Cold,
}

impl PromptBlockTier {
    pub fn as_str(&self) -> &'static str {
        match self {
            Self::Hot => "hot",
            Self::Warm => "warm",
            Self::Cold => "cold",
        }
    }

    pub fn from_str(s: &str) -> Self {
        match s.to_ascii_lowercase().as_str() {
            "hot" => Self::Hot,
            "warm" => Self::Warm,
            _ => Self::Cold,
        }
    }

    pub fn default_token_budget(&self) -> usize {
        match self {
            Self::Hot => 500,
            Self::Warm => 1500,
            Self::Cold => 3000,
        }
    }
}

/// High-level semantic memory fact (L2 tier).
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct MemoryFact {
    pub id: String,
    pub fact: String,
    pub source: String,
    pub session_id: Option<String>,
    pub categories: Vec<String>,
    pub importance: f32,
    pub metadata: Option<String>,
    pub normalized_hash: Option<String>,
    pub confidence: f32,
    pub last_confirmed_at: Option<chrono::DateTime<chrono::Utc>>,
    pub superseded_by: Option<String>,
    pub embedding_generation: Option<String>,
    pub embedding_model_id: Option<String>,
    pub created_at: chrono::DateTime<chrono::Utc>,
}

/// Provenance link connecting an L2 fact to an L3 episode or source.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct FactSource {
    pub id: String,
    pub fact_id: String,
    pub source_type: String,
    pub source_id: Option<String>,
    pub source_range_or_url: Option<String>,
    pub observed_at: chrono::DateTime<chrono::Utc>,
}

/// Pinned prompt memory block (L1 tier).
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct PromptBlock {
    pub label: String,
    pub content: String,
    pub tier: PromptBlockTier,
    pub token_count: usize,
    pub updated_at: chrono::DateTime<chrono::Utc>,
}
