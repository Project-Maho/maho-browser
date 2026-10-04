use criterion::{criterion_group, criterion_main, BenchmarkId, Criterion};
use maho_core::maho_core::MahoCore;
use maho_types::events::shell_event::ShellEvent;

/// Setup helper: create core with N history entries
fn setup_core_with_history(entry_count: usize) -> MahoCore {
    let mut core = MahoCore::new();

    for i in 0..entry_count {
        let url = format!("https://site{}.com/page{}", i % 100, i);
        let title = format!("Page Title {} with searchable keywords", i);
        core.add_history_entry(&url, &title);
    }

    core
}

/// Benchmark: command bar search with various history sizes
fn bench_search_with_history(c: &mut Criterion) {
    let mut group = c.benchmark_group("command_search");

    for entry_count in [100, 500, 1000] {
        group.bench_with_input(
            BenchmarkId::new("search_query", entry_count),
            &entry_count,
            |b, &n| {
                b.iter_with_setup(
                    || setup_core_with_history(n),
                    |ref mut core| {
                        let event = ShellEvent::CommandBarQuery {
                            text: "test query".to_string(),
                            mode: None,
                            is_incognito: false,
                        };
                        core.handle_event(event);
                    },
                );
            },
        );
    }

    group.finish();
}

/// Benchmark: empty query (should return recent items fast)
fn bench_search_empty_query(c: &mut Criterion) {
    let mut group = c.benchmark_group("command_search_empty");

    for entry_count in [100, 500, 1000] {
        group.bench_with_input(
            BenchmarkId::from_parameter(entry_count),
            &entry_count,
            |b, &n| {
                b.iter_with_setup(
                    || setup_core_with_history(n),
                    |ref mut core| {
                        let event = ShellEvent::CommandBarQuery {
                            text: "".to_string(),
                            mode: None,
                            is_incognito: false,
                        };
                        core.handle_event(event);
                    },
                );
            },
        );
    }

    group.finish();
}

/// Benchmark: search with partial matches
fn bench_search_partial_matches(c: &mut Criterion) {
    let mut group = c.benchmark_group("command_search_partial");
    let mut core = MahoCore::new();

    // Add 500 history entries
    for i in 0..500 {
        let url = format!("https://example{}.com/article/{}", i % 50, i);
        let title = format!("Article {}: How to optimize performance", i);
        core.add_history_entry(&url, &title);
    }

    // Benchmark various query lengths
    for query in ["a", "ar", "art", "arti", "artic", "articl", "article"] {
        group.bench_with_input(BenchmarkId::from_parameter(query.len()), &query, |b, q| {
            b.iter(|| {
                let event = ShellEvent::CommandBarQuery {
                    text: q.to_string(),
                    mode: None,
                    is_incognito: false,
                };
                core.handle_event(event);
            });
        });
    }

    group.finish();
}

criterion_group!(
    benches,
    bench_search_with_history,
    bench_search_empty_query,
    bench_search_partial_matches,
);
criterion_main!(benches);
