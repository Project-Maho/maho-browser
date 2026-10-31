use maho_core::maho_core::MahoCore;
use maho_types::common::Url;
use maho_types::events::core_update::CoreUpdate;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::TabId;
use maho_types::space::RootItem;
use maho_types::tab::{TabLifecycleState, TabRole};

fn create_tab_id(core: &mut MahoCore, url: &str) -> TabId {
    let space_id = core.get_active_space_id().clone();
    let updates = core.handle_event(ShellEvent::CreateTab {
        space_id,
        url: Some(Url::new(url)),
        parent_id: None,
        tab_id: None,
        window_id: None,
        is_private: false,
    });
    updates
        .iter()
        .find_map(|u| match u {
            CoreUpdate::TabCreated { tab } => Some(tab.id.clone()),
            _ => None,
        })
        .expect("TabCreated event not found")
}

#[test]
fn test_favorites_regression_unique_identities() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id().clone();

    // 1. Create tab A (which will be favorited)
    let tab_a_id = create_tab_id(&mut core, "https://example.com");

    // 2. Favorite tab A (in B-arch this transitions its role to Favorite,
    // and it remains a separate tab identity)
    core.handle_event(ShellEvent::FavoriteTab {
        tab_id: tab_a_id.clone(),
    });

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
    core.handle_event(ShellEvent::FavoriteTab {
        tab_id: tabs[0].clone(),
    });
    core.handle_event(ShellEvent::FavoriteTab {
        tab_id: tabs[2].clone(),
    });

    // Assert surface duplication absence
    let favorites = core.get_favorite_tabs(&space_id);
    let root_order = core.space_manager().get_root_order(&space_id);

    for fav in &favorites {
        let is_in_root = root_order.iter().any(|item| match item {
            maho_types::space::RootItem::Tab(tid) => tid == &fav.id,
            _ => false,
        });
        assert!(
            !is_in_root,
            "TabId {:?} found in both favorites list and root list!",
            fav.id
        );
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
    core.handle_event(ShellEvent::FavoriteTab {
        tab_id: tab_id.clone(),
    });
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

#[test]
fn test_favorite_close_leaves_no_ghost() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id().clone();
    let tab_id = create_tab_id(&mut core, "https://example.com");
    core.handle_event(ShellEvent::FavoriteTab {
        tab_id: tab_id.clone(),
    });
    assert!(core
        .get_favorite_tabs(&space_id)
        .iter()
        .any(|t| t.id == tab_id));

    core.handle_event(ShellEvent::CloseTab {
        tab_id: tab_id.clone(),
        expected_space_id: None,
    });

    // Under ADR-11 close protection, the favorite tab persists as suspended
    assert!(
        core.get_favorite_tabs(&space_id)
            .iter()
            .any(|t| t.id == tab_id),
        "closing a favorited tab must retain it in the favorites grid (suspended)"
    );
    assert!(
        matches!(
            core.tab_manager().get_tab(&tab_id).unwrap().state,
            TabLifecycleState::Suspended { .. }
        ),
        "closed favorite must be transitioned to Suspended state"
    );
}

#[test]
fn test_is_tab_pinned_strict_excludes_favorite() {
    let mut core = MahoCore::new();
    let pinned = create_tab_id(&mut core, "https://pinned.example");
    let fav = create_tab_id(&mut core, "https://fav.example");
    let normal = create_tab_id(&mut core, "https://normal.example");
    core.handle_event(ShellEvent::PinTab {
        tab_id: pinned.clone(),
    });
    core.handle_event(ShellEvent::FavoriteTab {
        tab_id: fav.clone(),
    });

    assert!(core.is_tab_pinned(pinned.as_ref()));
    assert!(
        !core.is_tab_pinned(fav.as_ref()),
        "favorite must not be reported as pinned (strict semantics for close guard)"
    );
    assert!(!core.is_tab_pinned(normal.as_ref()));
}

