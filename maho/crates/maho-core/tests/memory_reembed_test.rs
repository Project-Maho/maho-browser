use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;

use maho_core::embedding::{bytes_to_vec, vec_to_bytes, EMBED_DIM};
use maho_core::error::CoreError;
use maho_core::memory::{EmbeddingEngine, EmbeddingModelDescriptor, MemoryEngine};
use maho_storage::sqlite::{MemoryInsertParams, SqliteStorage};

struct CountingEngine {
    calls: AtomicUsize,
    fail_after_calls: Option<usize>,
}

impl EmbeddingEngine for CountingEngine {
    fn descriptor(&self) -> EmbeddingModelDescriptor {
        EmbeddingModelDescriptor {
            model_id: "test-model".into(),
            model_version: "2".into(),
            model_sha256: None,
        }
    }
    fn embed_single(&self, text: &str) -> Result<Vec<f32>, CoreError> {
        let mut v = vec![0.0; EMBED_DIM];
        v[text.len() % EMBED_DIM] = 1.0;
        Ok(v)
    }
    fn embed_batch(&self, texts: &[String]) -> Result<Vec<Vec<f32>>, CoreError> {
        let call = self.calls.fetch_add(1, Ordering::SeqCst);
        if self.fail_after_calls.is_some_and(|n| call >= n) {
            return Err(CoreError::EmbeddingUnavailable("interrupted".into()));
        }
        texts.iter().map(|t| self.embed_single(t)).collect()
    }
}

fn seed(storage: &SqliteStorage, count: usize) {
    for i in 0..count {
        let id = format!("fact-{i:03}");
        storage
            .insert_memory(MemoryInsertParams {
                id: &id,
                fact: &format!("remembered fact number {i}"),
                source: "user",
                session_id: None,
                categories: "[]",
                importance: 0.5,
                metadata: None,
            })
            .unwrap();
        storage
            .update_memory_embedding(&id, &vec_to_bytes(&vec![0.0; EMBED_DIM]))
            .unwrap();
    }
}

fn engine(fail_after_calls: Option<usize>) -> (MemoryEngine, Arc<CountingEngine>) {
    let counting = Arc::new(CountingEngine {
        calls: AtomicUsize::new(0),
        fail_after_calls,
    });
    (
        MemoryEngine::with_embedding_engine(counting.clone()),
        counting,
    )
}

fn drain(engine: &mut MemoryEngine, storage: &SqliteStorage) {
    for _ in 0..20 {
        if engine.step_maintenance(storage).unwrap() == 0 {
            break;
        }
    }
}

fn zero_vectors(storage: &SqliteStorage) -> usize {
    let (_, latest) = storage.load_memory_embeddings_since(0).unwrap();
    assert_eq!(latest.len(), storage.load_all_memories().unwrap().len());
    latest
        .into_iter()
        .filter(|(_, bytes)| {
            bytes
                .as_deref()
                .map_or(true, |b| bytes_to_vec(b).unwrap().iter().all(|x| *x == 0.0))
        })
        .count()
}

#[test]
fn legacy_zero_vectors_are_all_reembedded_in_batches() {
    let storage = SqliteStorage::open_in_memory_with_key("reembed-key").unwrap();
    seed(&storage, 100);
    let (mut memory, counting) = engine(None);
    let generation = memory.generation_id().to_string();
    assert_eq!(
        storage
            .memory_facts_needing_embedding(&generation, 1000)
            .unwrap()
            .len(),
        100
    );

    assert!(memory.schedule_reembed_if_stale(&storage).unwrap());
    assert!(memory.schedule_reembed_if_stale(&storage).unwrap());
    assert_eq!(memory.queued_jobs.len(), 1, "scheduling is idempotent");
    drain(&mut memory, &storage);

    assert_eq!(
        counting.calls.load(Ordering::SeqCst),
        2,
        "100 facts in batches of 64"
    );
    assert_eq!(zero_vectors(&storage), 0);
    assert!(storage
        .memory_facts_needing_embedding(&generation, 1000)
        .unwrap()
        .is_empty());
    assert!(!memory.schedule_reembed_if_stale(&storage).unwrap());
}

#[test]
fn interrupted_rebuild_resumes_without_duplicates() {
    let storage = SqliteStorage::open_in_memory_with_key("reembed-resume-key").unwrap();
    seed(&storage, 100);
    let (mut first, _) = engine(Some(1));
    first.schedule_reembed_if_stale(&storage).unwrap();
    drain(&mut first, &storage);
    let generation = first.generation_id().to_string();
    let remaining = storage
        .memory_facts_needing_embedding(&generation, 1000)
        .unwrap();
    assert_eq!(
        remaining.len(),
        36,
        "the first batch of 64 persisted before the failure"
    );

    let (mut second, counting) = engine(None);
    second.schedule_reembed_if_stale(&storage).unwrap();
    drain(&mut second, &storage);
    assert_eq!(
        counting.calls.load(Ordering::SeqCst),
        1,
        "only the remaining 36 were embedded"
    );
    assert_eq!(zero_vectors(&storage), 0);
    assert_eq!(
        storage.load_all_memories().unwrap().len(),
        100,
        "no duplicate facts"
    );
}

#[test]
fn facts_from_another_generation_are_stale_and_current_ones_are_not() {
    let storage = SqliteStorage::open_in_memory_with_key("reembed-generation-key").unwrap();
    seed(&storage, 2);
    let mut one_hot = vec![0.0; EMBED_DIM];
    one_hot[0] = 1.0;
    storage
        .update_memory_embedding_with_model("fact-000", &vec_to_bytes(&one_hot), "old-model:1")
        .unwrap();
    storage
        .update_memory_embedding_with_model("fact-001", &vec_to_bytes(&one_hot), "test-model:2")
        .unwrap();
    let stale = storage
        .memory_facts_needing_embedding("test-model:2", 10)
        .unwrap();
    assert_eq!(stale.len(), 1);
    assert_eq!(stale[0].0, "fact-000");
}
