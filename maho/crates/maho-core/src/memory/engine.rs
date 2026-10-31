//! Core 3-tier memory engine orchestrating embeddings, HNSW, FTS hybrid retrieval, and consolidation.

use instant_distance::{Builder, HnswMap, Search};
use maho_storage::sqlite::{MemoryInsertParams, SqliteStorage};
use sha2::Digest;
use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::sync::Arc;

use super::consolidation::{
    compute_cluster_hash, evaluate_claim, ConsolidationAction, DreamMetadata, DreamPolicy,
    DreamSessionInput, JobStatus, MaintenanceJob, MemoryJobKind, MemoryMaintenancePolicy,
};
use super::retrieval::{HybridMemoryResult, HybridMemoryRetriever};
use super::snapshot::{
    load_snapshot, save_snapshot, Embedding, MEMORY_INDEX_MAX_SEGMENTS, MEMORY_INDEX_SNAPSHOT_FILE,
};
use super::tier::{PromptBlock, PromptBlockTier, L1_BRIEFING_LABEL};
use crate::embedding::{bytes_to_vec, vec_to_bytes, EMBED_DIM};
use crate::error::CoreError;

/// Descriptor identifying an embedding model and format.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EmbeddingModelDescriptor {
    pub model_id: String,
    pub model_version: String,
    pub model_sha256: Option<String>,
}

/// Trait defining dense text embedding capability.
pub trait EmbeddingEngine: Send + Sync {
    fn descriptor(&self) -> EmbeddingModelDescriptor;
    fn embed_single(&self, text: &str) -> Result<Vec<f32>, CoreError>;
    fn embed_batch(&self, texts: &[String]) -> Result<Vec<Vec<f32>>, CoreError>;
}

/// Default built-in embedding engine delegating to `crate::embedding`.
pub struct DefaultEmbeddingEngine;

impl EmbeddingEngine for DefaultEmbeddingEngine {
    fn descriptor(&self) -> EmbeddingModelDescriptor {
        EmbeddingModelDescriptor {
            model_id: "all-MiniLM-L6-v2".to_string(),
            model_version: "1.0.0".to_string(),
            model_sha256: None,
        }
    }

    fn embed_single(&self, text: &str) -> Result<Vec<f32>, CoreError> {
        let res = crate::embedding::embed(&[text.to_string()])?;
        Ok(res
            .into_iter()
            .next()
            .unwrap_or_else(|| vec![0.0; EMBED_DIM]))
    }

    fn embed_batch(&self, texts: &[String]) -> Result<Vec<Vec<f32>>, CoreError> {
        crate::embedding::embed(texts)
    }
}

/// Validate vector length against expected dimensionality.
pub fn validate_vector(v: &[f32], expected_dim: usize) -> Result<(), CoreError> {
    if v.len() != expected_dim {
        return Err(CoreError::Embedding(format!(
            "invalid vector dimension: expected {}, got {}",
            expected_dim,
            v.len()
        )));
    }
    Ok(())
}

pub struct MemoryEngine {
    pub embedding_engine: Arc<dyn EmbeddingEngine>,
    pub generation_id: String,
    pub hnsw_segments: Vec<HnswMap<Embedding, String>>,
    pub active_segments: HashMap<String, usize>,
    pub last_change_sequence: i64,
    pub index_initialized: bool,
    pub retriever: HybridMemoryRetriever,
    pub maintenance_policy: MemoryMaintenancePolicy,
    pub dream_policy: DreamPolicy,
    pub dream_metadata: DreamMetadata,
    pub pending_sessions: Vec<DreamSessionInput>,
    pub queued_jobs: Vec<MaintenanceJob>,
    pub processed_cluster_hashes: HashSet<String>,
    pub mock_time: Option<chrono::DateTime<chrono::Utc>>,
}

impl Default for MemoryEngine {
    fn default() -> Self {
        Self::new()
    }
}