#[test]
fn test_activate_suspended_favorite_resolves_to_active() {
    let mut core = MahoCore::new();
    let tab_id = create_tab_id(&mut core, "https://example.com");
    core.handle_event(ShellEvent::FavoriteTab {
        tab_id: tab_id.clone(),
    });

    // Suspend the favorite tab
    core.handle_event(ShellEvent::SuspendTab {
        tab_id: tab_id.clone(),
    });

    let tab = core.tab_manager().get_tab(&tab_id).unwrap();
    assert!(matches!(
        tab.state,
        maho_types::tab::TabLifecycleState::Suspended { .. }
    ));

    // Listen to updates
    let updates = std::rc::Rc::new(std::cell::RefCell::new(Vec::new()));
    let updates_cb = updates.clone();
    core.on_update(Box::new(move |update| {
        updates_cb.borrow_mut().push(update);
    }));

    // Activate the favorite tab
    core.handle_event(ShellEvent::ActivateTab {
        tab_id: tab_id.clone(),
    });

    // Verify it transitions back to Active
    let tab = core.tab_manager().get_tab(&tab_id).unwrap();
    assert!(matches!(
        tab.state,
        maho_types::tab::TabLifecycleState::Active
    ));

    // Verify TabLifecycleChanged event is generated
    let collected = updates.borrow();
    let has_lifecycle_change = collected.iter().any(|u| match u {
        CoreUpdate::TabLifecycleChanged {
            tab_id: tid,
            state: maho_types::tab::TabLifecycleState::Active,
        } => tid == &tab_id,
        _ => false,
    });
    assert!(
        has_lifecycle_change,
        "ActivateTab must emit TabLifecycleChanged to Active"
    );
}

/// Transitioning a Normal tab to Favorite must seed `pinned_url` from the
/// tab's current URL (favorites are close-protected and need a durable URL).
#[test]
fn favorite_transition_seeds_pinned_url() {
    let mut core = MahoCore::new();
    let id = create_tab_id(&mut core, "https://a.example");

    core.transition_tab_role(&id, TabRole::Favorite { order: 1 });

    let tab = core.tab_manager().get_tab(&id).unwrap();
    assert!(
        tab.pinned_url.is_some(),
        "favorite transition must seed pinned_url"
    );
    assert_eq!(tab.pinned_url.as_ref(), Some(&tab.url));
}

/// Re-transitioning to the identical role must be a no-op: `pinned_url` stays
/// untouched and the MahoCore-level call emits no CoreUpdate.
#[test]
fn same_role_transition_is_noop() {
    let mut core = MahoCore::new();
    let id = create_tab_id(&mut core, "https://a.example");

    // Establish a known Favorite role with a seeded pinned_url.
    core.transition_tab_role(&id, TabRole::Favorite { order: 1 });
    let before = core.tab_manager().get_tab(&id).unwrap().pinned_url.clone();

    // Same role again → must be idempotent.
    let updates = core.transition_tab_role(&id, TabRole::Favorite { order: 1 });
    let after = core.tab_manager().get_tab(&id).unwrap().pinned_url.clone();

    assert_eq!(
        before, after,
        "pinned_url must be unchanged on same-role transition"
    );
    assert!(
        updates.is_empty(),
        "same-role transition must emit no CoreUpdate"
    );
}

/// Favoriting a tab that currently lives inside a folder must decouple it from
/// that folder and strip it from the root order.
#[test]
fn favorite_decouples_from_folder() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id().clone();
    let id = create_tab_id(&mut core, "https://a.example");

    let folder = core
        .space_manager_mut()
        .create_folder(&space_id, "Folder", false, None)
        .expect("folder created");
    core.space_manager_mut()
        .add_tab_to_folder(&space_id, &folder.id, id.clone());

    assert!(
        core.space_manager().is_tab_in_folder(&space_id, &id),
        "precondition: tab must be in the folder"
    );

    core.transition_tab_role(&id, TabRole::Favorite { order: 1 });

    assert!(
        !core.space_manager().is_tab_in_folder(&space_id, &id),
        "favorite must be decoupled from its folder"
    );
    let root_order = core.space_manager().get_root_order(&space_id);
    assert!(
        !root_order.contains(&RootItem::Tab(id.clone())),
        "favorite must be stripped from root order"
    );
}

