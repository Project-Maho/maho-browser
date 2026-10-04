use criterion::{black_box, criterion_group, criterion_main, Criterion};
use maho_core::maho_core::MahoCore;
use maho_types::common::Url;
use maho_types::events::shell_event::ShellEvent;
use tempfile::tempdir;

fn seed_storage(num_tabs: usize) -> (tempfile::TempDir, std::path::PathBuf) {
    let temp_dir = tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("startup_bench_maho.lmdb");

    let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
    let space_id = core.get_active_space_id();

    for i in 0..num_tabs {
        core.handle_event(ShellEvent::CreateTab {
            space_id: space_id.clone(),
            url: Some(Url::new(format!("https://example{}.com", i))),
            parent_id: None,
            tab_id: None,
            window_id: None,
            is_private: false,
        });
    }

    core.save_state().expect("save seed state");
    drop(core);

    (temp_dir, storage_path)
}

fn bench_core_startup(c: &mut Criterion) {
    let (_temp_dir, storage_path) = seed_storage(50);

    c.bench_function("core_startup_cold_load", |b| {
        b.iter(|| {
            let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
            core.load_state().expect("load state");
            let tabs = core.get_tab_view_models();
            let spaces = core.get_space_view_models();
            black_box((tabs, spaces));
        });
    });

    c.bench_function("core_startup_load_state", |b| {
        b.iter(|| {
            let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
            core.load_state().expect("load state");
            black_box(core.get_active_space_id());
        });
    });
}

criterion_group!(benches, bench_core_startup);
criterion_main!(benches);