impl MemoryEngine {
    pub fn new() -> Self {
        let engine = Arc::new(DefaultEmbeddingEngine);
        Self::with_embedding_engine(engine)
    }

    pub fn with_embedding_engine(engine: Arc<dyn EmbeddingEngine>) -> Self {
        let desc = engine.descriptor();
        Self {
            embedding_engine: engine,
            generation_id: format!("{}:{}", desc.model_id, desc.model_version),
            hnsw_segments: Vec::new(),
            active_segments: HashMap::new(),
            last_change_sequence: 0,
            index_initialized: false,
            retriever: HybridMemoryRetriever::default(),
            maintenance_policy: MemoryMaintenancePolicy::default(),
            dream_policy: DreamPolicy::default(),
            dream_metadata: DreamMetadata::default(),
            pending_sessions: Vec::new(),
            queued_jobs: Vec::new(),
            processed_cluster_hashes: HashSet::new(),
            mock_time: None,
        }
    }

    /// Return current time, using mock_time if injected for deterministic testing.
    pub fn now(&self) -> chrono::DateTime<chrono::Utc> {
        self.mock_time.unwrap_or_else(chrono::Utc::now)
    }

    /// Inject or clear a fixed mock timestamp for testing.
    pub fn set_mock_time(&mut self, time: Option<chrono::DateTime<chrono::Utc>>) {
        self.mock_time = time;
    }

    /// Record a completed session into episodic memory for dreaming evaluation.
    pub fn record_session(&mut self, session: DreamSessionInput) {
        if !session.is_private {
            self.dream_metadata.sessions_since_dream += 1;
        }
        self.pending_sessions.push(session);
    }

    /// Enqueue a maintenance job into the execution queue.
    pub fn enqueue_job(&mut self, job: MaintenanceJob) {
        self.queued_jobs.push(job);
    }

    /// Retrieve the current L1 briefing prompt block from storage.
    pub fn get_l1_briefing(
        &self,
        storage: &SqliteStorage,
    ) -> Result<Option<PromptBlock>, CoreError> {
        match storage.get_memory_block(L1_BRIEFING_LABEL) {
            Ok(Some(content)) => {
                let token_count = content.split_whitespace().count();
                Ok(Some(PromptBlock {
                    label: L1_BRIEFING_LABEL.to_string(),
                    content,
                    tier: PromptBlockTier::Hot,
                    token_count,
                    updated_at: self.now(),
                }))
            }
            Ok(None) => Ok(None),
            Err(e) => Err(CoreError::Storage(e.to_string())),
        }
    }

    /// Regenerate L1 briefing block content from top active memories and bump timestamp.
    pub fn refresh_l1_briefing_block(
        &mut self,
        storage: &SqliteStorage,
    ) -> Result<PromptBlock, CoreError> {
        let now = self.now();
        let memories = storage
            .load_all_memories()
            .map_err(|e| CoreError::Storage(e.to_string()))?;

        let mut content = String::from("User Profile & Memory Briefing:\n");
        if memories.is_empty() {
            content.push_str("No active memories recorded.\n");
        } else {
            for (i, mem) in memories.iter().enumerate().take(10) {
                content.push_str(&format!("{}. {}\n", i + 1, mem.1));
            }
        }

        let token_count = content.split_whitespace().count();
        storage
            .upsert_memory_block(
                L1_BRIEFING_LABEL,
                &content,
                PromptBlockTier::Hot.as_str(),
                token_count as i64,
            )
            .map_err(|e| CoreError::Storage(e.to_string()))?;

        Ok(PromptBlock {
            label: L1_BRIEFING_LABEL.to_string(),
            content,
            tier: PromptBlockTier::Hot,
            token_count,
            updated_at: now,
        })
    }

