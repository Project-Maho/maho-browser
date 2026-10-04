// allow: SIZE_OK — Lane T exact allowlist mandates test module edits inside memory_manager.rs without splitting or structural edits.
//! Memory feature backend: extracts facts from chat sessions, embeds them,
//! retrieves them as AI context. Operates as a sub-manager owned by MahoCore.

use bincode::Options;
use instant_distance::{Builder, HnswMap, Search};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::OnceLock;
use tokio::runtime::Runtime;
use tokio::sync::mpsc;
use tokio::task::JoinHandle;

use crate::embedding;
use crate::llm_client;
use maho_storage::sqlite::SqliteStorage;

pub fn get_runtime() -> &'static Runtime {
    static RT: OnceLock<Runtime> = OnceLock::new();
    RT.get_or_init(|| Runtime::new().expect("maho-core shared tokio runtime"))
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RawFact {
    pub fact: String,
    #[serde(default)]
    pub category: String,
    #[serde(default = "default_importance")]
    pub importance: f32,
}
fn default_importance() -> f32 {
    0.5
}

#[derive(Debug)]
pub enum MemoryResult {
    FactsExtracted {
        session_id: String,
        facts: Vec<RawFact>,
    },
    EmbeddingsReady {
        fact_id: String,
        embedding: Vec<f32>,
    },
    BrowsingSummaryReady {
        content: String,
    },
    BrowsingSummaryDue,
    Failed {
        reason: String,
    },
}

const MEMORY_INDEX_SNAPSHOT_FILE: &str = "memory_index_snapshot.bin";
const MEMORY_INDEX_SNAPSHOT_VERSION: u32 = 1;
const MEMORY_INDEX_SNAPSHOT_LIMIT: u64 = 256 * 1024 * 1024;
const MEMORY_INDEX_MAX_SEGMENTS: usize = 32;

/// Wrapper for instant-distance Point trait
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Embedding(pub Vec<f32>);
impl instant_distance::Point for Embedding {
    fn distance(&self, other: &Self) -> f32 {
        let dot: f32 = self.0.iter().zip(&other.0).map(|(a, b)| a * b).sum();
        let na: f32 = self.0.iter().map(|v| v * v).sum::<f32>().sqrt();
        let nb: f32 = other.0.iter().map(|v| v * v).sum::<f32>().sqrt();
        1.0 - (dot / (na * nb + 1e-8))
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct AuthSnapshot {
    pub provider: String,
    pub base_url: String,
    pub api_key: String,
    pub model: String,
}

#[derive(Serialize, Deserialize)]
struct MemoryIndexSnapshot {
    version: u32,
    last_change_sequence: i64,
    segments: Vec<HnswMap<Embedding, String>>,
    active_segments: HashMap<String, usize>,
}

#[derive(Serialize)]
struct MemoryIndexSnapshotRef<'a> {
    version: u32,
    last_change_sequence: i64,
    segments: &'a [HnswMap<Embedding, String>],
    active_segments: &'a HashMap<String, usize>,
}

pub struct MemoryManager {
    /// Immutable HNSW segments. Changed facts are appended to a new segment;
    /// `active_segments` tombstones superseded vectors in older segments.
    hnsw_segments: Vec<HnswMap<Embedding, String>>,
    active_segments: HashMap<String, usize>,
    last_change_sequence: i64,
    index_initialized: bool,
    result_rx: mpsc::Receiver<MemoryResult>,
    result_tx: mpsc::Sender<MemoryResult>,
    active_tasks: Vec<JoinHandle<()>>,
    auth: Option<AuthSnapshot>,
    pub last_summary_at: Option<chrono::DateTime<chrono::Utc>>,
}

impl Default for MemoryManager {
    fn default() -> Self {
        Self::new()
    }
}

impl MemoryManager {
    pub fn new() -> Self {
        let (tx, rx) = mpsc::channel(64);
        Self {
            hnsw_segments: Vec::new(),
            active_segments: HashMap::new(),
            last_change_sequence: 0,
            index_initialized: false,
            result_rx: rx,
            result_tx: tx,
            active_tasks: Vec::new(),
            auth: None,
            last_summary_at: None,
        }
    }
    pub fn set_auth(&mut self, auth: Option<AuthSnapshot>) {
        self.auth = auth;
    }

    pub fn start_browsing_summary_loop(&mut self) {
        let tx = self.result_tx.clone();
        self.active_tasks.push(get_runtime().spawn(async move {
            let mut ticker = tokio::time::interval(std::time::Duration::from_secs(3600));
            ticker.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Delay);
            ticker.tick().await; // skip immediate first tick at t=0
            loop {
                ticker.tick().await;
                let _ = tx.send(MemoryResult::BrowsingSummaryDue).await;
            }
        }));
    }

