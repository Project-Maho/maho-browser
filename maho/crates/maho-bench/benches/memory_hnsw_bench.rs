use criterion::{criterion_group, criterion_main, BatchSize, BenchmarkId, Criterion};
use instant_distance::{Builder, Point, Search};
use std::time::Duration;

/// Wrapper for instant-distance Point trait matching maho-core's Embedding representation
#[derive(Clone, Debug)]
pub struct Embedding(pub Vec<f32>);

impl Point for Embedding {
    fn distance(&self, other: &Self) -> f32 {
        let dot: f32 = self.0.iter().zip(&other.0).map(|(a, b)| a * b).sum();
        let na: f32 = self.0.iter().map(|v| v * v).sum::<f32>().sqrt();
        let nb: f32 = other.0.iter().map(|v| v * v).sum::<f32>().sqrt();
        1.0 - (dot / (na * nb + 1e-8))
    }
}

/// Generates N deterministic synthetic fact embeddings of dimension `dim`.
fn generate_synthetic_embeddings(n: usize, dim: usize) -> (Vec<Embedding>, Vec<String>) {
    let mut points = Vec::with_capacity(n);
    let mut ids = Vec::with_capacity(n);
    for i in 0..n {
        let vec: Vec<f32> = (0..dim)
            .map(|d| ((d as f32 * 0.037) + (i as f32 * 0.013)).sin())
            .collect();
        points.push(Embedding(vec));
        ids.push(format!("fact_{i}"));
    }
    (points, ids)
}

fn bench_hnsw_build_384d(c: &mut Criterion) {
    let mut group = c.benchmark_group("memory_hnsw_build_384d");
    group.sample_size(10);
    group.measurement_time(Duration::from_secs(5));

    for n in [100, 1000, 5000] {
        let (points, ids) = generate_synthetic_embeddings(n, 384);
        group.bench_with_input(BenchmarkId::from_parameter(n), &n, |b, _| {
            b.iter_batched(
                || (points.clone(), ids.clone()),
                |(pts, id_list)| {
                    let hnsw = Builder::default().build(pts, id_list);
                    std::hint::black_box(hnsw);
                },
                BatchSize::LargeInput,
            );
        });
    }
    group.finish();
}

fn bench_hnsw_build_1536d(c: &mut Criterion) {
    let mut group = c.benchmark_group("memory_hnsw_build_1536d");
    group.sample_size(10);
    group.measurement_time(Duration::from_secs(5));

    for n in [100, 1000, 5000] {
        let (points, ids) = generate_synthetic_embeddings(n, 1536);
        group.bench_with_input(BenchmarkId::from_parameter(n), &n, |b, _| {
            b.iter_batched(
                || (points.clone(), ids.clone()),
                |(pts, id_list)| {
                    let hnsw = Builder::default().build(pts, id_list);
                    std::hint::black_box(hnsw);
                },
                BatchSize::LargeInput,
            );
        });
    }
    group.finish();
}

fn bench_hnsw_search_384d(c: &mut Criterion) {
    let mut group = c.benchmark_group("memory_hnsw_search_384d");

    for n in [100, 1000, 5000] {
        let (points, ids) = generate_synthetic_embeddings(n, 384);
        let hnsw = Builder::default().build(points, ids);
        let query = Embedding((0..384).map(|d| (d as f32 * 0.05).cos()).collect());

        group.bench_with_input(BenchmarkId::from_parameter(n), &n, |b, _| {
            b.iter(|| {
                let mut search = Search::default();
                let results: Vec<_> = hnsw.search(&query, &mut search).take(5).collect();
                std::hint::black_box(results);
            });
        });
    }
    group.finish();
}

fn bench_hnsw_search_1536d(c: &mut Criterion) {
    let mut group = c.benchmark_group("memory_hnsw_search_1536d");

    for n in [100, 1000, 5000] {
        let (points, ids) = generate_synthetic_embeddings(n, 1536);
        let hnsw = Builder::default().build(points, ids);
        let query = Embedding((0..1536).map(|d| (d as f32 * 0.05).cos()).collect());

        group.bench_with_input(BenchmarkId::from_parameter(n), &n, |b, _| {
            b.iter(|| {
                let mut search = Search::default();
                let results: Vec<_> = hnsw.search(&query, &mut search).take(5).collect();
                std::hint::black_box(results);
            });
        });
    }
    group.finish();
}

criterion_group!(
    benches,
    bench_hnsw_build_384d,
    bench_hnsw_build_1536d,
    bench_hnsw_search_384d,
    bench_hnsw_search_1536d,
);
criterion_main!(benches);