    /// Rebuild or incrementally synchronize the active generation HNSW index.
    pub fn rebuild_index(&mut self, storage: &SqliteStorage) -> Result<(), CoreError> {
        let _span = tracing::info_span!("memory_engine_rebuild_index").entered();
        let snapshot_path = memory_index_snapshot_path(storage);

        if !self.index_initialized {
            let restored = snapshot_path
                .as_deref()
                .and_then(|path| load_snapshot(path).ok());
            if let Some(snapshot) = restored {
                // Check if snapshot generation is compatible
                if snapshot.embedding_model_id == self.embedding_engine.descriptor().model_id
                    && snapshot.dimensions == EMBED_DIM
                {
                    self.hnsw_segments = snapshot.segments;
                    self.active_segments = snapshot.active_segments;
                    self.last_change_sequence = snapshot.last_change_sequence;
                    self.generation_id = snapshot.generation_id;
                    self.index_initialized = true;
                } else {
                    return self.full_rebuild(storage, snapshot_path.as_deref());
                }
            } else {
                return self.full_rebuild(storage, snapshot_path.as_deref());
            }
        }

        let (high_water, changes) = storage
            .load_memory_embeddings_since(self.last_change_sequence)
            .map_err(|e| CoreError::Storage(e.to_string()))?;

        if high_water < self.last_change_sequence {
            return self.full_rebuild(storage, snapshot_path.as_deref());
        }
        if changes.is_empty() {
            self.last_change_sequence = high_water;
            return Ok(());
        }

        let segment_id = self.hnsw_segments.len();
        let mut ids = Vec::new();
        let mut vecs = Vec::new();

        for (id, bytes) in changes {
            self.active_segments.remove(&id);
            let Some(bytes) = bytes else {
                continue;
            };
            if let Ok(vector) = bytes_to_vec(&bytes) {
                if validate_vector(&vector, EMBED_DIM).is_ok() {
                    self.active_segments.insert(id.clone(), segment_id);
                    ids.push(id);
                    vecs.push(Embedding(vector));
                }
            }
        }

        if !vecs.is_empty() {
            self.hnsw_segments.push(Builder::default().build(vecs, ids));
        }
        self.last_change_sequence = high_water;

        if self.hnsw_segments.len() > MEMORY_INDEX_MAX_SEGMENTS {
            return self.full_rebuild(storage, snapshot_path.as_deref());
        }

        if let Some(path) = snapshot_path.as_deref() {
            self.save_active_snapshot(path)?;
        }

        Ok(())
    }

    fn full_rebuild(
        &mut self,
        storage: &SqliteStorage,
        snapshot_path: Option<&Path>,
    ) -> Result<(), CoreError> {
        let entries = storage
            .load_all_active_memory_embeddings()
            .map_err(|e| CoreError::Storage(e.to_string()))?;
        let high_water = storage
            .memory_index_change_sequence()
            .map_err(|e| CoreError::Storage(e.to_string()))?;

        let mut ids = Vec::new();
        let mut vecs = Vec::new();

        for (id, bytes) in entries {
            if let Ok(vector) = bytes_to_vec(&bytes) {
                if validate_vector(&vector, EMBED_DIM).is_ok() {
                    ids.push(id);
                    vecs.push(Embedding(vector));
                }
            }
        }

        self.hnsw_segments.clear();
        self.active_segments.clear();
        if !vecs.is_empty() {
            self.active_segments
                .extend(ids.iter().cloned().map(|id| (id, 0)));
            self.hnsw_segments.push(Builder::default().build(vecs, ids));
        }

        self.last_change_sequence = high_water;
        self.index_initialized = true;

        if let Some(path) = snapshot_path {
            self.save_active_snapshot(path)?;
        }

        Ok(())
    }

    fn save_active_snapshot(&self, path: &Path) -> Result<(), CoreError> {
        let desc = self.embedding_engine.descriptor();
        save_snapshot(
            path,
            &desc.model_id,
            &desc.model_version,
            desc.model_sha256.as_deref(),
            &self.generation_id,
            self.last_change_sequence,
            &self.hnsw_segments,
            &self.active_segments,
        )
    }

