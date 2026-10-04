//! Background memory consolidation, dreaming jobs, and contradiction resolution.

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

/// Canonical maintenance job types for memory lifecycle management.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum MemoryJobKind {
    ExtractEpisodeFacts,
    EmbedFact,
    ConsolidateFactCluster,
    ResolveSupersession,
    RefreshPromptBlock,
    SummarizeBrowseWindow,
    RebuildEmbeddingGeneration,
    CompactMemoryIndex,
    PruneLowValueMemory,
}

impl MemoryJobKind {
    pub fn as_str(&self) -> &'static str {
        match self {
            Self::ExtractEpisodeFacts => "ExtractEpisodeFacts",
            Self::EmbedFact => "EmbedFact",
            Self::ConsolidateFactCluster => "ConsolidateFactCluster",
            Self::ResolveSupersession => "ResolveSupersession",
            Self::RefreshPromptBlock => "RefreshPromptBlock",
            Self::SummarizeBrowseWindow => "SummarizeBrowseWindow",
            Self::RebuildEmbeddingGeneration => "RebuildEmbeddingGeneration",
            Self::CompactMemoryIndex => "CompactMemoryIndex",
            Self::PruneLowValueMemory => "PruneLowValueMemory",
        }
    }

    pub fn from_str(s: &str) -> Option<Self> {
        match s {
            "ExtractEpisodeFacts" => Some(Self::ExtractEpisodeFacts),
            "EmbedFact" => Some(Self::EmbedFact),
            "ConsolidateFactCluster" => Some(Self::ConsolidateFactCluster),
            "ResolveSupersession" => Some(Self::ResolveSupersession),
            "RefreshPromptBlock" => Some(Self::RefreshPromptBlock),
            "SummarizeBrowseWindow" => Some(Self::SummarizeBrowseWindow),
            "RebuildEmbeddingGeneration" => Some(Self::RebuildEmbeddingGeneration),
            "CompactMemoryIndex" => Some(Self::CompactMemoryIndex),
            "PruneLowValueMemory" => Some(Self::PruneLowValueMemory),
            _ => None,
        }
    }
}

/// Execution status of a maintenance job.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum JobStatus {
    Pending,
    Running,
    Completed,
    Failed,
}

impl JobStatus {
    pub fn as_str(&self) -> &'static str {
        match self {
            Self::Pending => "pending",
            Self::Running => "running",
            Self::Completed => "completed",
            Self::Failed => "failed",
        }
    }

    pub fn from_str(s: &str) -> Self {
        match s.to_ascii_lowercase().as_str() {
            "running" => Self::Running,
            "completed" => Self::Completed,
            "failed" => Self::Failed,
            _ => Self::Pending,
        }
    }
}

/// Dynamic scheduling policy governing background memory maintenance.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MemoryMaintenancePolicy {
    pub private_context: bool,
    pub user_memory_enabled: bool,
    pub browser_idle_state: bool,
    pub power_or_battery_policy: String,
    pub memory_pressure: String,
    pub network_allowed_for_consolidation: bool,
    pub max_cpu_budget: f32,
    pub max_jobs_per_tick: usize,
}

impl Default for MemoryMaintenancePolicy {
    fn default() -> Self {
        Self {
            private_context: false,
            user_memory_enabled: true,
            browser_idle_state: true,
            power_or_battery_policy: "normal".to_string(),
            memory_pressure: "normal".to_string(),
            network_allowed_for_consolidation: true,
            max_cpu_budget: 0.25,
            max_jobs_per_tick: 5,
        }
    }
}

/// Policy dictating dreaming schedule thresholds (e.g. 24h OR 5 sessions).
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct DreamPolicy {
    pub max_age_secs: i64,
    pub max_sessions: usize,
}

impl Default for DreamPolicy {
    fn default() -> Self {
        Self {
            max_age_secs: 24 * 3600, // 24 hours
            max_sessions: 5,
        }
    }
}

impl DreamPolicy {
    pub fn new(max_age_secs: i64, max_sessions: usize) -> Self {
        Self {
            max_age_secs,
            max_sessions,
        }
    }

    pub fn should_dream(
        &self,
        last_dream_at: Option<DateTime<Utc>>,
        sessions_since_dream: usize,
        now: DateTime<Utc>,
    ) -> bool {
        if self.max_sessions > 0 && sessions_since_dream >= self.max_sessions {
            return true;
        }
        match last_dream_at {
            None => false,
            Some(last) => {
                let elapsed = (now - last).num_seconds();
                elapsed >= self.max_age_secs && sessions_since_dream > 0
            }
        }
    }
}

/// Metadata tracked for dreaming and consolidation cycles.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, Default)]
pub struct DreamMetadata {
    pub last_dream_at: Option<DateTime<Utc>>,
    pub sessions_since_dream: usize,
    pub total_dreams_completed: usize,
    pub last_processed_cluster_hash: Option<String>,
}

/// Episodic input session candidate for dreaming consolidation.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct DreamSessionInput {
    pub session_id: String,
    pub is_private: bool,
    pub facts: Vec<String>,
    pub created_at: DateTime<Utc>,
}

