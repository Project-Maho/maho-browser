use std::path::PathBuf;
use std::sync::Arc;

use maho_core::embedding::{self, EmbeddingRegistry, LocalEmbedder, EMBED_DIM};
use maho_core::error::CoreError;
use maho_core::memory::{DefaultEmbeddingEngine, EmbeddingEngine, MemoryEngine};
use maho_storage::sqlite::{MemoryInsertParams, SqliteStorage};

fn model_dir() -> PathBuf {
    PathBuf::from(std::env::var_os("MAHO_TEST_EMBEDDING_MODEL_DIR").expect(
        "MAHO_TEST_EMBEDDING_MODEL_DIR must point at the multilingual-e5-small files \
         (config.json, tokenizer.json, model.safetensors)",
    ))
}

fn loaded() -> &'static LocalEmbedder {
    static MODEL: std::sync::OnceLock<LocalEmbedder> = std::sync::OnceLock::new();
    MODEL.get_or_init(|| LocalEmbedder::load(&model_dir()).expect("model loads"))
}

fn cosine(a: &[f32], b: &[f32]) -> f32 {
    a.iter().zip(b).map(|(x, y)| x * y).sum()
}

#[test]
fn vectors_are_normalized_and_never_zero() {
    let vectors = loaded()
        .embed(&[
            "hello".to_string(),
            "".to_string(),
            "안녕하세요".to_string(),
        ])
        .unwrap();
    assert_eq!(vectors.len(), 3);
    for v in &vectors {
        assert_eq!(v.len(), EMBED_DIM);
        let norm: f32 = v.iter().map(|x| x * x).sum::<f32>().sqrt();
        assert!((norm - 1.0).abs() < 1e-3, "norm {norm}");
        assert!(v.iter().any(|x| *x != 0.0));
    }
}

#[test]
fn semantic_ordering_holds_across_languages() {
    let texts: Vec<String> = [
        "The user commutes to work by bicycle",
        "사용자는 자전거로 출근한다",
        "How do I ride a bike to the office?",
        "The quarterly tax filing deadline is in March",
    ]
    .iter()
    .map(|s| s.to_string())
    .collect();
    let v = loaded().embed(&texts).unwrap();
    let english_korean = cosine(&v[0], &v[1]);
    let english_question = cosine(&v[0], &v[2]);
    let english_tax = cosine(&v[0], &v[3]);
    assert!(
        english_korean > english_tax && english_question > english_tax,
        "translation {english_korean}, paraphrase {english_question}, unrelated {english_tax}"
    );
}

#[test]
fn batch_and_single_embeddings_agree() {
    let texts = vec![
        "short".to_string(),
        "a much longer sentence that pads the batch".to_string(),
    ];
    let batch = loaded().embed(&texts).unwrap();
    let single = loaded().embed(&texts[..1]).unwrap();
    assert!(cosine(&batch[0], &single[0]) > 0.999);
}

#[test]
fn missing_model_is_typed_unavailable() {
    let empty = tempfile::tempdir().unwrap();
    assert!(matches!(
        LocalEmbedder::load(empty.path()),
        Err(CoreError::EmbeddingUnavailable(_))
    ));
    let registry = EmbeddingRegistry::default();
    assert!(!registry.is_available());
    assert!(matches!(
        registry.embed(&["x".to_string()]),
        Err(CoreError::EmbeddingUnavailable(_))
    ));
}

#[test]
fn retrieval_falls_back_to_lexical_when_model_is_unavailable() {
    assert!(!embedding::registry().is_available());
    let storage = SqliteStorage::open_in_memory_with_key("embedding-test-key").unwrap();
    storage
        .insert_memory(MemoryInsertParams {
            id: "fact-1",
            fact: "The user commutes by bicycle",
            source: "user",
            session_id: None,
            categories: "[]",
            importance: 0.8,
            metadata: None,
        })
        .unwrap();
    let engine: Arc<dyn EmbeddingEngine> = Arc::new(DefaultEmbeddingEngine);
    assert!(matches!(
        engine.embed_single("bicycle"),
        Err(CoreError::EmbeddingUnavailable(_))
    ));
    let mut memory = MemoryEngine::with_embedding_engine(engine);
    memory.rebuild_index(&storage).unwrap();
    let results = memory.search_hybrid("bicycle", &storage, 5);
    assert_eq!(results.len(), 1);
    assert_eq!(results[0].fact, "The user commutes by bicycle");
}