    /// Dense-only nearest neighbor search across active segments.
    pub fn search_dense(&self, query_embedding: &[f32], top_k: usize) -> Vec<(String, f32)> {
        if top_k == 0 || validate_vector(query_embedding, EMBED_DIM).is_err() {
            return Vec::new();
        }
        let q = Embedding(query_embedding.to_vec());
        let mut candidates = Vec::new();

        for (segment_id, hnsw) in self.hnsw_segments.iter().enumerate() {
            let mut search = Search::default();
            candidates.extend(
                hnsw.search(&q, &mut search)
                    .filter(|item| {
                        self.active_segments.get(item.value).copied() == Some(segment_id)
                    })
                    .map(|item| (item.value.clone(), item.distance)),
            );
        }

        candidates.sort_unstable_by(|left, right| left.1.total_cmp(&right.1));
        candidates.truncate(top_k);
        candidates
    }

    /// Hybrid lexical (FTS5) + dense (HNSW) retrieval.
    pub fn search_hybrid(
        &self,
        query: &str,
        storage: &SqliteStorage,
        top_k: usize,
    ) -> Vec<HybridMemoryResult> {
        if top_k == 0 {
            return Vec::new();
        }

        // 1. Fetch lexical FTS candidates
        let lexical_candidates = storage
            .search_memories_fts(query, top_k * 2)
            .unwrap_or_default();

        // 2. Fetch dense candidates if embedding succeeds
        let dense_candidates = match self.embedding_engine.embed_single(query) {
            Ok(emb) => self.search_dense(&emb, top_k * 2),
            Err(_) => Vec::new(),
        };

        // 3. Collect metadata for candidates
        let mut metadata_map = HashMap::new();
        if let Ok(all_facts) = storage.load_all_memories() {
            for fact in all_facts {
                metadata_map.insert(fact.0, (fact.5 as f32, 1.0f32, fact.2, fact.1));
            }
        }

        // 4. Fuse with RRF
        self.retriever.fuse(
            &lexical_candidates,
            &dense_candidates,
            &metadata_map,
            top_k,
            None,
        )
    }

