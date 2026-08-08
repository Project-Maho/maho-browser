use criterion::{black_box, criterion_group, criterion_main, BenchmarkId, Criterion};
use maho_core::maho_core::MahoCore;
use maho_types::common::Url;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::{SpaceId, TabId};

fn create_tabs(core: &mut MahoCore, space_id: &SpaceId, n: usize) -> Vec<TabId> {
    let mut tab_ids = Vec::with_capacity(n);
    for i in 0..n {
        let url = format!("https://example{}.com/path/{}", i, i);
        let event = ShellEvent::CreateTab {
            space_id: space_id.clone(),
            url: Some(Url::new(&url)),
            parent_id: None,
            tab_id: None,
            window_id: None,
            is_private: false,
        };
        let updates = core.handle_event(event);
        for update in updates {
            if let maho_types::events::core_update::CoreUpdate::TabCreated { tab } = update {
                tab_ids.push(tab.id);
                break;
            }
        }
    }
    tab_ids
}

fn bench_single_tab_lookup(c: &mut Criterion) {
    let mut group = c.benchmark_group("tab_snapshot_lookup");

    for &n in &[10usize, 100, 311, 1000] {
        let mut core = MahoCore::new();
        let space_id = core.get_active_space_id();
        let tab_ids = create_tabs(&mut core, &space_id, n);
        let target = tab_ids[n / 2].clone();

        group.bench_with_input(
            BenchmarkId::new("get_tab_snapshot_json", n),
            &target,
            |b, tab_id| {
                b.iter(|| {
                    let json = core.get_tab_snapshot_json(black_box(tab_id));
                    black_box(json);
                });
            },
        );

        group.bench_with_input(
            BenchmarkId::new("get_tab_view_models_baseline", n),
            &target,
            |b, _tab_id| {
                b.iter(|| {
                    let all = core.get_tab_view_models();
                    black_box(all);
                });
            },
        );
    }

    group.finish();
}

criterion_group!(benches, bench_single_tab_lookup);
criterion_main!(benches);
