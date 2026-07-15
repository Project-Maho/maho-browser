use criterion::{criterion_group, criterion_main, BenchmarkId, Criterion};
use maho_core::maho_core::MahoCore;
use maho_types::common::Url;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::{SpaceId, TabId};

/// Helper to extract tab ID from CoreUpdate responses
fn extract_tab_id_from_updates(core: &mut MahoCore, space_id: &SpaceId, url: &str) -> TabId {
    let event = ShellEvent::CreateTab {
        space_id: space_id.clone(),
        url: Some(Url::new(url)),
        parent_id: None,
        tab_id: None,
        window_id: None,
        is_private: false,
    };
    let updates = core.handle_event(event);

    // Find TabCreated update and extract the ID
    for update in updates {
        if let maho_types::events::core_update::CoreUpdate::TabCreated { tab } = update {
            return tab.id;
        }
    }

    // Fallback: get from view models if update didn't contain it
    let tabs = core.get_tab_view_models();
    tabs.last()
        .map(|t| t.id.clone())
        .expect("Failed to create tab")
}

/// Helper to activate a tab
fn activate_tab(core: &mut MahoCore, tab_id: &TabId) {
    let event = ShellEvent::ActivateTab {
        tab_id: tab_id.clone(),
    };
    core.handle_event(event);
}

/// Benchmark: active→active tab switching
fn bench_active_to_active_switch(c: &mut Criterion) {
    let mut group = c.benchmark_group("tab_switch_active_to_active");

    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();

    // Create 10 tabs and activate first one
    let mut tab_ids = Vec::new();
    for i in 0..10 {
        let url = format!("https://example{}.com", i);
        let tab_id = extract_tab_id_from_updates(&mut core, &space_id, &url);
        tab_ids.push(tab_id);
    }

    // Activate first tab to establish baseline
    if let Some(first_tab) = tab_ids.first() {
        activate_tab(&mut core, first_tab);
    }

    // Benchmark switching from tab 0 to tab 1
    let target_tab = tab_ids.get(1).expect("Need at least 2 tabs").clone();
    group.bench_function("switch_tab", |b| {
        b.iter(|| {
            activate_tab(&mut core, &target_tab);
        });
    });

    group.finish();
}

/// Benchmark: suspended→active tab switching
fn bench_suspended_to_active_switch(c: &mut Criterion) {
    let mut group = c.benchmark_group("tab_switch_suspended_to_active");

    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();

    // Create an active tab
    let active_tab_id = extract_tab_id_from_updates(&mut core, &space_id, "https://active-tab.com");
    activate_tab(&mut core, &active_tab_id);

    // Create a suspended tab by creating and not interacting (stays in default state)
    // The tab lifecycle starts as Active, so we simulate "suspended" by creating
    // a tab that hasn't been the active focus
    let suspended_tab_id =
        extract_tab_id_from_updates(&mut core, &space_id, "https://suspended-tab.com");
    // Don't activate this tab - in real usage it would age to suspended

    // Benchmark switching TO the suspended tab
    group.bench_function("activate_suspended", |b| {
        b.iter(|| {
            activate_tab(&mut core, &suspended_tab_id);
        });
    });

    group.finish();
}

/// Benchmark: scale test with many tabs
fn bench_scale_activate_tab(c: &mut Criterion) {
    let mut group = c.benchmark_group("tab_switch_scale");

    for tab_count in [50, 100, 500] {
        group.bench_with_input(
            BenchmarkId::from_parameter(tab_count),
            &tab_count,
            |b, &n| {
                b.iter_with_setup(
                    || {
                        // Setup: create core with N tabs
                        let mut core = MahoCore::new();
                        let space_id = core.get_active_space_id();
                        let mut tab_ids = Vec::with_capacity(n);

                        for i in 0..n {
                            let url = format!("https://example{}.com", i);
                            let tab_id = extract_tab_id_from_updates(&mut core, &space_id, &url);
                            tab_ids.push(tab_id);
                        }

                        // Activate first tab
                        if let Some(first) = tab_ids.first() {
                            activate_tab(&mut core, first);
                        }

                        // Target: last tab
                        let target = tab_ids.last().expect("Need at least 1 tab").clone();
                        (core, target)
                    },
                    |(ref mut core, target_tab)| {
                        // Benchmark: activate the target tab
                        activate_tab(core, &target_tab);
                    },
                );
            },
        );
    }

    group.finish();
}

criterion_group!(
    benches,
    bench_active_to_active_switch,
    bench_suspended_to_active_switch,
    bench_scale_activate_tab,
);
criterion_main!(benches);