/// `load_state` must backfill `pinned_url` for old-schema Pinned/Favorite tabs
/// that were persisted with `pinned_url == None`, while leaving Normal tabs alone.
#[test]
fn load_state_backfills_pinned_url() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    // Old-schema tabs: Pinned + Favorite + Normal, all with pinnedUrl == null.
    let tabs_json = r#"[
        {
            "id": "tab-pinned",
            "parentId": null,
            "spaceId": "space-test",
            "url": "https://pinned.example",
            "title": "Pinned",
            "customTitle": null,
            "favicon": null,
            "state": { "kind": "active" },
            "isMuted": false,
            "zoomLevel": 1.0,
            "createdAt": "2026-07-08T00:00:00Z",
            "lastActiveAt": "2026-07-08T00:00:00Z",
            "scrollPosition": {"x": 0, "y": 0},
            "pinnedUrl": null,
            "windowId": null,
            "isPinned": true,
            "isFavorite": false,
            "favoriteOrder": null
        },
        {
            "id": "tab-fav",
            "parentId": null,
            "spaceId": "space-test",
            "url": "https://fav.example",
            "title": "Fav",
            "customTitle": null,
            "favicon": null,
            "state": { "kind": "active" },
            "isMuted": false,
            "zoomLevel": 1.0,
            "createdAt": "2026-07-08T00:00:00Z",
            "lastActiveAt": "2026-07-08T00:00:00Z",
            "scrollPosition": {"x": 0, "y": 0},
            "pinnedUrl": null,
            "windowId": null,
            "isPinned": true,
            "isFavorite": true,
            "favoriteOrder": 1
        },
        {
            "id": "tab-normal",
            "parentId": null,
            "spaceId": "space-test",
            "url": "https://normal.example",
            "title": "Normal",
            "customTitle": null,
            "favicon": null,
            "state": { "kind": "active" },
            "isMuted": false,
            "zoomLevel": 1.0,
            "createdAt": "2026-07-08T00:00:00Z",
            "lastActiveAt": "2026-07-08T00:00:00Z",
            "scrollPosition": {"x": 0, "y": 0},
            "pinnedUrl": null,
            "windowId": null,
            "isPinned": false,
            "isFavorite": false,
            "favoriteOrder": null
        }
    ]"#;

    {
        let storage = maho_storage::lmdb::LmdbStorage::open(&storage_path).expect("init storage");
        storage.put("tabs", tabs_json.as_bytes()).unwrap();
        // Pin schema_version to current so migrations do not interfere with the
        // pinned_url backfill under test.
        let version_bytes = serde_json::to_vec(&3u32).unwrap();
        storage.put("schema_version", &version_bytes).unwrap();
    }

    let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
    core.load_state().expect("state should restore");

    let pinned = core
        .tab_manager()
        .get_tab(&TabId::new("tab-pinned"))
        .unwrap();
    assert_eq!(
        pinned.pinned_url.as_ref(),
        Some(&pinned.url),
        "pinned tab pinned_url must be backfilled from url"
    );

    let fav = core.tab_manager().get_tab(&TabId::new("tab-fav")).unwrap();
    assert_eq!(
        fav.pinned_url.as_ref(),
        Some(&fav.url),
        "favorite tab pinned_url must be backfilled from url"
    );

    let normal = core
        .tab_manager()
        .get_tab(&TabId::new("tab-normal"))
        .unwrap();
    assert_eq!(
        normal.pinned_url, None,
        "normal tab pinned_url must stay None"
    );
}

/// `is_tab_close_protected` must report true for BOTH Pinned and Favorite tabs
/// (close guard), false for Normal tabs and unknown ids.
#[test]
fn is_tab_close_protected_true_for_pinned_and_favorite() {
    let mut core = MahoCore::new();
    let pinned = create_tab_id(&mut core, "https://pinned.example");
    let fav = create_tab_id(&mut core, "https://fav.example");
    let normal = create_tab_id(&mut core, "https://normal.example");
    core.handle_event(ShellEvent::PinTab {
        tab_id: pinned.clone(),
    });
    core.handle_event(ShellEvent::FavoriteTab {
        tab_id: fav.clone(),
    });

    assert!(core.is_tab_close_protected(pinned.as_ref()));
    assert!(core.is_tab_close_protected(fav.as_ref()));
    assert!(!core.is_tab_close_protected(normal.as_ref()));
    assert!(!core.is_tab_close_protected("nonexistent-id"));
}