/// In-flight or scheduled maintenance job.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct MaintenanceJob {
    pub id: String,
    pub kind: MemoryJobKind,
    pub status: JobStatus,
    pub source_cursor: Option<String>,
    pub idempotency_key: String,
    pub payload: Option<String>,
    pub created_at: DateTime<Utc>,
}

/// Compute a deterministic idempotency key for a maintenance job.
pub fn compute_job_idempotency_key(
    kind: MemoryJobKind,
    source_cursor: Option<&str>,
    model_generation: Option<&str>,
) -> String {
    let mut hasher = Sha256::new();
    hasher.update(kind.as_str().as_bytes());
    hasher.update(b"::");
    if let Some(cursor) = source_cursor {
        hasher.update(cursor.as_bytes());
    }
    hasher.update(b"::");
    if let Some(gen) = model_generation {
        hasher.update(gen.as_bytes());
    }
    format!("{:x}", hasher.finalize())
}

/// Compute a deterministic cluster hash from a list of candidate fact strings.
pub fn compute_cluster_hash(facts: &[&str]) -> String {
    let mut normalized: Vec<String> = facts.iter().map(|f| f.trim().to_lowercase()).collect();
    normalized.sort();
    let mut hasher = Sha256::new();
    for fact in normalized {
        hasher.update(fact.as_bytes());
        hasher.update(b"::");
    }
    format!("{:x}", hasher.finalize())
}

/// Actions resulting from the consolidation state machine.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub enum ConsolidationAction {
    /// Reinforce an existing matching fact with increased confidence.
    Reinforce {
        fact_id: String,
        confidence_boost: f32,
    },
    /// Supersede an older outdated fact with a newer corrected fact.
    Supersede {
        old_fact_id: String,
        new_fact_text: String,
        provenance: String,
    },
    /// Store a completely new distinct fact claim.
    CreateNew {
        fact_text: String,
        category: String,
        importance: f32,
        provenance: String,
    },
    /// Retain contradictory claims side-by-side with lower confidence.
    RetainConflict {
        fact_text: String,
        conflicting_fact_id: String,
    },
}

/// Decide consolidation action given existing facts and candidate claim.
pub fn evaluate_claim(
    existing_facts: &[(String, String, f32, f32)], // (id, text, importance, confidence)
    candidate_text: &str,
    candidate_category: &str,
    candidate_importance: f32,
    provenance: &str,
) -> ConsolidationAction {
    let cand_clean = candidate_text.trim().to_lowercase();

    for (id, text, _imp, _conf) in existing_facts {
        let existing_clean = text.trim().to_lowercase();
        if existing_clean == cand_clean {
            return ConsolidationAction::Reinforce {
                fact_id: id.clone(),
                confidence_boost: 0.1,
            };
        }
    }

    ConsolidationAction::CreateNew {
        fact_text: candidate_text.to_string(),
        category: candidate_category.to_string(),
        importance: candidate_importance,
        provenance: provenance.to_string(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use chrono::TimeZone;

    #[test]
    fn test_dream_policy_threshold_sessions() {
        let policy = DreamPolicy::new(86400, 5);
        let fixed_time = Utc.with_ymd_and_hms(2026, 8, 20, 12, 0, 0).unwrap();

        // Under 5 sessions -> false
        assert!(!policy.should_dream(Some(fixed_time), 4, fixed_time));
        // Exactly 5 sessions -> true
        assert!(policy.should_dream(Some(fixed_time), 5, fixed_time));
        // Over 5 sessions -> true
        assert!(policy.should_dream(Some(fixed_time), 6, fixed_time));
    }

    #[test]
    fn test_dream_policy_threshold_time() {
        let policy = DreamPolicy::new(86400, 5); // 24h
        let t0 = Utc.with_ymd_and_hms(2026, 8, 20, 0, 0, 0).unwrap();
        let t_23h = Utc.with_ymd_and_hms(2026, 8, 20, 23, 0, 0).unwrap();
        let t_24h = Utc.with_ymd_and_hms(2026, 8, 21, 0, 0, 0).unwrap();

        // 23 hours elapsed, 1 session -> false
        assert!(!policy.should_dream(Some(t0), 1, t_23h));
        // 24 hours elapsed, 1 session -> true
        assert!(policy.should_dream(Some(t0), 1, t_24h));
    }

    #[test]
    fn test_compute_cluster_hash_idempotence() {
        let facts_a = vec!["User prefers dark theme", "Works at Acme Corp"];
        let facts_b = vec!["Works at Acme Corp", "User prefers dark theme"];
        let facts_c = vec![" user prefers dark theme ", "works at acme corp"];

        let hash_a = compute_cluster_hash(&facts_a);
        let hash_b = compute_cluster_hash(&facts_b);
        let hash_c = compute_cluster_hash(&facts_c);

        assert_eq!(hash_a, hash_b);
        assert_eq!(hash_a, hash_c);
    }
}
