use criterion::{criterion_group, criterion_main, BenchmarkId, Criterion};

fn bench_yrs_insert(c: &mut Criterion) {
    let mut group = c.benchmark_group("yrs_insert");
    for n in [100, 500, 1000] {
        group.bench_with_input(BenchmarkId::from_parameter(n), &n, |b, &n| {
            b.iter(|| maho_bench::yrs_bench::insert_tab_metadata(n));
        });
    }
    group.finish();
}

fn bench_automerge_insert(c: &mut Criterion) {
    let mut group = c.benchmark_group("automerge_insert");
    for n in [100, 500, 1000] {
        group.bench_with_input(BenchmarkId::from_parameter(n), &n, |b, &n| {
            b.iter(|| maho_bench::automerge_bench::insert_tab_metadata(n));
        });
    }
    group.finish();
}

fn bench_yrs_merge(c: &mut Criterion) {
    let mut group = c.benchmark_group("yrs_merge");
    for n in [100, 500, 1000] {
        group.bench_with_input(BenchmarkId::from_parameter(n), &n, |b, &n| {
            b.iter(|| maho_bench::yrs_bench::merge_two_peers(n));
        });
    }
    group.finish();
}

fn bench_automerge_merge(c: &mut Criterion) {
    let mut group = c.benchmark_group("automerge_merge");
    for n in [100, 500, 1000] {
        group.bench_with_input(BenchmarkId::from_parameter(n), &n, |b, &n| {
            b.iter(|| maho_bench::automerge_bench::merge_two_peers(n));
        });
    }
    group.finish();
}

fn bench_yrs_cold_load(c: &mut Criterion) {
    let mut group = c.benchmark_group("yrs_cold_load");
    for n in [100, 500, 1000] {
        group.bench_with_input(BenchmarkId::from_parameter(n), &n, |b, &n| {
            b.iter(|| maho_bench::yrs_bench::cold_load(n));
        });
    }
    group.finish();
}

fn bench_automerge_cold_load(c: &mut Criterion) {
    let mut group = c.benchmark_group("automerge_cold_load");
    for n in [100, 500, 1000] {
        group.bench_with_input(BenchmarkId::from_parameter(n), &n, |b, &n| {
            b.iter(|| maho_bench::automerge_bench::cold_load(n));
        });
    }
    group.finish();
}

fn bench_diamond_insert(c: &mut Criterion) {
    let mut group = c.benchmark_group("diamond_insert");
    for n in [100, 500, 1000] {
        group.bench_with_input(BenchmarkId::from_parameter(n), &n, |b, &n| {
            b.iter(|| maho_bench::diamond_bench::insert_tab_metadata(n));
        });
    }
    group.finish();
}

fn bench_diamond_merge(c: &mut Criterion) {
    let mut group = c.benchmark_group("diamond_merge");
    for n in [100, 500, 1000] {
        group.bench_with_input(BenchmarkId::from_parameter(n), &n, |b, &n| {
            b.iter(|| maho_bench::diamond_bench::merge_two_peers(n));
        });
    }
    group.finish();
}

fn bench_diamond_cold_load(c: &mut Criterion) {
    let mut group = c.benchmark_group("diamond_cold_load");
    for n in [100, 500, 1000] {
        group.bench_with_input(BenchmarkId::from_parameter(n), &n, |b, &n| {
            b.iter(|| maho_bench::diamond_bench::cold_load(n));
        });
    }
    group.finish();
}

criterion_group!(
    benches,
    bench_yrs_insert,
    bench_automerge_insert,
    bench_diamond_insert,
    bench_yrs_merge,
    bench_automerge_merge,
    bench_diamond_merge,
    bench_yrs_cold_load,
    bench_automerge_cold_load,
    bench_diamond_cold_load,
);
criterion_main!(benches);