    /// Run one step of background maintenance (dreaming / consolidation / re-embedding / briefing refresh).
    pub fn step_maintenance(&mut self, storage: &SqliteStorage) -> Result<usize, CoreError> {
        if self.maintenance_policy.private_context || !self.maintenance_policy.user_memory_enabled {
            return Ok(0);
        }

        let current_time = self.now();

        // 1. Evaluate DreamPolicy against dream metadata
        let should_dream = self.dream_policy.should_dream(
            self.dream_metadata.last_dream_at,
            self.dream_metadata.sessions_since_dream,
            current_time,
        );

        if should_dream && !self.pending_sessions.is_empty() {
            // Filter non-private sessions
            let mut eligible_facts: Vec<String> = Vec::new();
            for session in &self.pending_sessions {
                if !session.is_private {
                    eligible_facts.extend(session.facts.clone());
                }
            }

            if !eligible_facts.is_empty() {
                let facts_refs: Vec<&str> = eligible_facts.iter().map(|s| s.as_str()).collect();
                let cluster_hash = compute_cluster_hash(&facts_refs);

                // Enqueue ConsolidateFactCluster then RefreshPromptBlock in order
                let consolidate_job = MaintenanceJob {
                    id: format!("job_consolidate_{}", cluster_hash),
                    kind: MemoryJobKind::ConsolidateFactCluster,
                    status: JobStatus::Pending,
                    source_cursor: None,
                    idempotency_key: cluster_hash.clone(),
                    payload: serde_json::to_string(&eligible_facts).ok(),
                    created_at: current_time,
                };

                let refresh_job = MaintenanceJob {
                    id: format!("job_refresh_l1_{}", cluster_hash),
                    kind: MemoryJobKind::RefreshPromptBlock,
                    status: JobStatus::Pending,
                    source_cursor: None,
                    idempotency_key: format!("briefing_{}", cluster_hash),
                    payload: None,
                    created_at: current_time,
                };

                self.queued_jobs.push(consolidate_job);
                self.queued_jobs.push(refresh_job);
                self.dream_metadata.last_processed_cluster_hash = Some(cluster_hash);
            }

            // Mark last dream metadata
            self.dream_metadata.last_dream_at = Some(current_time);
            self.dream_metadata.sessions_since_dream = 0;
            self.pending_sessions.clear();
        }

        // 2. Process maintenance jobs from queue
        let mut jobs_processed = 0;
        while jobs_processed < self.maintenance_policy.max_jobs_per_tick
            && !self.queued_jobs.is_empty()
        {
            let mut job = self.queued_jobs.remove(0);
            job.status = JobStatus::Running;

            match job.kind {
                MemoryJobKind::ConsolidateFactCluster => {
                    let cluster_key = job.idempotency_key.clone();
                    if !self.processed_cluster_hashes.contains(&cluster_key) {
                        if let Some(payload_str) = &job.payload {
                            if let Ok(facts) = serde_json::from_str::<Vec<String>>(payload_str) {
                                let existing_records =
                                    storage.load_all_memories().unwrap_or_default();
                                let existing_tuples: Vec<(String, String, f32, f32)> =
                                    existing_records
                                        .iter()
                                        .map(|r| (r.0.clone(), r.1.clone(), r.5 as f32, 1.0f32))
                                        .collect();

                                for fact_text in facts {
                                    let action = evaluate_claim(
                                        &existing_tuples,
                                        &fact_text,
                                        "general",
                                        0.8,
                                        "dream_consolidation",
                                    );

                                    match action {
                                        ConsolidationAction::CreateNew { fact_text, .. } => {
                                            let fact_id = format!(
                                                "fact_{:x}",
                                                sha2::Sha256::digest(fact_text.as_bytes())
                                            );
                                            let _ = storage.insert_memory(MemoryInsertParams {
                                                id: &fact_id,
                                                fact: &fact_text,
                                                source: "dream",
                                                session_id: None,
                                                categories: "[]",
                                                importance: 0.8,
                                                metadata: None,
                                            });
                                            if let Ok(emb) =
                                                self.embedding_engine.embed_single(&fact_text)
                                            {
                                                let bytes = vec_to_bytes(&emb);
                                                let _ = storage
                                                    .update_memory_embedding(&fact_id, &bytes);
                                            }
                                        }
                                        ConsolidationAction::Reinforce { .. } => {
                                            // Fact reinforced; no duplicate row created
                                        }
                                        _ => {}
                                    }
                                }
                            }
                        }
                        self.processed_cluster_hashes.insert(cluster_key);
                    }
                    job.status = JobStatus::Completed;
                    jobs_processed += 1;
                }
                MemoryJobKind::RefreshPromptBlock => {
                    let _ = self.refresh_l1_briefing_block(storage);
                    job.status = JobStatus::Completed;
                    jobs_processed += 1;
                }
                MemoryJobKind::EmbedFact => {
                    if let Some(fact_id) = &job.source_cursor {
                        if let Ok(all_facts) = storage.load_all_memories() {
                            if let Some(record) = all_facts.into_iter().find(|f| f.0 == *fact_id) {
                                if let Ok(emb) = self.embedding_engine.embed_single(&record.1) {
                                    let bytes = vec_to_bytes(&emb);
                                    let _ = storage.update_memory_embedding(&record.0, &bytes);
                                }
                            }
                        }
                    }
                    job.status = JobStatus::Completed;
                    jobs_processed += 1;
                }
                MemoryJobKind::RebuildEmbeddingGeneration => {
                    if let Ok(all_facts) = storage.load_all_memories() {
                        for record in all_facts {
                            if let Ok(emb) = self.embedding_engine.embed_single(&record.1) {
                                let bytes = vec_to_bytes(&emb);
                                let _ = storage.update_memory_embedding(&record.0, &bytes);
                            }
                        }
                    }
                    job.status = JobStatus::Completed;
                    jobs_processed += 1;
                }
                _ => {
                    job.status = JobStatus::Completed;
                    jobs_processed += 1;
                }
            }
        }

        if jobs_processed > 0 {
            let _ = self.rebuild_index(storage);
            self.dream_metadata.total_dreams_completed += 1;
        }

        Ok(jobs_processed)
    }
}

