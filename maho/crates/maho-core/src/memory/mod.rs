//! 3-tier memory engine and context subsystems.

pub mod consolidation;
pub mod engine;
pub mod retrieval;
pub mod snapshot;
pub mod tier;

pub use consolidation::{
    compute_cluster_hash, compute_job_idempotency_key, evaluate_claim, ConsolidationAction,
    DreamMetadata, DreamPolicy, DreamSessionInput, JobStatus, MaintenanceJob, MemoryJobKind,
    MemoryMaintenancePolicy,
};
pub use engine::{
    validate_vector, DefaultEmbeddingEngine, EmbeddingEngine, EmbeddingModelDescriptor,
    MemoryEngine,
};
pub use retrieval::{HybridMemoryResult, HybridMemoryRetriever};
pub use snapshot::{Embedding, MemoryIndexSnapshot};
pub use tier::{
    FactSource, MemoryFact, MemoryTier, PromptBlock, PromptBlockTier, L1_BRIEFING_LABEL,
};
