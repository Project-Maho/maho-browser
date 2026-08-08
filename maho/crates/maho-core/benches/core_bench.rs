use criterion::{black_box, criterion_group, criterion_main, BatchSize, Criterion};
use maho_core::content_blocker::{compile_engine_snapshot, ContentBlocker};
use maho_core::maho_core::MahoCore;
use maho_types::common::Url;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::SpaceId;
use maho_types::space::SpaceColor;

const CONTENT_BLOCKER_30K_RULES: usize = 30_000;
const CONTENT_BLOCKER_100K_RULES: usize = 100_000;

fn deterministic_content_blocker_rules(rule_count: usize) -> String {
    let mut rules = String::with_capacity(rule_count * "||rule-.benchmark.invalid^\n".len());
    for rule_index in 0..rule_count {
        rules.push_str("||rule-");
        rules.push_str(&rule_index.to_string());
        rules.push_str(".benchmark.invalid^\n");
    }
    rules
}

fn content_blocker_with_rules(rule_count: usize) -> ContentBlocker {
    let mut blocker = ContentBlocker::new();
    blocker.set_popup_blocking(false);
    match blocker
        .update_filter_list_content("easylist", deterministic_content_blocker_rules(rule_count))
    {
        Ok(()) => blocker,
        Err(error) => {
            panic!("deterministic benchmark rules must fit the content blocker limits: {error}")
        }
    }
}

fn matching_benchmark_url(rule_count: usize) -> String {
    format!("https://rule-{}.benchmark.invalid/asset.js", rule_count - 1)
}

fn bench_content_blocker_compile(c: &mut Criterion) {
    for (benchmark_name, rule_count) in [
        (
            "content_blocker_compile_30000_rules",
            CONTENT_BLOCKER_30K_RULES,
        ),
        (
            "content_blocker_compile_100000_rules",
            CONTENT_BLOCKER_100K_RULES,
        ),
    ] {
        let blocker = content_blocker_with_rules(rule_count);
        let snapshot = blocker.create_compile_snapshot();
        c.bench_function(benchmark_name, |b| {
            b.iter_batched(
                || snapshot.clone(),
                |snapshot| black_box(compile_engine_snapshot(snapshot)),
                BatchSize::LargeInput,
            );
        });
    }
}

fn bench_content_blocker_match(c: &mut Criterion) {
    for (benchmark_name, rule_count) in [
        (
            "content_blocker_match_30000_rules",
            CONTENT_BLOCKER_30K_RULES,
        ),
        (
            "content_blocker_match_100000_rules",
            CONTENT_BLOCKER_100K_RULES,
        ),
    ] {
        let mut blocker = content_blocker_with_rules(rule_count);
        blocker.rebuild_engine_sync();
        let request_url = matching_benchmark_url(rule_count);
        c.bench_function(benchmark_name, |b| {
            b.iter(|| {
                black_box(blocker.should_block_request(
                    black_box(&request_url),
                    black_box("https://site.benchmark.invalid"),
                    black_box("script"),
                ))
            });
        });
    }
}

fn bench_create_tab(c: &mut Criterion) {
    c.bench_function("create_tab", |b| {
        b.iter_with_setup(MahoCore::new, |mut core| {
            core.handle_event(ShellEvent::CreateTab {
                space_id: SpaceId("space-1".into()),
                url: Some(Url("https://example.com".into())),
                parent_id: None,
                tab_id: None,
                window_id: None,
                is_private: false,
            });
        });
    });
}

fn bench_close_tab(c: &mut Criterion) {
    c.bench_function("close_tab", |b| {
        b.iter_with_setup(
            || {
                let mut core = MahoCore::new();
                core.handle_event(ShellEvent::CreateTab {
                    space_id: SpaceId("space-1".into()),
                    url: Some(Url("https://example.com".into())),
                    parent_id: None,
                    tab_id: None,
                    window_id: None,
                    is_private: false,
                });
                let tab_id = core.get_tab_view_models().first().map(|t| t.id.clone());
                (core, tab_id)
            },
            |(mut core, tab_id)| {
                if let Some(id) = tab_id {
                    core.handle_event(ShellEvent::CloseTab {
                        tab_id: id,
                        expected_space_id: None,
                    });
                }
            },
        );
    });
}

fn bench_switch_space(c: &mut Criterion) {
    c.bench_function("switch_space", |b| {
        b.iter_with_setup(
            || {
                let mut core = MahoCore::new();
                let profile_id = core
                    .get_active_profile_id()
                    .cloned()
                    .unwrap_or_else(|| maho_types::identifiers::ProfileId::new(""));
                core.handle_event(ShellEvent::CreateSpace {
                    name: "Test".into(),
                    color: SpaceColor {
                        hue: 220.0,
                        saturation: 0.8,
                        brightness: 0.9,
                        grain: 0.0,
                    },
                    profile_id,
                });
                let spaces = core.get_space_view_models();
                let target = spaces
                    .last()
                    .map(|s| s.id.clone())
                    .unwrap_or(SpaceId("s".into()));
                (core, target)
            },
            |(mut core, target)| {
                core.handle_event(ShellEvent::ActivateSpace { space_id: target });
            },
        );
    });
}

fn bench_command_bar_query(c: &mut Criterion) {
    c.bench_function("command_bar_query", |b| {
        b.iter_with_setup(
            || {
                let mut core = MahoCore::new();
                for i in 0..100 {
                    core.handle_event(ShellEvent::CreateTab {
                        space_id: SpaceId("space-1".into()),
                        url: Some(Url(format!("https://example.com/{i}"))),
                        parent_id: None,
                        tab_id: None,
                        window_id: None,
                        is_private: false,
                    });
                }
                core
            },
            |mut core| {
                core.handle_event(ShellEvent::CommandBarQuery {
                    text: "example".into(),
                    mode: None,
                    is_incognito: false,
                });
            },
        );
    });
}

criterion_group!(
    benches,
    bench_create_tab,
    bench_close_tab,
    bench_switch_space,
    bench_command_bar_query,
    bench_content_blocker_compile,
    bench_content_blocker_match,
);
criterion_main!(benches);
