use maho_core::maho_core::MahoCore;
use maho_types::events::shell_event::ShellEvent;
use maho_types::events::core_update::CoreUpdate;
use maho_types::identifiers::TabId;
use maho_types::common::Url;
use maho_types::tab::TabRole;

fn create_tab_id(core: &mut MahoCore, url: &str) -> TabId {
    let space_id = core.get_active_space_id().clone();
    let updates = core.handle_event(ShellEvent::CreateTab {
        space_id,
        url: Some(Url::new(url)),
        parent_id: None,
        tab_id: None,
        window_id: None,
    });
    updates.iter().find_map(|u| match u {
        CoreUpdate::TabCreated { tab } => Some(tab.id.clone()),
        _ => None,
    }).expect("TabCreated event not found")
}

#[test]
fn test_favorites_regression_unique_identities() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id().clone();

    // 1. Create tab A (which will be favorited)
    let tab_a_id = create_tab_id(&mut core, "https://example.com");

    // 2. Favorite tab A (in B-arch this transitions its role to Favorite,
    // and it remains a separate tab identity)
    core.handle_event(ShellEvent::FavoriteTab { tab_id: tab_a_id.clone() });

    // 3. Create tab B (Normal tab with same URL)
    let tab_b_id = create_tab_id(&mut core, "https://example.com");

    // Assert different IDs
    assert_ne!(tab_a_id, tab_b_id);

    // Verify favorite list only has tab A
    let favorites = core.get_favorite_tabs(&space_id);
    assert!(favorites.iter().any(|t| t.id == tab_a_id));
    assert!(!favorites.iter().any(|t| t.id == tab_b_id));

    // Verify normal tabs (root order) only has tab B
    // (Note: in A-arch, favorite_tab_removes_from_root_order is true,
    // but the bug is that favorite and tab list share the same ID if not separated)
    let root_order = core.space_manager().get_root_order(&space_id);
    let has_a = root_order.contains(&maho_types::space::RootItem::Tab(tab_a_id.clone()));
    let has_b = root_order.contains(&maho_types::space::RootItem::Tab(tab_b_id.clone()));
    assert!(!has_a);
    assert!(has_b);

    // Assert no ID appears in both
    for fav in &favorites {
        assert!(!root_order.contains(&maho_types::space::RootItem::Tab(fav.id.clone())));
    }
}

#[test]
fn test_surface_duplication_absence_property() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id().clone();

    // Create 5 tabs
    let mut tabs = vec![];
    for i in 0..5 {
        let url_str = format!("https://example{}.com", i);
        tabs.push(create_tab_id(&mut core, &url_str));
    }

    // Favorite 2 of them
    core.handle_event(ShellEvent::FavoriteTab { tab_id: tabs[0].clone() });
    core.handle_event(ShellEvent::FavoriteTab { tab_id: tabs[2].clone() });

    // Assert surface duplication absence
    let favorites = core.get_favorite_tabs(&space_id);
    let root_order = core.space_manager().get_root_order(&space_id);

    for fav in &favorites {
        let is_in_root = root_order.iter().any(|item| match item {
            maho_types::space::RootItem::Tab(tid) => tid == &fav.id,
            _ => false,
        });
        assert!(!is_in_root, "TabId {:?} found in both favorites list and root list!", fav.id);
    }
}

#[test]
fn test_role_exclusivity_contract() {
    let mut core = MahoCore::new();
    let tab_id = create_tab_id(&mut core, "https://example.com");

    // Verify it starts with Normal role
    let tab = core.tab_manager().get_tab(&tab_id).unwrap();
    assert_eq!(tab.role, TabRole::Normal);

    // Transition to Pinned
    core.handle_event(ShellEvent::ChangeTabRole {
        tab_id: tab_id.clone(),
        new_role: TabRole::Pinned,
    });
    let tab = core.tab_manager().get_tab(&tab_id).unwrap();
    assert_eq!(tab.role, TabRole::Pinned);
    assert!(tab.role.is_pinned());
    assert!(!tab.role.is_favorite());

    // Transition to Favorite
    core.handle_event(ShellEvent::FavoriteTab { tab_id: tab_id.clone() });
    let tab = core.tab_manager().get_tab(&tab_id).unwrap();
    assert!(matches!(tab.role, TabRole::Favorite { .. }));
    assert!(tab.role.is_favorite());
    assert!(tab.role.is_pinned()); // under current design, favorite is also pinned

    // Transition back to Normal
    core.handle_event(ShellEvent::ChangeTabRole {
        tab_id: tab_id.clone(),
        new_role: TabRole::Normal,
    });
    let tab = core.tab_manager().get_tab(&tab_id).unwrap();
    assert_eq!(tab.role, TabRole::Normal);
    assert!(!tab.role.is_pinned());
    assert!(!tab.role.is_favorite());
}

#[test]
fn test_serialization_round_trip_preservation() {
    use maho_types::tab::Tab;

    let roles = vec![
        TabRole::Normal,
        TabRole::Pinned,
        TabRole::Favorite { order: 42 },
    ];

    for role in roles {
        let tab_val = serde_json::json!({
            "id": "tab-uuid-1234",
            "parentId": null,
            "spaceId": "space-123",
            "url": "https://example.com",
            "title": "Test Tab",
            "favicon": null,
            "state": { "kind": "active" },
            "role": role,
            "isMuted": false,
            "zoomLevel": 1.0,
            "createdAt": maho_types::common::DateTime::now(),
            "lastActiveAt": maho_types::common::DateTime::now(),
            "scrollPosition": { "x": 0, "y": 0 },
            "windowId": null,
        });
        let tab: Tab = serde_json::from_value(tab_val).unwrap();

        let serialized = serde_json::to_string(&tab).unwrap();
        let deserialized: Tab = serde_json::from_str(&serialized).unwrap();

        assert_eq!(deserialized.role, role);
    }
}
