use maho_core::{
    embedding::{vec_to_bytes, EMBED_DIM},
    error::CoreError,
    memory::{EmbeddingEngine, EmbeddingModelDescriptor, MemoryEngine},
};
use maho_storage::sqlite::{MemoryInsertParams, SqliteStorage};
use std::sync::Arc;
struct FixedEmbedding;
impl EmbeddingEngine for FixedEmbedding {
    fn descriptor(&self) -> EmbeddingModelDescriptor {
        EmbeddingModelDescriptor {
            model_id: "test".into(),
            model_version: "1".into(),
            model_sha256: None,
        }
    }
    fn embed_single(&self, _: &str) -> Result<Vec<f32>, CoreError> {
        Ok(vec![1.0; EMBED_DIM])
    }
    fn embed_batch(&self, texts: &[String]) -> Result<Vec<Vec<f32>>, CoreError> {
        texts.iter().map(|t| self.embed_single(t)).collect()
    }
}
#[test]
fn dense_only_hit_returns_fact_not_identifier() {
    let storage = SqliteStorage::open_in_memory_with_key("review-memory-key").unwrap();
    let fact = "The user commutes by bicycle";
    storage
        .insert_memory(MemoryInsertParams {
            id: "opaque-id",
            fact,
            source: "user",
            session_id: None,
            categories: "[]",
            importance: 0.8,
            metadata: None,
        })
        .unwrap();
    storage
        .update_memory_embedding("opaque-id", &vec_to_bytes(&vec![1.0; EMBED_DIM]))
        .unwrap();
    let mut engine = MemoryEngine::with_embedding_engine(Arc::new(FixedEmbedding));
    engine.rebuild_index(&storage).unwrap();
    let results = engine.search_hybrid("transportation", &storage, 5);
    assert_eq!(results.len(), 1);
    assert_eq!(results[0].lexical_rank, None);
    assert_eq!(results[0].fact, fact);
    assert!(engine
        .retriever
        .format_prompt_block(&results)
        .contains(fact));
}