/// Closing a favorite must put it to sleep at its home (pinned) URL so the
/// next open lands on the pinned page instead of the last visited page.
#[test]
fn closing_a_favorite_sleeps_it_at_its_pinned_url() {
    let mut core = MahoCore::new();
    let tab_id = create_tab_id(&mut core, "https://home.example.com");
    core.handle_event(ShellEvent::FavoriteTab {
        tab_id: tab_id.clone(),
    });

    // The user authors a distinct home URL, then navigates the tab away.
    let json = format!(
        r#"{{"kind":"set_tab_pinned_url","tab_id":"{}","url":"https://pinned.example.com"}}"#,
        tab_id.0
    );
    core.handle_event(serde_json::from_str::<ShellEvent>(&json).expect("set pinned url must parse"));
    core.handle_event(ShellEvent::TabUrlUpdated {
        tab_id: tab_id.clone(),
        url: Url::new("https://elsewhere.example.com/page"),
    });

    let captured: std::rc::Rc<std::cell::RefCell<Vec<CoreUpdate>>> =
        std::rc::Rc::new(std::cell::RefCell::new(Vec::new()));
    let sink = captured.clone();
    core.on_update(Box::new(move |update| sink.borrow_mut().push(update)));

    core.handle_event(ShellEvent::CloseTab {
        tab_id: tab_id.clone(),
        expected_space_id: None,
    });

    let tab = core
        .tab_manager()
        .get_tab(&tab_id)
        .expect("a closed favorite must survive as a sleeping ghost");
    assert!(
        matches!(tab.state, TabLifecycleState::Suspended { .. }),
        "closed favorite must be suspended, got: {:?}",
        tab.state
    );
    assert_eq!(
        tab.pinned_url,
        Some(Url::new("https://pinned.example.com")),
        "close must not clobber the authored home URL"
    );
    assert_eq!(
        tab.url,
        Url::new("https://pinned.example.com"),
        "closed favorite must sleep at its pinned URL"
    );
    assert!(
        captured.borrow().iter().any(|update| matches!(
            update,
            CoreUpdate::TabUpdated { tab_id: id, changes }
                if *id == tab_id && changes.url.as_deref() == Some("https://pinned.example.com")
        )),
        "the URL reset must be announced with TabUpdated, got: {:?}",
        captured.borrow()
    );
}

/// The shell's two-stage close dispatches suspend_tab directly: favorites reset
/// to their pinned URL, while pinned (non-favorite) tabs keep the live URL so
/// pinned_close_behavior stays authoritative.
#[test]
fn suspend_event_resets_favorites_only() {
    let mut core = MahoCore::new();
    let fav = create_tab_id(&mut core, "https://fav-home.example");
    core.handle_event(ShellEvent::FavoriteTab { tab_id: fav.clone() });
    let pinned = create_tab_id(&mut core, "https://pin-home.example");
    core.handle_event(ShellEvent::PinTab {
        tab_id: pinned.clone(),
    });

    let json = format!(
        r#"{{"kind":"set_tab_pinned_url","tab_id":"{}","url":"https://fav-pinned.example"}}"#,
        fav.0
    );
    core.handle_event(serde_json::from_str::<ShellEvent>(&json).expect("set pinned url must parse"));

    for tab_id in [fav.clone(), pinned.clone()] {
        core.handle_event(ShellEvent::TabUrlUpdated {
            tab_id,
            url: Url::new("https://elsewhere.example/live"),
        });
    }

    core.handle_event(ShellEvent::SuspendTab { tab_id: fav.clone() });
    core.handle_event(ShellEvent::SuspendTab {
        tab_id: pinned.clone(),
    });

    let fav_tab = core.tab_manager().get_tab(&fav).expect("favorite exists");
    assert!(matches!(fav_tab.state, TabLifecycleState::Suspended { .. }));
    assert_eq!(
        fav_tab.url,
        Url::new("https://fav-pinned.example"),
        "suspended favorite must reset to its pinned URL"
    );

    let pinned_tab = core
        .tab_manager()
        .get_tab(&pinned)
        .expect("pinned tab exists");
    assert!(matches!(
        pinned_tab.state,
        TabLifecycleState::Suspended { .. }
    ));
    assert_eq!(
        pinned_tab.url,
        Url::new("https://elsewhere.example/live"),
        "pinned tabs must keep the live URL on suspend"
    );
}

