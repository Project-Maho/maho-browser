use maho_core::maho_core::MahoCore;
use maho_types::common::Orientation;
use maho_types::events::core_update::CoreUpdate;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::{TabId, WindowId};

#[test]
fn test_multi_window_split_view_isolation() {
    let mut core = MahoCore::new();
    let w1 = WindowId::new("window-1");
    let w2 = WindowId::new("window-2");
    let t1 = TabId::new("tab-1");
    let t2 = TabId::new("tab-2");
    let t3 = TabId::new("tab-3");
    let t4 = TabId::new("tab-4");

    // Create split in window-1
    core.handle_event(ShellEvent::CreateSplit {
        window_id: w1.clone(),
        tab_ids: vec![t1, t2],
        orientation: Orientation::Horizontal,
        layout: None,
    });

    // Create split in window-2
    core.handle_event(ShellEvent::CreateSplit {
        window_id: w2.clone(),
        tab_ids: vec![t3, t4],
        orientation: Orientation::Vertical,
        layout: None,
    });

    let config1 = core.get_split_view_config(&w1).unwrap();
    let config2 = core.get_split_view_config(&w2).unwrap();

    assert_eq!(config1.orientation, Orientation::Horizontal);
    assert_eq!(config1.panes.len(), 2);
    assert_eq!(config1.panes[0].tab_id.0, "tab-1");

    assert_eq!(config2.orientation, Orientation::Vertical);
    assert_eq!(config2.panes.len(), 2);
    assert_eq!(config2.panes[0].tab_id.0, "tab-3");

    // Clear window-1 split
    core.handle_event(ShellEvent::ClearSplitView {
        window_id: w1.clone(),
    });
    assert!(core.get_split_view_config(&w1).is_none());
    assert!(core.get_split_view_config(&w2).is_some());
}

#[test]
fn test_split_view_events_emit_changed() {
    let mut core = MahoCore::new();
    let w = WindowId::new("window-1");
    let t1 = TabId::new("tab-1");
    let t2 = TabId::new("tab-2");
    let t3 = TabId::new("tab-3");

    // 1. CreateSplit
    let updates = core.handle_event(ShellEvent::CreateSplit {
        window_id: w.clone(),
        tab_ids: vec![t1.clone(), t2.clone(), t3.clone()],
        orientation: Orientation::Horizontal,
        layout: None,
    });

    assert_eq!(updates.len(), 1);
    if let CoreUpdate::SplitViewChanged {
        window_id,
        config,
        schema_version,
    } = &updates[0]
    {
        assert_eq!(window_id, &w);
        assert_eq!(*schema_version, 1);
        let cfg = config.as_ref().unwrap();
        assert_eq!(cfg.panes.len(), 3);
        // Equal thirds. The ratios are derived by walking the binary layout
        // tree (root 1/3, then 1/2 of the remaining 2/3), so the trailing panes
        // land one ULP off an exact `1.0 / 3.0` literal. Compare with a
        // tolerance like the resize assertions below; exact float equality here
        // asserts an IEEE rounding path, not the contract.
        assert_eq!(cfg.ratios.len(), 3);
        for ratio in &cfg.ratios {
            assert!(
                (ratio - 1.0 / 3.0).abs() < 1e-9,
                "expected equal thirds, got {:?}",
                cfg.ratios
            );
        }
        assert!((cfg.ratios.iter().sum::<f64>() - 1.0).abs() < 1e-9);
    } else {
        panic!("Expected SplitViewChanged");
    }

    // 2. ResizeSplit
    let updates = core.handle_event(ShellEvent::ResizeSplit {
        window_id: w.clone(),
        pane_id: "tab-1".to_string(),
        ratio: 0.5,
    });
    assert_eq!(updates.len(), 1);
    if let CoreUpdate::SplitViewChanged { config, .. } = &updates[0] {
        let cfg = config.as_ref().unwrap();
        assert_eq!(cfg.ratios[0], 0.5);
        // remaining 0.5 distributed to the other 2 panes
        assert!((cfg.ratios[1] - 0.25).abs() < 1e-9);
        assert!((cfg.ratios[2] - 0.25).abs() < 1e-9);
    } else {
        panic!("Expected SplitViewChanged");
    }

    // 3. RemoveSplit (leaves 2 panes, does not clear split yet)
    let updates = core.handle_event(ShellEvent::RemoveSplit {
        window_id: w.clone(),
        pane_id: "tab-2".to_string(),
    });
    assert_eq!(updates.len(), 1);
    if let CoreUpdate::SplitViewChanged { config, .. } = &updates[0] {
        let cfg = config.as_ref().unwrap();
        assert_eq!(cfg.panes.len(), 2);
        assert_eq!(cfg.panes[0].tab_id.0, "tab-1");
        assert_eq!(cfg.panes[1].tab_id.0, "tab-3");
    } else {
        panic!("Expected SplitViewChanged");
    }

    // 4. RemoveSplit again (leaves 1 pane, must clear split and return None)
    let updates = core.handle_event(ShellEvent::RemoveSplit {
        window_id: w.clone(),
        pane_id: "tab-3".to_string(),
    });
    assert_eq!(updates.len(), 1);
    if let CoreUpdate::SplitViewChanged { config, .. } = &updates[0] {
        assert!(config.is_none());
    } else {
        panic!("Expected SplitViewChanged");
    }
}

#[test]
fn test_window_closed_removes_config() {
    let mut core = MahoCore::new();
    let w = WindowId::new("window-1");
    let t1 = TabId::new("tab-1");
    let t2 = TabId::new("tab-2");

    core.handle_event(ShellEvent::CreateSplit {
        window_id: w.clone(),
        tab_ids: vec![t1, t2],
        orientation: Orientation::Horizontal,
        layout: None,
    });
    assert!(core.get_split_view_config(&w).is_some());

    core.window_closed(&w);
    assert!(core.get_split_view_config(&w).is_none());
}