fn memory_index_snapshot_path(storage: &SqliteStorage) -> Option<PathBuf> {
    let database_path = Path::new(storage.path());
    if storage.path() == ":memory:" {
        return None;
    }
    database_path
        .parent()
        .map(|directory| directory.join(MEMORY_INDEX_SNAPSHOT_FILE))
}

#[cfg(test)]
mod tests {
    use super::*;
    use chrono::{Duration, TimeZone, Utc};

    fn test_storage() -> SqliteStorage {
        SqliteStorage::open_in_memory_with_key("memory-dreaming-test-key").unwrap()
    }

    #[test]
    fn test_dream_trigger_threshold_sessions_fires_once_and_idempotent() {
        let storage = test_storage();
        let mut engine = MemoryEngine::new();
        engine.dream_policy = DreamPolicy::new(86400, 5); // 24h OR 5 sessions
        let fixed_time = Utc.with_ymd_and_hms(2026, 8, 20, 10, 0, 0).unwrap();
        engine.set_mock_time(Some(fixed_time));

        // 1. Record 4 sessions -> threshold not met
        for i in 1..=4 {
            engine.record_session(DreamSessionInput {
                session_id: format!("sess-{}", i),
                is_private: false,
                facts: vec![format!("Fact from session {}", i)],
                created_at: fixed_time,
            });
        }

        // Maintenance step should not dream
        let processed = engine.step_maintenance(&storage).unwrap();
        assert_eq!(processed, 0, "4 sessions should not trigger dreaming yet");
        assert_eq!(engine.dream_metadata.sessions_since_dream, 4);

        // 2. Record 5th session -> threshold met!
        engine.record_session(DreamSessionInput {
            session_id: "sess-5".to_string(),
            is_private: false,
            facts: vec!["Fact from session 5".to_string()],
            created_at: fixed_time,
        });

        let processed = engine.step_maintenance(&storage).unwrap();
        // Should have processed 2 jobs: ConsolidateFactCluster and RefreshPromptBlock
        assert_eq!(processed, 2, "5 sessions must fire dreaming jobs");
        assert_eq!(engine.dream_metadata.sessions_since_dream, 0);
        assert_eq!(engine.dream_metadata.last_dream_at, Some(fixed_time));

        // 3. Re-running maintenance immediately without new sessions does NOT re-fire (idempotent)
        let re_processed = engine.step_maintenance(&storage).unwrap();
        assert_eq!(
            re_processed, 0,
            "Re-running maintenance without new sessions must be idempotent (0 jobs)"
        );
    }