/// A restart must reopen favorites at their pinned URL: load_state resets the
/// live URL for favorites only, leaving pinned/normal tabs untouched.
#[test]
fn load_state_restores_favorites_at_their_pinned_url() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    let tabs_json = r#"[
        {
            "id": "tab-fav",
            "parentId": null,
            "spaceId": "space-test",
            "url": "https://last-visited.example/page",
            "title": "Fav",
            "customTitle": null,
            "favicon": null,
            "state": { "kind": "active" },
            "isMuted": false,
            "zoomLevel": 1.0,
            "createdAt": "2026-07-08T00:00:00Z",
            "lastActiveAt": "2026-07-08T00:00:00Z",
            "scrollPosition": {"x": 0, "y": 0},
            "pinnedUrl": "https://home.example",
            "windowId": null,
            "isPinned": true,
            "isFavorite": true,
            "favoriteOrder": 0
        },
        {
            "id": "tab-pinned",
            "parentId": null,
            "spaceId": "space-test",
            "url": "https://pin-last.example/page",
            "title": "Pinned",
            "customTitle": null,
            "favicon": null,
            "state": { "kind": "active" },
            "isMuted": false,
            "zoomLevel": 1.0,
            "createdAt": "2026-07-08T00:00:00Z",
            "lastActiveAt": "2026-07-08T00:00:00Z",
            "scrollPosition": {"x": 0, "y": 0},
            "pinnedUrl": "https://pin-home.example",
            "windowId": null,
            "isPinned": true,
            "isFavorite": false,
            "favoriteOrder": null
        },
        {
            "id": "tab-normal",
            "parentId": null,
            "spaceId": "space-test",
            "url": "https://normal.example/page",
            "title": "Normal",
            "customTitle": null,
            "favicon": null,
            "state": { "kind": "active" },
            "isMuted": false,
            "zoomLevel": 1.0,
            "createdAt": "2026-07-08T00:00:00Z",
            "lastActiveAt": "2026-07-08T00:00:00Z",
            "scrollPosition": {"x": 0, "y": 0},
            "pinnedUrl": null,
            "windowId": null,
            "isPinned": false,
            "isFavorite": false,
            "favoriteOrder": null
        }
    ]"#;

    {
        let storage = maho_storage::lmdb::LmdbStorage::open(&storage_path).expect("init storage");
        storage.put("tabs", tabs_json.as_bytes()).unwrap();
        let version_bytes = serde_json::to_vec(&3u32).unwrap();
        storage.put("schema_version", &version_bytes).unwrap();
    }

    let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
    core.load_state().expect("state should restore");

    let fav = core
        .tab_manager()
        .get_tab(&TabId::new("tab-fav"))
        .unwrap();
    assert_eq!(
        fav.url,
        Url::new("https://home.example"),
        "favorite must restart at its pinned URL"
    );
    assert_eq!(fav.pinned_url, Some(Url::new("https://home.example")));

    let pinned = core
        .tab_manager()
        .get_tab(&TabId::new("tab-pinned"))
        .unwrap();
    assert_eq!(
        pinned.url,
        Url::new("https://pin-last.example/page"),
        "pinned tabs must keep their live URL across a restart"
    );

    let normal = core
        .tab_manager()
        .get_tab(&TabId::new("tab-normal"))
        .unwrap();
    assert_eq!(normal.url, Url::new("https://normal.example/page"));
}