    pub fn note_summary_completed(&mut self, at: chrono::DateTime<chrono::Utc>) {
        self.last_summary_at = Some(at);
    }

    /// Restore the persistent HNSW snapshot and apply only facts changed since
    /// its journal cursor. Missing or invalid snapshots fall back to a complete
    /// build and are replaced atomically.
    pub fn rebuild_index(
        &mut self,
        storage: &SqliteStorage,
    ) -> Result<(), crate::error::CoreError> {
        let _span = tracing::info_span!("rebuild_index").entered();
        let snapshot_path = memory_index_snapshot_path(storage);

        if !self.index_initialized {
            let restored = snapshot_path
                .as_deref()
                .and_then(|path| Self::load_snapshot(path).ok());
            if let Some(snapshot) = restored {
                self.hnsw_segments = snapshot.segments;
                self.active_segments = snapshot.active_segments;
                self.last_change_sequence = snapshot.last_change_sequence;
                self.index_initialized = true;
            } else {
                return self.full_rebuild(storage, snapshot_path.as_deref());
            }
        }

        let (high_water, changes) = storage
            .load_memory_embeddings_since(self.last_change_sequence)
            .map_err(|e| crate::error::CoreError::Storage(e.to_string()))?;
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
            if let Ok(vector) = crate::embedding::bytes_to_vec(&bytes) {
                self.active_segments.insert(id.clone(), segment_id);
                ids.push(id);
                vecs.push(Embedding(vector));
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
            self.save_snapshot(path)?;
        }
        Ok(())
    }

    fn full_rebuild(
        &mut self,
        storage: &SqliteStorage,
        snapshot_path: Option<&Path>,
    ) -> Result<(), crate::error::CoreError> {
        let entries = storage
            .load_all_active_memory_embeddings()
            .map_err(|e| crate::error::CoreError::Storage(e.to_string()))?;
        let high_water = storage
            .memory_index_change_sequence()
            .map_err(|e| crate::error::CoreError::Storage(e.to_string()))?;
        let mut ids = Vec::new();
        let mut vecs = Vec::new();
        for (id, bytes) in entries {
            if let Ok(vector) = crate::embedding::bytes_to_vec(&bytes) {
                ids.push(id);
                vecs.push(Embedding(vector));
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
            self.save_snapshot(path)?;
        }
        Ok(())
    }

    fn load_snapshot(path: &Path) -> Result<MemoryIndexSnapshot, crate::error::CoreError> {
        let bytes =
            std::fs::read(path).map_err(|e| crate::error::CoreError::Storage(e.to_string()))?;
        let snapshot: MemoryIndexSnapshot = bincode::DefaultOptions::new()
            .with_limit(MEMORY_INDEX_SNAPSHOT_LIMIT)
            .deserialize(&bytes)
            .map_err(|e| crate::error::CoreError::Storage(e.to_string()))?;
        if snapshot.version != MEMORY_INDEX_SNAPSHOT_VERSION
            || snapshot.last_change_sequence < 0
            || snapshot
                .active_segments
                .values()
                .any(|segment| *segment >= snapshot.segments.len())
        {
            return Err(crate::error::CoreError::Storage(
                "invalid memory index snapshot".into(),
            ));
        }
        Ok(snapshot)
    }

    fn save_snapshot(&self, path: &Path) -> Result<(), crate::error::CoreError> {
        let snapshot = MemoryIndexSnapshotRef {
            version: MEMORY_INDEX_SNAPSHOT_VERSION,
            last_change_sequence: self.last_change_sequence,
            segments: &self.hnsw_segments,
            active_segments: &self.active_segments,
        };
        let bytes = bincode::DefaultOptions::new()
            .with_limit(MEMORY_INDEX_SNAPSHOT_LIMIT)
            .serialize(&snapshot)
            .map_err(|e| crate::error::CoreError::Storage(e.to_string()))?;
        let temp_path = path.with_extension("bin.tmp");
        std::fs::write(&temp_path, bytes)
            .and_then(|_| std::fs::rename(&temp_path, path))
            .map_err(|e| crate::error::CoreError::Storage(e.to_string()))
    }

    /// Top-K semantic search across the persisted base index and incremental
    /// segments, excluding vectors superseded by later changes.
    pub fn search(&self, query_embedding: &[f32], top_k: usize) -> Vec<String> {
        if top_k == 0 {
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
                    .map(|item| (item.distance, item.value.clone())),
            );
        }
        candidates.sort_unstable_by(|left, right| left.0.total_cmp(&right.0));
        candidates
            .into_iter()
            .take(top_k)
            .map(|(_, id)| id)
            .collect()
    }

