use criterion::{black_box, criterion_group, criterion_main, Criterion};
use maho_core::maho_core::MahoCore;
use maho_types::common::Url;
use maho_types::events::shell_event::ShellEvent;
use tempfile::tempdir;

fn bench_persistence(c: &mut Criterion) {
    let temp_dir = tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("bench_maho.lmdb");

    let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
    let space_id = core.get_active_space_id();

    let mut tab_ids = Vec::new();
    for i in 0..100 {
        let _updates = core.handle_event(ShellEvent::CreateTab {
            space_id: space_id.clone(),
            url: Some(Url::new(format!("https://example{}.com", i))),
            parent_id: None,
            tab_id: None,
            window_id: None,
            is_private: false,
        });
        if let Some(tab) = core.get_tab_view_models().last() {
            tab_ids.push(tab.id.clone());
        }
    }
    core.save_state().unwrap();

    c.bench_function("core_startup_cold_load", |b| {
        b.iter(|| {
            let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
            restored.load_state().unwrap();
            let tabs = restored.get_tab_view_models();
            black_box(tabs);
        });
    });

    c.bench_function("core_startup_load_state", |b| {
        b.iter(|| {
            let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
            restored.load_state().unwrap();
            black_box(restored.get_active_space_id());
        });
    });

    c.bench_function("lmdb_save_state_100_tabs", |b| {
        b.iter(|| {
            core.save_state().unwrap();
        });
    });

    c.bench_function("lmdb_handle_event_activate_tab_100_tabs", |b| {
        let mut index = 0;
        b.iter(|| {
            let tab_id = &tab_ids[index % tab_ids.len()];
            core.handle_event(ShellEvent::ActivateTab {
                tab_id: tab_id.clone(),
            });
            index += 1;
        });
    });

    c.bench_function("lmdb_handle_event_tab_url_updated_100_tabs", |b| {
        let mut index = 0;
        b.iter(|| {
            let tab_id = &tab_ids[index % tab_ids.len()];
            core.handle_event(ShellEvent::TabUrlUpdated {
                tab_id: tab_id.clone(),
                url: Url::new(format!("https://updated-{}.com", index)),
            });
            index += 1;
        });
    });

    c.bench_function("lmdb_handle_event_throttled_100_rapid_creates", |b| {
        b.iter(|| {
            for i in 0..100 {
                core.handle_event(ShellEvent::CreateTab {
                    space_id: space_id.clone(),
                    url: Some(Url::new(format!("https://burst-{}.com", i))),
                    parent_id: None,
                    tab_id: None,
                    window_id: None,
                    is_private: false,
                });
            }
        });
    });
}

criterion_group!(benches, bench_persistence);
criterion_main!(benches);
