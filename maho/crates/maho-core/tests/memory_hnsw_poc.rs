use instant_distance::{Builder, Search};
use std::time::Instant;

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

#[test]
fn test_hnsw_benchmark() {
    let num_vectors = 1000;
    let dim = 384;

    // Generate random mock embeddings
    let mut points = Vec::with_capacity(num_vectors);
    let mut ids = Vec::with_capacity(num_vectors);
    for i in 0..num_vectors {
        let vec: Vec<f32> = (0..dim).map(|val| (val as f32 + i as f32).sin()).collect();
        points.push(Embedding(vec));
        ids.push(format!("fact_{}", i));
    }

    // Measure build time
    let start_build = Instant::now();
    let hnsw = Builder::default().build(points, ids);
    let build_duration = start_build.elapsed();
    println!(
        "HNSW Build duration for {} vectors: {:?}",
        num_vectors, build_duration
    );
    assert!(
        build_duration.as_millis() < 60000,
        "HNSW build took too long: {:?}",
        build_duration
    );

    // Search top 5
    let query = Embedding((0..dim).map(|val| (val as f32).cos()).collect());
    let mut search = Search::default();

    let start_search = Instant::now();
    let results: Vec<_> = hnsw.search(&query, &mut search).take(5).collect();
    let search_duration = start_search.elapsed();
    println!("HNSW Search duration for top 5: {:?}", search_duration);

    assert!(
        search_duration.as_millis() < 50,
        "HNSW search took too long: {:?}",
        search_duration
    );
    assert_eq!(results.len(), 5);
}