    /// Triggered at session-end. Sync method spawns an async extraction.
    pub fn extract_from_session(&mut self, session_id: String, raw_text: String) {
        let Some(auth) = self.auth.clone() else {
            return;
        };
        let tx = self.result_tx.clone();
        let h = get_runtime().spawn(async move {
            match llm_client::extract_facts_json(&auth, &raw_text).await {
                Ok(facts) => {
                    let _ = tx
                        .send(MemoryResult::FactsExtracted { session_id, facts })
                        .await;
                }
                Err(e) => {
                    let _ = tx
                        .send(MemoryResult::Failed {
                            reason: format!("extract: {e}"),
                        })
                        .await;
                }
            }
        });
        self.active_tasks.push(h);
    }

    /// Triggered for nightly browsing summary.
    pub fn summarize_browsing(&mut self, raw_text: String) {
        let Some(auth) = self.auth.clone() else {
            return;
        };
        let tx = self.result_tx.clone();
        let h = get_runtime().spawn(async move {
            match llm_client::summarize_browsing(&auth, &raw_text).await {
                Ok(content) => {
                    let _ = tx
                        .send(MemoryResult::BrowsingSummaryReady { content })
                        .await;
                }
                Err(e) => {
                    let _ = tx
                        .send(MemoryResult::Failed {
                            reason: format!("summary: {e}"),
                        })
                        .await;
                }
            }
        });
        self.active_tasks.push(h);
    }

    /// Spawn embedding job for a fact.
    pub fn embed_fact(&mut self, fact_id: String, content: String) {
        let tx = self.result_tx.clone();
        let h = get_runtime().spawn(async move {
            let result = tokio::task::spawn_blocking(move || embedding::embed(&[content])).await;
            match result {
                Ok(Ok(mut vecs)) if !vecs.is_empty() => {
                    let _ = tx
                        .send(MemoryResult::EmbeddingsReady {
                            fact_id,
                            embedding: vecs.remove(0),
                        })
                        .await;
                }
                Ok(Err(e)) => {
                    let _ = tx
                        .send(MemoryResult::Failed {
                            reason: format!("embed: {e}"),
                        })
                        .await;
                }
                Err(e) => {
                    let _ = tx
                        .send(MemoryResult::Failed {
                            reason: format!("embed join: {e}"),
                        })
                        .await;
                }
                _ => {
                    let _ = tx
                        .send(MemoryResult::Failed {
                            reason: "empty embedding".into(),
                        })
                        .await;
                }
            }
        });
        self.active_tasks.push(h);
    }

    /// Drain results — called by MahoCore each tick. Caller persists via storage.
    pub fn drain_results(&mut self) -> Vec<MemoryResult> {
        let mut out = Vec::new();
        while let Ok(r) = self.result_rx.try_recv() {
            out.push(r);
        }
        self.active_tasks.retain(|h| !h.is_finished());
        out
    }

