use std::sync::Arc;

use maho_core::error::CoreError;
use maho_core::memory::episodic::{
    canonical_origin, decide, forget_origin, is_sensitive_host, record_episode, EpisodeDecision,
    EpisodeInput, SkipReason,
};
use maho_core::memory::{EmbeddingEngine, EmbeddingModelDescriptor, MemoryEngine};
use maho_storage::sqlite::SqliteStorage;
use maho_types::settings::EpisodicMemorySettings;

struct NoModel;
impl EmbeddingEngine for NoModel {
    fn descriptor(&self) -> EmbeddingModelDescriptor {
        EmbeddingModelDescriptor {
            model_id: "none".into(),
            model_version: "0".into(),
            model_sha256: None,
        }
    }
    fn embed_single(&self, _: &str) -> Result<Vec<f32>, CoreError> {
        Err(CoreError::EmbeddingUnavailable("test".into()))
    }
    fn embed_batch(&self, _: &[String]) -> Result<Vec<Vec<f32>>, CoreError> {
        Err(CoreError::EmbeddingUnavailable("test".into()))
    }
}

fn enabled() -> EpisodicMemorySettings {
    EpisodicMemorySettings {
        enabled: true,
        ..Default::default()
    }
}

fn visit(url: &str) -> EpisodeInput<'_> {
    EpisodeInput {
        url,
        title: "Sourdough starter guide",
        summary: "Feed the starter twice a day with equal flour and water",
        is_private_context: false,
    }
}

fn stored(storage: &SqliteStorage) -> usize {
    storage
        .load_all_memories()
        .unwrap()
        .iter()
        .filter(|m| m.2 == "browse")
        .count()
}

#[test]
fn defaults_are_off_and_old_settings_json_still_loads() {
    assert!(!EpisodicMemorySettings::default().enabled);
    let settings: EpisodicMemorySettings = serde_json::from_str("{}").unwrap();
    assert_eq!(settings, EpisodicMemorySettings::default());
}

#[test]
fn every_gate_stores_nothing() {
    let storage = SqliteStorage::open_in_memory_with_key("episodic-gates").unwrap();
    let cases: Vec<(EpisodicMemorySettings, EpisodeInput, SkipReason)> = vec![
        (
            EpisodicMemorySettings::default(),
            visit("https://bread.example/guide"),
            SkipReason::Disabled,
        ),
        (
            EpisodicMemorySettings {
                paused: true,
                ..enabled()
            },
            visit("https://bread.example/guide"),
            SkipReason::Paused,
        ),
        (
            enabled(),
            EpisodeInput {
                is_private_context: true,
                ..visit("https://bread.example/guide")
            },
            SkipReason::PrivateContext,
        ),
        (
            enabled(),
            visit("chrome://settings"),
            SkipReason::NotWebPage,
        ),
        (
            enabled(),
            visit("file:///home/user/notes.txt"),
            SkipReason::NotWebPage,
        ),
        (
            EpisodicMemorySettings {
                excluded_domains: vec!["example".into(), "*.bread.example".into()],
                ..enabled()
            },
            visit("https://www.bread.example/guide"),
            SkipReason::ExcludedDomain,
        ),
        (
            enabled(),
            visit("https://www.chase.com/accounts"),
            SkipReason::SensitiveDomain,
        ),
        (
            enabled(),
            visit("https://accounts.google.com/signin"),
            SkipReason::SensitiveDomain,
        ),
        (
            enabled(),
            visit("https://mybank.example/"),
            SkipReason::SensitiveDomain,
        ),
        (
            enabled(),
            visit("https://shop.example/checkout"),
            SkipReason::Empty,
        ),
    ];
    for (settings, input, expected) in cases {
        let input = if expected == SkipReason::Empty {
            EpisodeInput {
                title: " ",
                summary: "",
                ..input
            }
        } else {
            input
        };
        assert_eq!(
            decide(&settings, &input),
            EpisodeDecision::Skip(expected),
            "{}",
            input.url
        );
        assert_eq!(
            record_episode(&storage, &settings, &input).unwrap(),
            Err(expected)
        );
    }
    assert_eq!(stored(&storage), 0);
}

