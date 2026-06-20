//! Memory feature backend: extracts facts from chat sessions, embeds them,
//! retrieves them as AI context. Operates as a sub-manager owned by MahoCore.

use std::sync::OnceLock;
use tokio::runtime::Runtime;
use tokio::sync::mpsc;
use tokio::task::JoinHandle;
use serde::{Serialize, Deserialize};
use instant_distance::{Builder, HnswMap, Search};

use crate::embedding;
use crate::llm_client;
use maho_storage::sqlite::SqliteStorage;

pub(crate) fn get_runtime() -> &'static Runtime {
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
fn default_importance() -> f32 { 0.5 }

#[derive(Debug)]
pub enum MemoryResult {
    FactsExtracted { session_id: String, facts: Vec<RawFact> },
    EmbeddingsReady { fact_id: String, embedding: Vec<f32> },
    BrowsingSummaryReady { content: String },
    BrowsingSummaryDue,
    Failed { reason: String },
}

/// Wrapper for instant-distance Point trait
#[derive(Clone, Debug)]
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

pub struct MemoryManager {
    /// In-memory ANN index (fact_id keyed)
    hnsw: Option<HnswMap<Embedding, String>>,
    result_rx: mpsc::UnboundedReceiver<MemoryResult>,
    result_tx: mpsc::UnboundedSender<MemoryResult>,
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
        let (tx, rx) = mpsc::unbounded_channel();
        Self {
            hnsw: None,
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

    pub fn start_browsing_summary_loop(&self) {
        let tx = self.result_tx.clone();
        get_runtime().spawn(async move {
            let mut ticker = tokio::time::interval(std::time::Duration::from_secs(3600));
            ticker.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Delay);
            ticker.tick().await; // skip immediate first tick at t=0
            loop {
                ticker.tick().await;
                let _ = tx.send(MemoryResult::BrowsingSummaryDue);
            }
        });
    }

    pub fn note_summary_completed(&mut self, at: chrono::DateTime<chrono::Utc>) {
        self.last_summary_at = Some(at);
    }

    /// Build HNSW from all stored embeddings. Call at MahoCore startup.
    pub fn rebuild_index(&mut self, storage: &SqliteStorage) -> Result<(), crate::error::CoreError> {
        let entries = storage.load_all_active_memory_embeddings()
            .map_err(|e| crate::error::CoreError::Storage(e.to_string()))?;
        if entries.is_empty() {
            self.hnsw = None;
            return Ok(());
        }
        let mut ids = Vec::new();
        let mut vecs = Vec::new();
        for (id, bytes) in entries {
            if let Ok(vec) = crate::embedding::bytes_to_vec(&bytes) {
                ids.push(id);
                vecs.push(Embedding(vec));
            }
        }
        if vecs.is_empty() {
            self.hnsw = None;
            return Ok(());
        }
        let hnsw = Builder::default().build(vecs, ids);
        self.hnsw = Some(hnsw);
        Ok(())
    }

    /// Top-K semantic search.
    pub fn search(&self, query_embedding: &[f32], top_k: usize) -> Vec<String> {
        let Some(ref hnsw) = self.hnsw else { return Vec::new(); };
        let q = Embedding(query_embedding.to_vec());
        let mut search = Search::default();
        hnsw.search(&q, &mut search)
            .take(top_k)
            .map(|item| item.value.clone())
            .collect()
    }

    /// Triggered at session-end. Sync method spawns an async extraction.
    pub fn extract_from_session(&mut self, session_id: String, raw_text: String) {
        let Some(auth) = self.auth.clone() else { return; };
        let tx = self.result_tx.clone();
        let h = get_runtime().spawn(async move {
            match llm_client::extract_facts_json(&auth, &raw_text).await {
                Ok(facts) => { let _ = tx.send(MemoryResult::FactsExtracted { session_id, facts }); }
                Err(e)    => { let _ = tx.send(MemoryResult::Failed { reason: format!("extract: {e}") }); }
            }
        });
        self.active_tasks.push(h);
    }

    /// Triggered for nightly browsing summary.
    pub fn summarize_browsing(&mut self, raw_text: String) {
        let Some(auth) = self.auth.clone() else { return; };
        let tx = self.result_tx.clone();
        let h = get_runtime().spawn(async move {
            match llm_client::summarize_browsing(&auth, &raw_text).await {
                Ok(content) => { let _ = tx.send(MemoryResult::BrowsingSummaryReady { content }); }
                Err(e)      => { let _ = tx.send(MemoryResult::Failed { reason: format!("summary: {e}") }); }
            }
        });
        self.active_tasks.push(h);
    }

    /// Spawn embedding job for a fact.
    pub fn embed_fact(&mut self, fact_id: String, content: String) {
        let tx = self.result_tx.clone();
        let h = get_runtime().spawn(async move {
            let result = tokio::task::spawn_blocking(move || {
                embedding::embed(&[content])
            }).await;
            match result {
                Ok(Ok(mut vecs)) if !vecs.is_empty() => {
                    let _ = tx.send(MemoryResult::EmbeddingsReady { fact_id, embedding: vecs.remove(0) });
                }
                Ok(Err(e)) => { let _ = tx.send(MemoryResult::Failed { reason: format!("embed: {e}") }); }
                Err(e)     => { let _ = tx.send(MemoryResult::Failed { reason: format!("embed join: {e}") }); }
                _ => { let _ = tx.send(MemoryResult::Failed { reason: "empty embedding".into() }); }
            }
        });
        self.active_tasks.push(h);
    }

    /// Drain results — called by MahoCore each tick. Caller persists via storage.
    pub fn drain_results(&mut self) -> Vec<MemoryResult> {
        let mut out = Vec::new();
        while let Ok(r) = self.result_rx.try_recv() { out.push(r); }
        self.active_tasks.retain(|h| !h.is_finished());
        out
    }
}