    #[test]
    fn test_dream_trigger_threshold_age_fires_consolidation() {
        let storage = test_storage();
        let mut engine = MemoryEngine::new();
        engine.dream_policy = DreamPolicy::new(86400, 5); // 24h OR 5 sessions

        let t0 = Utc.with_ymd_and_hms(2026, 8, 20, 0, 0, 0).unwrap();
        engine.set_mock_time(Some(t0));
        engine.dream_metadata.last_dream_at = Some(t0);

        // Record 1 session 12 hours later (threshold 5 sessions not met, age 12h < 24h)
        let t_12h = t0 + Duration::hours(12);
        engine.set_mock_time(Some(t_12h));
        engine.record_session(DreamSessionInput {
            session_id: "sess-mid".to_string(),
            is_private: false,
            facts: vec!["Midday discovery".to_string()],
            created_at: t_12h,
        });
        let processed_12h = engine.step_maintenance(&storage).unwrap();
        assert_eq!(
            processed_12h, 0,
            "12h elapsed does not satisfy 24h age threshold"
        );

        // Advance mock time to 25 hours later (25h >= 24h threshold)
        let t_25h = t0 + Duration::hours(25);
        engine.set_mock_time(Some(t_25h));
        let processed_25h = engine.step_maintenance(&storage).unwrap();
        assert_eq!(
            processed_25h, 2,
            "25h elapsed must satisfy 24h threshold and run dreaming"
        );
        assert_eq!(engine.dream_metadata.last_dream_at, Some(t_25h));
        assert_eq!(engine.dream_metadata.sessions_since_dream, 0);

        // Re-running immediately without new sessions does not fire
        let processed_again = engine.step_maintenance(&storage).unwrap();
        assert_eq!(
            processed_again, 0,
            "Idempotent on re-run with 0 new sessions"
        );
    }

    #[test]
    fn test_refresh_prompt_block_regenerates_l1_briefing_and_bumps_timestamp() {
        let storage = test_storage();
        let mut engine = MemoryEngine::new();
        engine.dream_policy = DreamPolicy::new(86400, 1);
        let t0 = Utc.with_ymd_and_hms(2026, 8, 20, 10, 0, 0).unwrap();
        engine.set_mock_time(Some(t0));

        // 1. Initially no briefing
        let initial_briefing = engine.get_l1_briefing(&storage).unwrap();
        assert!(initial_briefing.is_none());

        // 2. Add session and run maintenance
        engine.record_session(DreamSessionInput {
            session_id: "sess-briefing".to_string(),
            is_private: false,
            facts: vec![
                "User prefers TypeScript and Rust".to_string(),
                "User lives in Tokyo timezone".to_string(),
            ],
            created_at: t0,
        });
        engine.step_maintenance(&storage).unwrap();

        // 3. Verify L1 briefing generated
        let briefing1 = engine
            .get_l1_briefing(&storage)
            .unwrap()
            .expect("L1 briefing must exist");
        assert_eq!(briefing1.label, "briefing");
        assert_eq!(briefing1.tier, PromptBlockTier::Hot);
        assert!(briefing1
            .content
            .contains("User prefers TypeScript and Rust"));
        assert!(briefing1.content.contains("User lives in Tokyo timezone"));
        assert_eq!(briefing1.updated_at, t0);

        // 4. Advance time, add another session, run maintenance
        let t1 = t0 + Duration::hours(30);
        engine.set_mock_time(Some(t1));
        engine.record_session(DreamSessionInput {
            session_id: "sess-briefing-2".to_string(),
            is_private: false,
            facts: vec!["User uses macOS terminal Ghostty".to_string()],
            created_at: t1,
        });
        engine.step_maintenance(&storage).unwrap();

        // 5. Verify refreshed briefing has updated content and bumped timestamp
        let briefing2 = engine
            .get_l1_briefing(&storage)
            .unwrap()
            .expect("L1 briefing must exist");
        assert!(briefing2
            .content
            .contains("User uses macOS terminal Ghostty"));
        assert_eq!(briefing2.updated_at, t1);
    }

