use maho_core::maho_core::MahoCore;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::TabId;
use maho_types::common::{ImageData, ImageFormat, Url};

#[test]
fn test_preview_round_trip_lmdb() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    let tab_id = TabId::new("test-tab-001");
    let fake_jpeg = vec![0xFF, 0xD8, 0xFF, 0xE0];

    {
        let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
        let space_id = core.get_active_space_id();

        // Create the tab first so it exists when loading
        core.handle_event(ShellEvent::CreateTab {
            space_id: space_id.clone(),
            url: Some(Url::new("https://example.com")),
            parent_id: None,
            tab_id: Some(tab_id.clone()),
            window_id: None,
        });

        let image = ImageData {
            data: fake_jpeg.clone(),
            width: 640,
            height: 400,
            format: ImageFormat::Jpeg,
        };
        core.update_tab_preview(&tab_id, image);
        core.save_state().expect("save state");
    }

    // Restore from same storage
    {
        let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
        core.load_state().expect("load state");

        let preview = core.get_tab_preview(&tab_id);
        assert!(preview.is_some());
        let preview = preview.unwrap();
        assert_eq!(preview.thumbnail.as_ref().unwrap().data, fake_jpeg);
    }
}

#[test]
fn test_preview_lru_eviction() {
    use maho_core::tab_preview_manager::TabPreviewManager;

    let mut manager = TabPreviewManager::new();

    // 100KB each
    let size_100kb = 100 * 1024;
    
    // Add 15 previews -> total 1.5MB
    for i in 0..15 {
        let tab_id = TabId::new(format!("tab-{}", i));
        let image = ImageData {
            data: vec![0u8; size_100kb],
            width: 10,
            height: 10,
            format: ImageFormat::Jpeg,
        };
        manager.update_preview(tab_id, image);
        std::thread::sleep(std::time::Duration::from_millis(1));
    }

    assert_eq!(manager.total_size_bytes(), 15 * size_100kb);

    // Evict down to 10 * 100KB = 1MB
    manager.evict_lru(10 * size_100kb);

    assert_eq!(manager.total_size_bytes(), 10 * size_100kb);

    // Oldest 5 (0, 1, 2, 3, 4) should be evicted, leaving 5 to 14
    for i in 0..5 {
        assert!(!manager.has_preview(&TabId::new(format!("tab-{}", i))));
    }
    for i in 5..15 {
        assert!(manager.has_preview(&TabId::new(format!("tab-{}", i))));
    }
}