#[test]
fn sensitive_classification_is_label_based_not_substring() {
    assert!(is_sensitive_host("online.wellsfargo.com"));
    assert!(is_sensitive_host("pay.example.com"));
    assert!(is_sensitive_host("secure-login.example.org"));
    assert!(is_sensitive_host("www.hometax.go.kr"));
    assert!(is_sensitive_host("mybank.example"));
    assert!(is_sensitive_host("www.citibank.com"));
    assert!(!is_sensitive_host("paydirt-novels.example"));
    assert!(!is_sensitive_host("banksy.example"));
    assert!(!is_sensitive_host("docs.rs"));
    assert!(!is_sensitive_host("news.ycombinator.com"));
}

#[test]
fn enabled_episode_is_stored_and_found_by_hybrid_search() {
    let storage = SqliteStorage::open_in_memory_with_key("episodic-store").unwrap();
    let id = record_episode(
        &storage,
        &enabled(),
        &visit("https://bread.example/guide?ref=1"),
    )
    .unwrap()
    .unwrap();
    let again = record_episode(
        &storage,
        &enabled(),
        &visit("https://bread.example/guide?ref=1"),
    )
    .unwrap()
    .unwrap();
    assert_eq!(id, again, "the same visit is stored once");
    assert_eq!(stored(&storage), 1);

    let record = storage
        .load_all_memories()
        .unwrap()
        .into_iter()
        .find(|m| m.0 == id)
        .unwrap();
    let categories: Vec<String> = serde_json::from_str(&record.4).unwrap();
    assert_eq!(categories, ["episode", "https://bread.example"]);

    let mut engine = MemoryEngine::with_embedding_engine(Arc::new(NoModel));
    engine.rebuild_index(&storage).unwrap();
    let results = engine.search_hybrid("sourdough starter", &storage, 5);
    assert!(results
        .iter()
        .any(|r| r.fact.contains("Sourdough starter guide")));
}

#[test]
fn forget_site_deletes_every_episode_for_that_origin_only() {
    let storage = SqliteStorage::open_in_memory_with_key("episodic-forget").unwrap();
    for url in [
        "https://bread.example/a",
        "https://bread.example/b",
        "https://BREAD.example:443/c",
        "http://bread.example/insecure",
        "https://other.example/a",
    ] {
        record_episode(&storage, &enabled(), &visit(url))
            .unwrap()
            .unwrap();
    }
    assert_eq!(stored(&storage), 5);

    assert_eq!(
        forget_origin(&storage, "https://bread.example/any/path").unwrap(),
        3
    );
    let remaining: Vec<String> = storage
        .load_all_memories()
        .unwrap()
        .into_iter()
        .filter(|m| m.2 == "browse")
        .map(|m| m.6.unwrap())
        .collect();
    assert_eq!(remaining.len(), 2);
    assert!(remaining
        .iter()
        .all(|meta| !meta.contains("\"origin\":\"https://bread.example\"")));
    assert!(storage.search_memories_fts("starter", 10).unwrap().len() == 2);
    assert_eq!(forget_origin(&storage, "https://bread.example").unwrap(), 0);
    assert!(forget_origin(&storage, "not a url").is_err());
}

#[test]
fn canonical_origin_normalizes_case_and_default_ports() {
    assert_eq!(
        canonical_origin("HTTPS://Bread.Example:443/x").unwrap(),
        "https://bread.example"
    );
    assert_eq!(
        canonical_origin("http://bread.example:8080/").unwrap(),
        "http://bread.example:8080"
    );
    assert_eq!(canonical_origin("ftp://bread.example"), None);
}