    #[test]
    fn test_private_and_incognito_sessions_excluded_from_dream_inputs() {
        let storage = test_storage();
        let mut engine = MemoryEngine::new();
        engine.dream_policy = DreamPolicy::new(86400, 1);
        let t0 = Utc.with_ymd_and_hms(2026, 8, 20, 10, 0, 0).unwrap();
        engine.set_mock_time(Some(t0));

        // 1. Record a private session with sensitive data
        engine.record_session(DreamSessionInput {
            session_id: "sess-private-1".to_string(),
            is_private: true,
            facts: vec!["Secret private token: sk-SECRET123".to_string()],
            created_at: t0,
        });

        // 2. Record a public session
        engine.record_session(DreamSessionInput {
            session_id: "sess-public-1".to_string(),
            is_private: false,
            facts: vec!["Public preference: loves Rust async".to_string()],
            created_at: t0,
        });

        engine.step_maintenance(&storage).unwrap();

        // 3. Verify private fact was NOT consolidated into memories
        let memories = storage.load_all_memories().unwrap();
        for mem in &memories {
            assert!(
                !mem.1.contains("SECRET123"),
                "Private session facts must be strictly excluded from dream inputs!"
            );
        }

        // 4. Verify private fact is not in L1 briefing
        let briefing = engine.get_l1_briefing(&storage).unwrap().unwrap();
        assert!(!briefing.content.contains("SECRET123"));
        assert!(briefing.content.contains("loves Rust async"));
    }

    #[test]
    fn test_memory_disabled_profile_returns_without_jobs() {
        let storage = test_storage();
        let mut engine = MemoryEngine::new();
        engine.dream_policy = DreamPolicy::new(86400, 1);
        let t0 = Utc.with_ymd_and_hms(2026, 8, 20, 10, 0, 0).unwrap();
        engine.set_mock_time(Some(t0));

        // Disable user memory
        engine.maintenance_policy.user_memory_enabled = false;

        engine.record_session(DreamSessionInput {
            session_id: "sess-1".to_string(),
            is_private: false,
            facts: vec!["Some fact".to_string()],
            created_at: t0,
        });

        let processed = engine.step_maintenance(&storage).unwrap();
        assert_eq!(processed, 0, "Memory-disabled profile must return 0 jobs");
        let memories = storage.load_all_memories().unwrap();
        assert!(memories.is_empty());

        // Also test private context policy
        engine.maintenance_policy.user_memory_enabled = true;
        engine.maintenance_policy.private_context = true;

        let processed = engine.step_maintenance(&storage).unwrap();
        assert_eq!(processed, 0, "Private context policy must return 0 jobs");
    }

    #[test]
    fn test_idempotent_cluster_reprocessing_yields_no_duplicate_summary_rows() {
        let storage = test_storage();
        let mut engine = MemoryEngine::new();
        engine.dream_policy = DreamPolicy::new(86400, 1);
        let t0 = Utc.with_ymd_and_hms(2026, 8, 20, 10, 0, 0).unwrap();
        engine.set_mock_time(Some(t0));

        let facts = vec![
            "Preferred browser layout is vertical tabs".to_string(),
            "Default search engine is DuckDuckGo".to_string(),
        ];

        // First pass
        engine.record_session(DreamSessionInput {
            session_id: "sess-dup-1".to_string(),
            is_private: false,
            facts: facts.clone(),
            created_at: t0,
        });
        engine.step_maintenance(&storage).unwrap();

        let count_after_first = storage.load_all_memories().unwrap().len();
        assert_eq!(count_after_first, 2);

        // Manually enqueue identical consolidation job or session with same facts
        let facts_refs: Vec<&str> = facts.iter().map(|s| s.as_str()).collect();
        let cluster_hash = compute_cluster_hash(&facts_refs);
        engine.enqueue_job(MaintenanceJob {
            id: "job-manual-dup".to_string(),
            kind: MemoryJobKind::ConsolidateFactCluster,
            status: JobStatus::Pending,
            source_cursor: None,
            idempotency_key: cluster_hash,
            payload: serde_json::to_string(&facts).ok(),
            created_at: t0,
        });

        let processed = engine.step_maintenance(&storage).unwrap();
        assert_eq!(processed, 1);

        let count_after_second = storage.load_all_memories().unwrap().len();
        assert_eq!(
            count_after_second, count_after_first,
            "Re-processing same input cluster hash must yield no duplicate summary rows"
        );
    }
}