    /// Test-only helper: push an externally-spawned JoinHandle into active_tasks
    /// so the drop-aborts test can verify the Drop impl without spawning real work.
    #[cfg(test)]
    pub(crate) fn push_task_for_test(&mut self, handle: JoinHandle<()>) {
        self.active_tasks.push(handle);
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

impl Drop for MemoryManager {
    fn drop(&mut self) {
        // Audit finding H11: abort spawned background tasks so they do not
        // continue running detached after the manager (and its references)
        // are destroyed. JoinHandle::abort() is idempotent and safe to call
        // on already-finished tasks.
        for handle in self.active_tasks.drain(..) {
            handle.abort();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use maho_storage::sqlite::MemoryInsertParams;

    fn insert_embedding(storage: &SqliteStorage, id: &str, vector: &[f32]) {
        storage
            .insert_memory(MemoryInsertParams {
                id,
                fact: id,
                source: "test",
                session_id: None,
                categories: "[]",
                importance: 0.5,
                metadata: None,
            })
            .unwrap();
        storage
            .update_memory_embedding(id, &crate::embedding::vec_to_bytes(vector))
            .unwrap();
    }

    #[test]
    fn memory_index_snapshot_restores_and_applies_incremental_changes() {
        let directory = tempfile::tempdir().unwrap();
        let database_path = directory.path().join("memory.db");
        let database_path = database_path.to_str().unwrap();
        let storage = SqliteStorage::open_with_key(database_path, "snapshot-test-key").unwrap();
        insert_embedding(&storage, "first", &[1.0, 0.0]);

        let mut initial = MemoryManager::new();
        initial.rebuild_index(&storage).unwrap();
        assert_eq!(initial.search(&[1.0, 0.0], 1), vec!["first"]);
        let snapshot_path = directory.path().join(MEMORY_INDEX_SNAPSHOT_FILE);
        assert!(snapshot_path.exists());
        drop(initial);

        insert_embedding(&storage, "second", &[0.0, 1.0]);
        let mut restored = MemoryManager::new();
        restored.rebuild_index(&storage).unwrap();
        assert_eq!(restored.search(&[0.0, 1.0], 1), vec!["second"]);
        assert!(restored.hnsw_segments.len() >= 2);

        storage.delete_memory("first").unwrap();
        restored.rebuild_index(&storage).unwrap();
        assert_eq!(restored.search(&[1.0, 0.0], 10), vec!["second"]);
    }

    #[test]
    fn corrupted_memory_index_snapshot_falls_back_to_full_rebuild() {
        let directory = tempfile::tempdir().unwrap();
        let database_path = directory.path().join("memory.db");
        let database_path = database_path.to_str().unwrap();
        let storage = SqliteStorage::open_with_key(database_path, "corrupt-test-key").unwrap();
        insert_embedding(&storage, "survivor", &[1.0, 0.0]);
        std::fs::write(
            directory.path().join(MEMORY_INDEX_SNAPSHOT_FILE),
            b"not a snapshot",
        )
        .unwrap();

        let mut manager = MemoryManager::new();
        manager.rebuild_index(&storage).unwrap();
        assert_eq!(manager.search(&[1.0, 0.0], 1), vec!["survivor"]);
        MemoryManager::load_snapshot(&directory.path().join(MEMORY_INDEX_SNAPSHOT_FILE)).unwrap();
    }

    struct TaskDropGuard {
        dropped_tx: Option<tokio::sync::oneshot::Sender<()>>,
        drop_counter: std::sync::Arc<std::sync::atomic::AtomicUsize>,
    }

    impl Drop for TaskDropGuard {
        fn drop(&mut self) {
            self.drop_counter
                .fetch_add(1, std::sync::atomic::Ordering::SeqCst);
            if let Some(tx) = self.dropped_tx.take() {
                let _ = tx.send(());
            }
        }
    }

    async fn assert_drop_aborts_tasks_for_count(task_count: usize) {
        let mut manager = MemoryManager::new();
        let drop_counter = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));

        if task_count == 0 {
            // Case 1: Empty manager — drop must be a safe, clean no-op.
            drop(manager);
            assert_eq!(
                drop_counter.load(std::sync::atomic::Ordering::SeqCst),
                0,
                "empty manager drop must not invoke task drop guards"
            );
            return;
        }

        let mut dropped_rxs = Vec::with_capacity(task_count);

        for index in 0..task_count {
            let (started_tx, started_rx) = tokio::sync::oneshot::channel();
            let (dropped_tx, dropped_rx) = tokio::sync::oneshot::channel();
            dropped_rxs.push(dropped_rx);

            let guard = TaskDropGuard {
                dropped_tx: Some(dropped_tx),
                drop_counter: std::sync::Arc::clone(&drop_counter),
            };

            let handle = tokio::spawn(async move {
                let _guard = guard;
                let _ = started_tx.send(());
                std::future::pending::<()>().await;
            });

            started_rx
                .await
                .unwrap_or_else(|_| panic!("task {index} failed to signal started"));
            manager.push_task_for_test(handle);
        }

        // Verify that before drop(manager) is called, no drop guards have executed.
        assert_eq!(
            drop_counter.load(std::sync::atomic::Ordering::SeqCst),
            0,
            "no task drop guards should be invoked before manager drop"
        );

        // Drop the manager: Drop implementation must abort all active tasks.
        drop(manager);

        // Await dropped signal for every started task with a bounded timeout guard:
        for (index, dropped_rx) in dropped_rxs.into_iter().enumerate() {
            tokio::time::timeout(std::time::Duration::from_secs(5), dropped_rx)
                .await
                .unwrap_or_else(|_| {
                    panic!(
                        "task {index}/{task_count} drop guard was not triggered within 5s timeout guard"
                    )
                })
                .unwrap_or_else(|_| {
                    panic!("task {index}/{task_count} dropped channel sender dropped without sending")
                });
        }

        assert_eq!(
            drop_counter.load(std::sync::atomic::Ordering::SeqCst),
            task_count,
            "every started task guard must be destroyed exactly once"
        );
    }

    #[tokio::test(flavor = "multi_thread", worker_threads = 2)]
    async fn memory_manager_drop_aborts_tasks() {
        // Subcase 1: Empty manager drops cleanly without tasks.
        assert_drop_aborts_tasks_for_count(0).await;

        // Subcase 2: Single task is aborted on Drop; guard dropped exactly once.
        assert_drop_aborts_tasks_for_count(1).await;

        // Subcase 3: Multiple tasks are all aborted on Drop; all guards dropped exactly once.
        assert_drop_aborts_tasks_for_count(5).await;
    }
}
