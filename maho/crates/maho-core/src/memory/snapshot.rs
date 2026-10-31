//! Embedding-generation-aware HNSW index snapshots (Snapshot v2).

use bincode::Options;
use instant_distance::{HnswMap, Point};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::Path;

use crate::embedding::EMBED_DIM;
use crate::error::CoreError;

pub const DEFAULT_MODEL_ID: &str = "all-MiniLM-L6-v2";
pub const DEFAULT_MODEL_VERSION: &str = "1.0.0";
pub const MEMORY_INDEX_SNAPSHOT_FILE: &str = "memory_index_snapshot.bin";
pub const MEMORY_INDEX_SNAPSHOT_VERSION_V1: u32 = 1;
pub const MEMORY_INDEX_SNAPSHOT_VERSION_V2: u32 = 2;
pub const MEMORY_INDEX_SNAPSHOT_LIMIT: u64 = 256 * 1024 * 1024;
pub const MEMORY_INDEX_MAX_SEGMENTS: usize = 32;

/// Compute cosine similarity between two float slices.
pub fn cosine_similarity(a: &[f32], b: &[f32]) -> f32 {
    let dot: f32 = a.iter().zip(b.iter()).map(|(x, y)| x * y).sum();
    let norm_a: f32 = a.iter().map(|x| x * x).sum::<f32>().sqrt();
    let norm_b: f32 = b.iter().map(|x| x * x).sum::<f32>().sqrt();
    if norm_a == 0.0 || norm_b == 0.0 {
        0.0
    } else {
        dot / (norm_a * norm_b)
    }
}

/// Wrapper for instant-distance Point trait with cosine distance.
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Embedding(pub Vec<f32>);

impl Point for Embedding {
    fn distance(&self, other: &Self) -> f32 {
        let sim = cosine_similarity(&self.0, &other.0);
        (1.0 - sim).max(0.0)
    }
}

/// Version 2 Snapshot format with full model and generation metadata.
#[derive(Serialize, Deserialize)]
pub struct MemoryIndexSnapshot {
    pub version: u32,
    pub embedding_model_id: String,
    pub embedding_model_version: String,
    pub embedding_model_sha256: Option<String>,
    pub dimensions: usize,
    pub normalization_version: u32,
    pub generation_id: String,
    pub last_change_sequence: i64,
    pub segments: Vec<HnswMap<Embedding, String>>,
    pub active_segments: HashMap<String, usize>,
}

#[derive(Serialize)]
pub struct MemoryIndexSnapshotRef<'a> {
    pub version: u32,
    pub embedding_model_id: &'a str,
    pub embedding_model_version: &'a str,
    pub embedding_model_sha256: Option<&'a str>,
    pub dimensions: usize,
    pub normalization_version: u32,
    pub generation_id: &'a str,
    pub last_change_sequence: i64,
    pub segments: &'a [HnswMap<Embedding, String>],
    pub active_segments: &'a HashMap<String, usize>,
}

/// Legacy Version 1 snapshot for backward-compatible deserialization.
#[derive(Deserialize)]
struct LegacyV1Snapshot {
    version: u32,
    last_change_sequence: i64,
    segments: Vec<HnswMap<Embedding, String>>,
    active_segments: HashMap<String, usize>,
}

/// Load snapshot from disk, supporting both V2 and fallback from V1.
pub fn load_snapshot(path: &Path) -> Result<MemoryIndexSnapshot, CoreError> {
    let bytes = std::fs::read(path).map_err(|e| CoreError::Storage(e.to_string()))?;

    // First try V2 deserialization
    if let Ok(v2) = bincode::DefaultOptions::new()
        .with_limit(MEMORY_INDEX_SNAPSHOT_LIMIT)
        .deserialize::<MemoryIndexSnapshot>(&bytes)
    {
        if v2.version == MEMORY_INDEX_SNAPSHOT_VERSION_V2
            && v2.last_change_sequence >= 0
            && v2.dimensions == EMBED_DIM
            && !v2.active_segments.values().any(|s| *s >= v2.segments.len())
        {
            return Ok(v2);
        }
    }

    // Fall back to legacy V1 format
    if let Ok(v1) = bincode::DefaultOptions::new()
        .with_limit(MEMORY_INDEX_SNAPSHOT_LIMIT)
        .deserialize::<LegacyV1Snapshot>(&bytes)
    {
        if v1.version == MEMORY_INDEX_SNAPSHOT_VERSION_V1
            && v1.last_change_sequence >= 0
            && !v1.active_segments.values().any(|s| *s >= v1.segments.len())
        {
            return Ok(MemoryIndexSnapshot {
                version: MEMORY_INDEX_SNAPSHOT_VERSION_V2,
                embedding_model_id: DEFAULT_MODEL_ID.to_string(),
                embedding_model_version: DEFAULT_MODEL_VERSION.to_string(),
                embedding_model_sha256: None,
                dimensions: EMBED_DIM,
                normalization_version: 1,
                generation_id: "legacy_v1".to_string(),
                last_change_sequence: v1.last_change_sequence,
                segments: v1.segments,
                active_segments: v1.active_segments,
            });
        }
    }

    Err(CoreError::Storage(
        "invalid or corrupted memory index snapshot".into(),
    ))
}

/// Save snapshot atomically using a temporary file.
pub fn save_snapshot(
    path: &Path,
    model_id: &str,
    model_version: &str,
    model_sha256: Option<&str>,
    generation_id: &str,
    last_change_sequence: i64,
    segments: &[HnswMap<Embedding, String>],
    active_segments: &HashMap<String, usize>,
) -> Result<(), CoreError> {
    let snapshot = MemoryIndexSnapshotRef {
        version: MEMORY_INDEX_SNAPSHOT_VERSION_V2,
        embedding_model_id: model_id,
        embedding_model_version: model_version,
        embedding_model_sha256: model_sha256,
        dimensions: EMBED_DIM,
        normalization_version: 1,
        generation_id,
        last_change_sequence,
        segments,
        active_segments,
    };
    let bytes = bincode::DefaultOptions::new()
        .with_limit(MEMORY_INDEX_SNAPSHOT_LIMIT)
        .serialize(&snapshot)
        .map_err(|e| CoreError::Storage(e.to_string()))?;
    let temp_path = path.with_extension("bin.tmp");
    std::fs::write(&temp_path, bytes)
        .and_then(|_| std::fs::rename(&temp_path, path))
        .map_err(|e| CoreError::Storage(e.to_string()))
}
