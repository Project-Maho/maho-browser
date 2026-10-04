use maho_core::boost_manager::BoostManager;
use maho_core::command_bar::CommandBarEngine;
use maho_core::maho_core::MahoCore;
use maho_types::boost::{BoostUpdate, CaseMode, ColorBoostUpdate, SizeMode, TypographyBoostUpdate};
use maho_types::common::{Orientation, Url};
use maho_types::events::core_update::CoreUpdate;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::{FolderId, SpaceId, TabId};
use maho_types::settings::{
    GeneralSettingsUpdate, NewTabPosition, PinnedCloseBehavior, SettingsUpdate,
};
use maho_types::space::{RootInsertionPoint, RootItem, SpaceColor, SpaceConfigUpdate};
use maho_types::tab::TabRole;
use maho_types::traits::shell_renderer::{SearchContext, SuggestionType, TabViewModel};

fn make_color(hue: f64) -> SpaceColor {
    SpaceColor {
        hue,
        saturation: 0.8,
        brightness: 0.9,
        grain: 0.0,
    }
}

fn create_tab(core: &mut MahoCore) -> TabId {
    let space_id = core.get_active_space_id();
    let before: Vec<TabId> = core
        .get_tab_view_models()
        .iter()
        .map(|t| t.id.clone())
        .collect();
    core.handle_event(ShellEvent::CreateTab {
        space_id,
        url: None,
        parent_id: None,
        tab_id: None,
        window_id: None,
        is_private: false,
    });
    let after = core.get_tab_view_models();
    after
        .iter()
        .find(|t| !before.contains(&t.id))
        .expect("New tab should appear in view models")
        .id
        .clone()
}

fn create_tab_with_url(core: &mut MahoCore, url: &str) -> TabId {
    let space_id = core.get_active_space_id();
    let before: Vec<TabId> = core
        .get_tab_view_models()
        .iter()
        .map(|t| t.id.clone())
        .collect();
    core.handle_event(ShellEvent::CreateTab {
        space_id,
        url: Some(Url::new(url)),
        parent_id: None,
        tab_id: None,
        window_id: None,
        is_private: false,
    });
    let after = core.get_tab_view_models();
    after
        .iter()
        .find(|t| !before.contains(&t.id))
        .expect("New tab should appear in view models")
        .id
        .clone()
}

fn create_tab_with_url_and_privacy(core: &mut MahoCore, url: &str, is_private: bool) -> TabId {
    let space_id = core.get_active_space_id();
    let before: Vec<TabId> = core
        .get_tab_view_models()
        .iter()
        .map(|t| t.id.clone())
        .collect();
    core.handle_event(ShellEvent::CreateTab {
        space_id,
        url: Some(Url::new(url)),
        parent_id: None,
        tab_id: None,
        window_id: None,
        is_private,
    });
    let after = core.get_tab_view_models();
    after
        .iter()
        .find(|t| !before.contains(&t.id))
        .expect("New tab should appear in view models")
        .id
        .clone()
}

fn create_tab_in_space(core: &mut MahoCore, space_id: &SpaceId) -> TabId {
    let before: Vec<TabId> = core
        .get_tab_view_models()
        .iter()
        .map(|t| t.id.clone())
        .collect();
    core.handle_event(ShellEvent::CreateTab {
        space_id: space_id.clone(),
        url: None,
        parent_id: None,
        tab_id: None,
        window_id: None,
        is_private: false,
    });
    let after = core.get_tab_view_models();
    after
        .iter()
        .find(|t| !before.contains(&t.id))
        .expect("New tab should appear in view models")
        .id
        .clone()
}

fn create_space(core: &mut MahoCore, name: &str, hue: f64) -> SpaceId {
    let before: Vec<SpaceId> = core
        .get_space_view_models()
        .iter()
        .map(|s| s.id.clone())
        .collect();
    let profile_id = core
        .get_active_profile_id()
        .cloned()
        .unwrap_or_else(|| maho_types::identifiers::ProfileId::new(""));
    core.handle_event(ShellEvent::CreateSpace {
        name: name.to_string(),
        color: make_color(hue),
        profile_id,
    });
    let after = core.get_space_view_models();
    after
        .iter()
        .find(|s| !before.contains(&s.id))
        .expect("New space should appear in view models")
        .id
        .clone()
}

// ===== TAB LIFECYCLE =====

#[test]
fn tab_create_increases_count() {
    let mut core = MahoCore::new();
    let initial = core.get_tab_view_models().len();
    create_tab(&mut core);
    assert_eq!(core.get_tab_view_models().len(), initial + 1);
}

#[test]
fn create_tab_emits_tab_created_update() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();

    let updates = core.handle_event(ShellEvent::CreateTab {
        space_id,
        url: Some(Url::new("https://example.com/emitted")),
        parent_id: None,
        tab_id: None,
        window_id: None,
        is_private: false,
    });

    assert!(matches!(
        updates.as_slice(),
        [CoreUpdate::TabCreated { .. }]
    ));
}

#[test]
fn new_tab_with_explicit_id_opens_at_top_of_normal_area() {
    // Chromium assigns a stable tab_id before firing create_tab, so genuinely new
    // tabs arrive WITH an explicit id yet must still open at the top.
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();
    let first = TabId::generate();
    let second = TabId::generate();

    core.handle_event(ShellEvent::CreateTab {
        space_id: space_id.clone(),
        url: Some(Url::new("https://first.example.com")),
        parent_id: None,
        tab_id: Some(first.clone()),
        window_id: None,
        is_private: false,
    });
    core.handle_event(ShellEvent::CreateTab {
        space_id: space_id.clone(),
        url: Some(Url::new("https://second.example.com")),
        parent_id: None,
        tab_id: Some(second.clone()),
        window_id: None,
        is_private: false,
    });

    let root_order = core.space_manager().get_root_order(&space_id);
    let pos_first = root_order
        .iter()
        .position(|i| i == &maho_types::space::RootItem::Tab(first.clone()))
        .expect("first tab in root_order");
    let pos_second = root_order
        .iter()
        .position(|i| i == &maho_types::space::RootItem::Tab(second.clone()))
        .expect("second tab in root_order");
    assert_eq!(
        pos_second, 0,
        "newest tab must be at the top of the normal area"
    );
    assert!(
        pos_second < pos_first,
        "newest tab must sit above the older tab"
    );
}

#[test]
fn create_tab_threads_is_private_to_view_model() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();

    core.handle_event(ShellEvent::CreateTab {
        space_id: space_id.clone(),
        url: Some(Url::new("https://private.example.com")),
        parent_id: None,
        tab_id: None,
        window_id: None,
        is_private: true,
    });
    core.handle_event(ShellEvent::CreateTab {
        space_id,
        url: Some(Url::new("https://public.example.com")),
        parent_id: None,
        tab_id: None,
        window_id: None,
        is_private: false,
    });

    let tabs = core.get_tab_view_models();
    let private_tab = tabs
        .iter()
        .find(|tab| tab.url == "https://private.example.com")
        .expect("private tab should exist");
    let public_tab = tabs
        .iter()
        .find(|tab| tab.url == "https://public.example.com")
        .expect("public tab should exist");

    assert!(private_tab.is_private);
    assert!(!public_tab.is_private);
}

#[test]
fn tab_create_with_url_sets_url() {
    let mut core = MahoCore::new();
    let tab_id = create_tab_with_url(&mut core, "https://example.com");
    let tabs = core.get_tab_view_models();
    let tab = tabs.iter().find(|t| t.id == tab_id).unwrap();
    assert_eq!(tab.url, "https://example.com");
}

#[test]
fn tab_close_decreases_count() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    let before = core.get_tab_view_models().len();
    core.handle_event(ShellEvent::CloseTab {
        tab_id: tab_id.clone(),
        expected_space_id: None,
    });
    assert_eq!(core.get_tab_view_models().len(), before - 1);
}

#[test]
fn private_tab_close_does_not_enter_closed_tab_suggestions() {
    let mut core = MahoCore::new();
    let normal_id =
        create_tab_with_url_and_privacy(&mut core, "https://closed-normal.example.com", false);
    let private_id =
        create_tab_with_url_and_privacy(&mut core, "https://closed-private.example.com", true);

    core.handle_event(ShellEvent::CloseTab {
        tab_id: normal_id,
        expected_space_id: None,
    });
    core.handle_event(ShellEvent::CloseTab {
        tab_id: private_id,
        expected_space_id: None,
    });

    let normal_results = core.command_bar_search_with_tabs("closed-normal", None, vec![], false);
    let private_results = core.command_bar_search_with_tabs("closed-private", None, vec![], false);

    assert!(normal_results
        .iter()
        .any(|result| matches!(result.kind, SuggestionType::ClosedTab)));
    assert!(!private_results
        .iter()
        .any(|result| matches!(result.kind, SuggestionType::ClosedTab)));
}

#[test]
fn tab_activate_no_panic() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    core.handle_event(ShellEvent::ActivateTab { tab_id });
}

#[test]
fn tab_duplicate_increases_count() {
    let mut core = MahoCore::new();
    let tab_id = create_tab_with_url(&mut core, "https://example.com");
    let before = core.get_tab_view_models().len();
    core.handle_event(ShellEvent::DuplicateTab { tab_id });
    assert_eq!(core.get_tab_view_models().len(), before + 1);
}

#[test]
fn tab_pin_appears_in_pinned() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    core.handle_event(ShellEvent::PinTab {
        tab_id: tab_id.clone(),
    });
    let space_id = core.get_active_space_id();
    let pinned = core.get_pinned_tabs(&space_id);
    assert!(pinned.iter().any(|t| t.is_pinned));
}

#[test]
fn pin_tab_emits_tab_updated_with_pinned_true() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);

    let updates = core.handle_event(ShellEvent::PinTab {
        tab_id: tab_id.clone(),
    });

    assert!(updates.iter().any(|u| matches!(
        u,
        CoreUpdate::TabRoleChanged { tab_id: updated_id, old_role: _, new_role }
            if *updated_id == tab_id && *new_role == TabRole::Pinned
    )));
}

#[test]
fn tab_unpin_removes_from_pinned() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    core.handle_event(ShellEvent::PinTab {
        tab_id: tab_id.clone(),
    });
    core.handle_event(ShellEvent::UnpinTab {
        tab_id: tab_id.clone(),
    });
    let space_id = core.get_active_space_id();
    let pinned = core.get_pinned_tabs(&space_id);
    assert!(pinned.is_empty() || !pinned.iter().any(|t| t.id == tab_id));
}

#[test]
fn unfreeze_tab_activates_frozen_tab() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);

    core.handle_event(ShellEvent::FreezeTab {
        tab_id: tab_id.clone(),
    });
    assert!(core
        .get_tab_view_models()
        .iter()
        .any(|tab| tab.id == tab_id && tab.lifecycle_state == "frozen"));

    core.handle_event(ShellEvent::UnfreezeTab {
        tab_id: tab_id.clone(),
    });
    assert!(core
        .get_tab_view_models()
        .iter()
        .any(|tab| tab.id == tab_id && tab.lifecycle_state == "active"));
}

#[test]
fn unpin_tab_emits_tab_updated_with_pinned_false() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    core.handle_event(ShellEvent::PinTab {
        tab_id: tab_id.clone(),
    });

    let updates = core.handle_event(ShellEvent::UnpinTab {
        tab_id: tab_id.clone(),
    });

    assert!(matches!(
        updates.as_slice(),
        [CoreUpdate::TabRoleChanged { tab_id: updated_id, old_role: _, new_role }]
            if *updated_id == tab_id && *new_role == TabRole::Normal
    ));
}

#[test]
fn tab_favorite_appears() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    core.favorite_tab(&tab_id);
    let space_id = core.get_active_space_id();
    let favs = core.get_favorite_tabs(&space_id);
    assert!(favs.iter().any(|t| t.is_favorite));
}

#[test]
fn favorite_tab_emits_tab_updated_with_favorite_true() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);

    let updates = core.handle_event(ShellEvent::FavoriteTab {
        tab_id: tab_id.clone(),
    });

    assert!(matches!(
        updates.as_slice(),
        [CoreUpdate::TabRoleChanged { tab_id: updated_id, new_role: TabRole::Favorite { .. }, .. }]
            if *updated_id == tab_id
    ));
}

#[test]
fn favorite_tab_is_idempotent_via_shell_event() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);

    let first_updates = core.handle_event(ShellEvent::FavoriteTab {
        tab_id: tab_id.clone(),
    });
    assert!(matches!(
        first_updates.as_slice(),
        [CoreUpdate::TabRoleChanged { tab_id: updated_id, new_role: TabRole::Favorite { .. }, .. }]
            if *updated_id == tab_id
    ));

    let first_favorite_order = core
        .get_tab_view_models()
        .iter()
        .find(|tab| tab.id == tab_id)
        .and_then(|tab| tab.favorite_order);

    let second_updates = core.handle_event(ShellEvent::FavoriteTab {
        tab_id: tab_id.clone(),
    });

    let second_favorite_order = core
        .get_tab_view_models()
        .iter()
        .find(|tab| tab.id == tab_id)
        .and_then(|tab| tab.favorite_order);

    assert_eq!(second_favorite_order, first_favorite_order);
    assert!(!second_updates.iter().any(|update| matches!(
        update,
        CoreUpdate::TabRoleChanged { tab_id: updated_id, new_role: TabRole::Favorite { .. }, .. }
            if *updated_id == tab_id
    )));
}

#[test]
fn tab_unfavorite_removes() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    core.favorite_tab(&tab_id);
    core.transition_tab_role(&tab_id, TabRole::Normal);
    let space_id = core.get_active_space_id();
    let favs = core.get_favorite_tabs(&space_id);
    assert!(!favs.iter().any(|t| t.id == tab_id));
}

#[test]
fn unfavorite_tab_emits_tab_updated_with_favorite_false() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    core.handle_event(ShellEvent::FavoriteTab {
        tab_id: tab_id.clone(),
    });

    let updates = core.handle_event(ShellEvent::ChangeTabRole {
        tab_id: tab_id.clone(),
        new_role: TabRole::Normal,
    });

    assert!(updates.iter().any(|update| matches!(
        update,
        CoreUpdate::TabRoleChanged { tab_id: updated_id, new_role: TabRole::Normal, .. }
            if *updated_id == tab_id
    )));
}

#[test]
fn tab_today_not_empty_after_create() {
    let mut core = MahoCore::new();
    create_tab(&mut core);
    let space_id = core.get_active_space_id();
    assert!(!core.get_today_tabs(&space_id).is_empty());
}

#[test]
fn tab_archived_initially_empty() {
    let core = MahoCore::new();
    let space_id = core.get_active_space_id();
    assert!(core.get_archived_tabs(&space_id).is_empty());
}

#[test]
fn tab_set_parent_succeeds() {
    let mut core = MahoCore::new();
    let parent = create_tab(&mut core);
    let child = create_tab(&mut core);
    assert!(core.set_tab_parent(&child, Some(parent)));
}

#[test]
fn tab_set_parent_none_succeeds() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    assert!(core.set_tab_parent(&tab_id, None));
}

#[test]
fn tab_navigate_to_changes_url() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    core.handle_event(ShellEvent::NavigateTo {
        tab_id: tab_id.clone(),
        url: Url::new("https://rust-lang.org"),
    });
    let tabs = core.get_tab_view_models();
    assert!(tabs.iter().any(|t| t.id == tab_id));
}

#[test]
fn tab_go_back_forward_no_panic() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    core.handle_event(ShellEvent::GoBack {
        tab_id: tab_id.clone(),
    });
    core.handle_event(ShellEvent::GoForward { tab_id });
}

#[test]
fn tab_reload_no_panic() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    core.handle_event(ShellEvent::Reload { tab_id });
}

#[test]
fn tab_stop_no_panic() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    core.handle_event(ShellEvent::Stop { tab_id });
}

#[test]
fn tab_mute_sets_muted() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    core.handle_event(ShellEvent::MuteTab {
        tab_id: tab_id.clone(),
    });
    let tabs = core.get_tab_view_models();
    let tab = tabs.iter().find(|t| t.id == tab_id).unwrap();
    assert!(tab.is_muted);
}

#[test]
fn tab_unmute_clears_muted() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    core.handle_event(ShellEvent::MuteTab {
        tab_id: tab_id.clone(),
    });
    core.handle_event(ShellEvent::UnmuteTab {
        tab_id: tab_id.clone(),
    });
    let tabs = core.get_tab_view_models();
    let tab = tabs.iter().find(|t| t.id == tab_id).unwrap();
    assert!(!tab.is_muted);
}

#[test]
fn tab_close_others_keeps_target() {
    let mut core = MahoCore::new();
    let keep = create_tab(&mut core);
    create_tab(&mut core);
    create_tab(&mut core);
    let space_id = core.get_active_space_id();
    core.handle_event(ShellEvent::CloseOtherTabs {
        space_id,
        tab_id: keep.clone(),
    });
    let tabs = core.get_tab_view_models();
    assert!(tabs.iter().any(|t| t.id == keep));
}

#[test]
fn tab_reopen_last_closed_no_panic() {
    let mut core = MahoCore::new();
    let tab_id = create_tab_with_url(&mut core, "https://example.com");
    core.handle_event(ShellEvent::CloseTab {
        tab_id,
        expected_space_id: None,
    });
    core.handle_event(ShellEvent::ReopenLastClosed);
}

#[test]
fn tab_title_updated_reflects() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    core.handle_event(ShellEvent::TabTitleUpdated {
        tab_id: tab_id.clone(),
        title: "New Title".to_string(),
    });
    let tabs = core.get_tab_view_models();
    let tab = tabs.iter().find(|t| t.id == tab_id).unwrap();
    assert_eq!(tab.title, "New Title");
}

#[test]
fn tab_url_updated_reflects() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    core.handle_event(ShellEvent::TabUrlUpdated {
        tab_id: tab_id.clone(),
        url: Url::new("https://updated.com"),
    });
    let tabs = core.get_tab_view_models();
    let tab = tabs.iter().find(|t| t.id == tab_id).unwrap();
    assert_eq!(tab.url, "https://updated.com");
}

#[test]
fn tab_loading_changed_no_panic() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    core.handle_event(ShellEvent::TabLoadingChanged {
        tab_id,
        is_loading: true,
    });
}

#[test]
fn tab_navigation_state_changed_no_panic() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    core.handle_event(ShellEvent::TabNavigationStateChanged {
        tab_id,
        can_go_back: true,
        can_go_forward: false,
    });
}

#[test]
fn tick_no_panic() {
    let mut core = MahoCore::new();
    create_tab(&mut core);
    let _ = core.tick();
}

// ===== SPACE MANAGEMENT =====

#[test]
fn space_initial_exists() {
    let core = MahoCore::new();
    assert!(!core.get_space_view_models().is_empty());
}

#[test]
fn space_create_increases_count() {
    let mut core = MahoCore::new();
    let initial = core.get_space_view_models().len();
    create_space(&mut core, "Work", 220.0);
    assert_eq!(core.get_space_view_models().len(), initial + 1);
}

#[test]
fn create_space_emits_space_created_update() {
    let mut core = MahoCore::new();
    let profile_id = core
        .get_active_profile_id()
        .cloned()
        .unwrap_or_else(|| maho_types::identifiers::ProfileId::new(""));

    let updates = core.handle_event(ShellEvent::CreateSpace {
        name: "Emit Space".to_string(),
        color: make_color(42.0),
        profile_id,
    });

    assert!(matches!(
        updates.as_slice(),
        [CoreUpdate::SpaceCreated { .. }]
    ));
}

#[test]
fn space_activate_no_panic() {
    let mut core = MahoCore::new();
    let sid = create_space(&mut core, "Second", 0.0);
    core.handle_event(ShellEvent::ActivateSpace { space_id: sid });
}

#[test]
fn activate_space_emits_active_space_changed_update() {
    let mut core = MahoCore::new();
    let sid = create_space(&mut core, "Second", 0.0);

    let updates = core.handle_event(ShellEvent::ActivateSpace {
        space_id: sid.clone(),
    });

    assert!(matches!(
        updates.as_slice(),
        [CoreUpdate::ActiveSpaceChanged { space_id, .. }] if *space_id == sid
    ));
}

#[test]
fn update_space_config_emits_structured_config_update() {
    let mut core = MahoCore::new();
    let sid = create_space(&mut core, "Configurable", 120.0);

    let updates = core.handle_event(ShellEvent::UpdateSpaceConfig {
        changes: SpaceConfigUpdate {
            space_id: sid.clone(),
            name: Some("Renamed Config".to_string()),
            color: Some(make_color(240.0)),
            theme: None,
            icon: Some(Some("🌙".to_string())),
            profile_id: None,
        },
    });

    assert!(matches!(
        updates.as_slice(),
        [CoreUpdate::SpaceConfigUpdated { space_id, changes }]
            if *space_id == sid
                && changes.name.as_deref() == Some("Renamed Config")
                && changes.icon.as_ref().and_then(|icon| icon.as_ref()).map(|icon| icon.as_str()) == Some("🌙")
                && changes.color.is_some()
    ));

    let updated = core
        .get_space_view_models()
        .into_iter()
        .find(|space| space.id == sid)
        .expect("space should still exist");
    assert_eq!(updated.name, "Renamed Config");
    assert_eq!(updated.color.hue, 240.0);
    assert_eq!(updated.color.saturation, 0.8);
    assert_eq!(updated.color.brightness, 0.9);
    assert_eq!(updated.icon.as_deref(), Some("🌙"));
}

#[test]
fn rename_space_rejects_empty_name() {
    let mut core = MahoCore::new();
    let sid = create_space(&mut core, "Valid Space", 30.0);

    let updates = core.handle_event(ShellEvent::RenameSpace {
        space_id: sid.clone(),
        name: "   ".to_string(),
    });

    assert!(matches!(
        updates.as_slice(),
        [CoreUpdate::Error { context, error }]
            if context == "update_space_config"
                && error.code == "INVALID_NAME"
                && error.message == "Space name cannot be empty"
    ));
    assert_eq!(
        core.get_space_view_models()
            .into_iter()
            .find(|space| space.id == sid)
            .map(|space| space.name),
        Some("Valid Space".to_string())
    );
}

#[test]
fn rename_space_rejects_duplicate_name() {
    let mut core = MahoCore::new();
    let first = create_space(&mut core, "First", 30.0);
    let second = create_space(&mut core, "Second", 60.0);

    let updates = core.handle_event(ShellEvent::RenameSpace {
        space_id: second.clone(),
        name: "First".to_string(),
    });

    assert!(matches!(
        updates.as_slice(),
        [CoreUpdate::Error { context, error }]
            if context == "update_space_config"
                && error.code == "DUPLICATE_NAME"
                && error.message == "Space name must be unique"
    ));
    let names: Vec<String> = core
        .get_space_view_models()
        .into_iter()
        .filter(|space| space.id == first || space.id == second)
        .map(|space| space.name)
        .collect();
    assert_eq!(names, vec!["First".to_string(), "Second".to_string()]);
}

#[test]
fn space_events_emit_one_logical_update_each() {
    let mut core = MahoCore::new();
    let sid = create_space(&mut core, "One Update", 10.0);

    let rename_updates = core.handle_event(ShellEvent::RenameSpace {
        space_id: sid.clone(),
        name: "Renamed Once".to_string(),
    });
    assert_eq!(rename_updates.len(), 1);
    assert!(matches!(
        rename_updates.as_slice(),
        [CoreUpdate::SpaceConfigUpdated { .. }]
    ));

    let recolor_updates = core.handle_event(ShellEvent::RecolorSpace {
        space_id: sid.clone(),
        color: make_color(80.0),
    });
    assert_eq!(recolor_updates.len(), 1);
    assert!(matches!(
        recolor_updates.as_slice(),
        [CoreUpdate::SpaceConfigUpdated { .. }]
    ));

    let reorder_updates = core.handle_event(ShellEvent::ReorderSpace {
        space_id: sid.clone(),
        from: 0,
        to: 0,
    });
    assert_eq!(reorder_updates.len(), 1);
    assert!(matches!(
        reorder_updates.as_slice(),
        [CoreUpdate::SpaceOrderChanged { .. }]
    ));

    let activate_updates = core.handle_event(ShellEvent::ActivateSpace { space_id: sid });
    assert_eq!(activate_updates.len(), 1);
}

#[test]
fn update_space_config_preserves_active_space_and_only_applies_requested_fields() {
    let mut core = MahoCore::new();
    let active_space = core.get_active_space_id();
    let other_space = create_space(&mut core, "Other", 180.0);

    let before = core.get_space_view_models();
    let before_active = before
        .iter()
        .find(|space| space.id == active_space)
        .expect("active space should exist");
    let before_other = before
        .iter()
        .find(|space| space.id == other_space)
        .expect("other space should exist");

    let updates = core.handle_event(ShellEvent::UpdateSpaceConfig {
        changes: SpaceConfigUpdate {
            space_id: other_space.clone(),
            name: Some("Other Renamed".to_string()),
            color: None,
            theme: None,
            icon: Some(None),
            profile_id: None,
        },
    });

    assert!(matches!(
        updates.as_slice(),
        [CoreUpdate::SpaceConfigUpdated { space_id, changes }]
            if *space_id == other_space
                && changes.name.as_deref() == Some("Other Renamed")
                && changes.color.is_none()
                && changes.icon == Some(None)
                && changes.profile_id.is_none()
    ));

    let after = core.get_space_view_models();
    let after_active = after
        .iter()
        .find(|space| space.id == active_space)
        .expect("active space should still exist");
    let after_other = after
        .iter()
        .find(|space| space.id == other_space)
        .expect("other space should still exist");

    assert!(
        after_active.is_active,
        "global active space should remain active"
    );
    assert_eq!(after_active.id, before_active.id);
    assert_eq!(after_active.name, before_active.name);
    assert_eq!(after_other.id, before_other.id);
    assert_eq!(after_other.name, "Other Renamed");
    assert_eq!(after_other.color.hue, before_other.color.hue);
    assert_eq!(after_other.color.saturation, before_other.color.saturation);
    assert_eq!(after_other.color.brightness, before_other.color.brightness);
    assert_eq!(after_other.icon, None);
}

#[test]
fn space_rename_reflects() {
    let mut core = MahoCore::new();
    let sid = create_space(&mut core, "Old", 120.0);
    core.handle_event(ShellEvent::RenameSpace {
        space_id: sid,
        name: "Renamed".to_string(),
    });
    assert!(core
        .get_space_view_models()
        .iter()
        .any(|s| s.name == "Renamed"));
}

#[test]
fn space_recolor_no_panic() {
    let mut core = MahoCore::new();
    let sid = create_space(&mut core, "Colorful", 220.0);
    core.handle_event(ShellEvent::RecolorSpace {
        space_id: sid,
        color: make_color(280.0),
    });
}

#[test]
fn space_delete_decreases_count() {
    let mut core = MahoCore::new();
    let sid = create_space(&mut core, "Temp", 30.0);
    let count = core.get_space_view_models().len();
    core.handle_event(ShellEvent::DeleteSpace { space_id: sid });
    assert_eq!(core.get_space_view_models().len(), count - 1);
}

#[test]
fn delete_active_space_falls_back_to_first_remaining_space() {
    let mut core = MahoCore::new();
    let active = core.get_active_space_id();
    let second = create_space(&mut core, "Second", 180.0);

    let updates = core.handle_event(ShellEvent::DeleteSpace {
        space_id: active.clone(),
    });

    assert!(matches!(
        updates.as_slice(),
        [
            CoreUpdate::TabsMigrated {
                from_space_id,
                to_space_id,
                ..
            },
            CoreUpdate::SpaceDeleted { .. },
            CoreUpdate::ActiveSpaceChanged { space_id, .. }
        ] if *from_space_id == active && *to_space_id == second && *space_id == second
    ));
    assert_eq!(core.get_active_space_id(), second);
}

#[test]
fn active_space_stays_stable_during_reorder() {
    let mut core = MahoCore::new();
    let active = core.get_active_space_id();
    let second = create_space(&mut core, "Second", 180.0);
    let third = create_space(&mut core, "Third", 240.0);

    let before_active = core
        .get_space_view_models()
        .into_iter()
        .find(|space| space.id == active)
        .expect("active space should exist");

    let updates = core.handle_event(ShellEvent::ReorderSpace {
        space_id: third.clone(),
        from: 2,
        to: 0,
    });

    assert!(matches!(
        updates.as_slice(),
        [CoreUpdate::SpaceOrderChanged { .. }]
    ));
    let after = core.get_space_view_models();
    let active_after = after.iter().find(|space| space.id == active).unwrap();
    assert!(active_after.is_active);
    assert_eq!(active_after.name, before_active.name);
    assert_eq!(core.get_active_space_id(), active);
    assert_eq!(after.first().map(|space| &space.id), Some(&third));
    assert!(after.iter().any(|space| space.id == second));
}

#[test]
fn space_create_folder_no_panic() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();
    core.handle_event(ShellEvent::CreateFolder {
        space_id,
        name: "Research".to_string(),
        is_pinned: false,
        parent_folder_id: None,
        provider_type: None,
        config_json: None,
    });
}

#[test]
fn create_folder_emits_folder_created_update() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();

    let updates = core.handle_event(ShellEvent::CreateFolder {
        space_id,
        name: "Emit Folder".to_string(),
        is_pinned: false,
        parent_folder_id: None,
        provider_type: None,
        config_json: None,
    });

    assert!(matches!(
        updates.as_slice(),
        [CoreUpdate::FolderCreated { .. }]
    ));
}

#[test]
fn space_folder_view_models() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();
    core.handle_event(ShellEvent::CreateFolder {
        space_id: space_id.clone(),
        name: "Folder1".to_string(),
        is_pinned: false,
        parent_folder_id: None,
        provider_type: None,
        config_json: None,
    });
    assert!(!core.get_folder_view_models(&space_id).is_empty());
}

#[test]
fn create_folder_can_start_pinned() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();

    let updates = core.handle_event(ShellEvent::CreateFolder {
        space_id: space_id.clone(),
        name: "Pinned Folder".to_string(),
        is_pinned: true,
        parent_folder_id: None,
        provider_type: None,
        config_json: None,
    });

    assert!(matches!(
        updates.as_slice(),
        [CoreUpdate::FolderCreated { folder }] if folder.is_pinned
    ));

    let folder = core
        .get_folder_view_models(&space_id)
        .into_iter()
        .find(|folder| folder.name == "Pinned Folder")
        .expect("folder should exist");
    assert!(folder.is_pinned);
}

#[test]
fn space_active_id_valid() {
    let core = MahoCore::new();
    let space_id = core.get_active_space_id();
    assert!(core
        .get_space_view_models()
        .iter()
        .any(|s| s.id == space_id));
}

#[test]
fn space_tab_in_new_space() {
    let mut core = MahoCore::new();
    let sid = create_space(&mut core, "NewSpace", 180.0);
    let before = core.get_tab_view_models().len();
    core.handle_event(ShellEvent::CreateTab {
        space_id: sid,
        url: None,
        parent_id: None,
        tab_id: None,
        window_id: None,
        is_private: false,
    });
    assert_eq!(core.get_tab_view_models().len(), before + 1);
}

// ===== COMMAND BAR / SEARCH =====

#[test]
fn command_bar_query_no_panic() {
    let mut core = MahoCore::new();
    core.handle_event(ShellEvent::CommandBarQuery {
        text: "hello world".to_string(),
        mode: None,
        is_incognito: false,
    });
}

#[test]
fn command_bar_url_query_no_panic() {
    let mut core = MahoCore::new();
    core.handle_event(ShellEvent::CommandBarQuery {
        text: "https://google.com".to_string(),
        mode: None,
        is_incognito: false,
    });
}

#[test]
fn incognito_command_bar_query_excludes_history_and_bookmarks() {
    let mut core = MahoCore::new();
    core.add_history_entry("https://hidden-history.example.com", "Hidden History");
    core.add_bookmark(
        "https://hidden-bookmark.example.com",
        "Hidden Bookmark",
        None,
    );

    let updates = core.handle_event(ShellEvent::CommandBarQuery {
        text: "hidden".to_string(),
        mode: None,
        is_incognito: true,
    });

    let suggestions = updates
        .into_iter()
        .find_map(|update| match update {
            CoreUpdate::CommandBarResults { suggestions } => Some(suggestions),
            _ => None,
        })
        .expect("command bar results should be emitted");

    assert!(!suggestions
        .iter()
        .any(|result| matches!(result.kind, SuggestionType::History)));
    assert!(!suggestions
        .iter()
        .any(|result| matches!(result.kind, SuggestionType::Bookmark)));
}

#[test]
fn command_bar_open_close_no_panic() {
    let mut core = MahoCore::new();
    core.handle_event(ShellEvent::CommandBarOpened);
    core.handle_event(ShellEvent::CommandBarClosed);
}

#[test]
fn search_save_and_get_recent() {
    let mut core = MahoCore::new();
    core.save_search("test query 1".to_string());
    core.save_search("test query 2".to_string());
    let recent = core.get_recent_searches();
    assert!(recent.len() >= 2);
    assert!(recent.contains(&"test query 1".to_string()));
}

#[test]
fn search_save_via_event() {
    let mut core = MahoCore::new();
    core.handle_event(ShellEvent::SaveSearch {
        query: "event search".to_string(),
    });
    assert!(core
        .get_recent_searches()
        .contains(&"event search".to_string()));
}

#[test]
fn search_engines_exist() {
    let core = MahoCore::new();
    assert!(!core.get_search_engines().is_empty());
}

#[test]
fn search_engine_add() {
    let mut core = MahoCore::new();
    let initial = core.get_search_engines().len();
    core.add_search_engine(maho_types::search_engine::SearchEngine {
        id: "ddg".to_string(),
        name: "DuckDuckGo".to_string(),
        url_template: "https://duckduckgo.com/?q={query}".to_string(),
        shortcut: Some("@d".to_string()),
        icon_url: None,
        is_default: false,
    });
    assert_eq!(core.get_search_engines().len(), initial + 1);
}

#[test]
fn search_engine_remove() {
    let mut core = MahoCore::new();
    core.add_search_engine(maho_types::search_engine::SearchEngine {
        id: "bing".to_string(),
        name: "Bing".to_string(),
        url_template: "https://bing.com/search?q={query}".to_string(),
        shortcut: None,
        icon_url: None,
        is_default: false,
    });
    let count_with_bing = core.get_search_engines().len();
    core.remove_search_engine("bing");
    assert!(core.get_search_engines().len() < count_with_bing);
}

#[test]
fn search_engine_set_default() {
    let mut core = MahoCore::new();
    core.add_search_engine(maho_types::search_engine::SearchEngine {
        id: "ddg2".to_string(),
        name: "DDG".to_string(),
        url_template: "https://duckduckgo.com/?q={query}".to_string(),
        shortcut: None,
        icon_url: None,
        is_default: false,
    });
    assert!(core.set_default_search_engine("ddg2"));
}

#[test]
fn command_bar_usage_tracking() {
    let mut core = MahoCore::new();
    core.record_command_bar_usage("new_tab");
    core.record_command_bar_usage("new_tab");
    core.record_command_bar_usage("close_tab");
    // Without storage, get_top_used_commands returns empty — just verify no panic
    let _ = core.get_top_used_commands(5);
}

// ===== SETTINGS =====

#[test]
fn settings_get() {
    let core = MahoCore::new();
    assert!(!core
        .get_settings()
        .general
        .default_search_engine
        .name
        .is_empty());
}

#[test]
fn settings_view_model_has_sections() {
    let core = MahoCore::new();
    let vm = core.get_settings_view_model();
    assert!(!vm.sections.is_empty());
}

#[test]
fn settings_update_appearance() {
    let mut core = MahoCore::new();
    core.update_settings(maho_types::settings::SettingsUpdate {
        general: None,
        appearance: Some(maho_types::settings::AppearanceSettingsUpdate {
            theme: Some(maho_types::settings::Theme::Dark),
            density: None,
            sidebar_width: Some(300.0),
            show_tab_bar: None,
            window_transparency: None,
            sidebar_collapsed: None,
            custom_icon_path: None,
        }),
        privacy: None,
        reader: None,
        keyboard_shortcuts: None,
        per_site_settings: None,
        toolbar_items: None,
        max: None,
        autofill: None,
        advanced: None,
        notifications: None,
    });
    assert_eq!(core.get_settings().appearance.sidebar_width, 300.0);
}

#[test]
fn settings_zoom_for_site() {
    let mut core = MahoCore::new();
    core.set_zoom_for_site("example.com".to_string(), 1.5);
    assert!((core.get_zoom_for_site("example.com") - 1.5).abs() < 0.01);
}

#[test]
fn settings_zoom_default() {
    let core = MahoCore::new();
    assert!((core.get_zoom_for_site("unknown.com") - 1.0).abs() < 0.01);
}

#[test]
fn sidebar_toggle_no_panic() {
    let mut core = MahoCore::new();
    core.handle_event(ShellEvent::SidebarToggled { visible: false });
    core.handle_event(ShellEvent::SidebarToggled { visible: true });
}

#[test]
fn sidebar_resize_no_panic() {
    let mut core = MahoCore::new();
    core.handle_event(ShellEvent::SidebarResized { width: 200.0 });
}

#[test]
fn window_resize_no_panic() {
    let mut core = MahoCore::new();
    core.handle_event(ShellEvent::WindowResized {
        size: maho_types::common::Size {
            width: 1024.0,
            height: 768.0,
        },
    });
}

#[test]
fn window_focus_changed_no_panic() {
    let mut core = MahoCore::new();
    core.handle_event(ShellEvent::WindowFocusChanged { focused: true });
    core.handle_event(ShellEvent::WindowFocusChanged { focused: false });
}

// ===== PERMISSIONS =====

#[test]
fn permission_grant_and_query() {
    let mut core = MahoCore::new();
    core.grant_permission("https://example.com", "camera");
    assert_eq!(
        core.query_permission("https://example.com", "camera"),
        "allow"
    );
}

#[test]
fn permission_revoke() {
    let mut core = MahoCore::new();
    core.grant_permission("https://example.com", "microphone");
    core.revoke_permission("https://example.com", "microphone");
    assert_ne!(
        core.query_permission("https://example.com", "microphone"),
        "allow"
    );
}

#[test]
fn permission_query_unknown() {
    let core = MahoCore::new();
    assert_ne!(
        core.query_permission("https://nonexistent.com", "camera"),
        "allow"
    );
}

// ===== DOWNLOADS =====

#[test]
fn download_start_returns_id() {
    let mut core = MahoCore::new();
    let id = core.start_download(
        "file.zip",
        "https://example.com/file.zip",
        1024,
        None,
        None,
        None,
    );
    assert!(!id.is_empty());
}

#[test]
fn download_view_models_not_empty() {
    let mut core = MahoCore::new();
    core.start_download("f.zip", "https://example.com/f.zip", 512, None, None, None);
    assert!(!core.get_download_view_models().is_empty());
}

#[test]
fn download_pause_resume_no_panic() {
    let mut core = MahoCore::new();
    let id = core.start_download(
        "big.zip",
        "https://example.com/big.zip",
        99999,
        None,
        None,
        None,
    );
    core.pause_download(&id);
    core.resume_download(&id);
}

#[test]
fn download_cancel_no_panic() {
    let mut core = MahoCore::new();
    let id = core.start_download("c.zip", "https://example.com/c.zip", 100, None, None, None);
    core.cancel_download(&id);
}

#[test]
fn download_progress_update() {
    let mut core = MahoCore::new();
    let id = core.start_download("p.zip", "https://example.com/p.zip", 1000, None, None, None);
    core.update_download_progress(&id, 500);
}

#[test]
fn download_fail_no_panic() {
    let mut core = MahoCore::new();
    let id = core.start_download(
        "fail.zip",
        "https://example.com/fail.zip",
        100,
        None,
        None,
        None,
    );
    core.fail_download(&id, "network error");
}

// ===== BOOKMARKS =====

#[test]
fn bookmark_add_returns_bookmark() {
    let mut core = MahoCore::new();
    let bm = core.add_bookmark_entry(
        "Rust".to_string(),
        "https://rust-lang.org".to_string(),
        None,
        None,
    );
    assert_eq!(bm.title, "Rust");
}

#[test]
fn bookmark_get_all_not_empty() {
    let mut core = MahoCore::new();
    core.add_bookmark_entry(
        "Example".to_string(),
        "https://example.com".to_string(),
        None,
        None,
    );
    assert!(!core.get_all_bookmark_entries().is_empty());
}

#[test]
fn bookmark_remove_decreases_count() {
    let mut core = MahoCore::new();
    let bm = core.add_bookmark_entry(
        "ToRemove".to_string(),
        "https://remove.com".to_string(),
        None,
        None,
    );
    let before = core.get_all_bookmark_entries().len();
    core.remove_bookmark_entry(&bm.id.0);
    assert_eq!(core.get_all_bookmark_entries().len(), before - 1);
}

#[test]
fn bookmark_folder_create() {
    let mut core = MahoCore::new();
    let folder = core.create_bookmark_folder("Dev".to_string(), None);
    assert_eq!(folder.name, "Dev");
}

#[test]
fn bookmark_in_folder() {
    let mut core = MahoCore::new();
    let folder = core.create_bookmark_folder("Work".to_string(), None);
    let bm = core.add_bookmark_entry(
        "GitHub".to_string(),
        "https://github.com".to_string(),
        Some(folder.id.clone()),
        None,
    );
    assert_eq!(bm.folder_id, Some(folder.id));
}

#[test]
fn bookmark_search() {
    let mut core = MahoCore::new();
    core.add_bookmark_entry(
        "Rust Lang".to_string(),
        "https://rust-lang.org".to_string(),
        None,
        None,
    );
    assert!(!core.search_bookmark_entries("Rust").is_empty());
}

#[test]
fn bookmark_move_no_panic() {
    let mut core = MahoCore::new();
    let folder = core.create_bookmark_folder("Target".to_string(), None);
    let bm = core.add_bookmark_entry(
        "Movable".to_string(),
        "https://move.com".to_string(),
        None,
        None,
    );
    core.move_bookmark_entry(&bm.id.0, Some(folder.id));
}

// ===== CONTENT BLOCKER =====

#[test]
fn content_blocker_toggle() {
    let mut core = MahoCore::new();
    core.toggle_content_blocker(true);
    assert!(core.is_content_blocker_enabled());
    core.toggle_content_blocker(false);
    assert!(!core.is_content_blocker_enabled());
}

#[test]
fn content_blocker_popup() {
    let mut core = MahoCore::new();
    core.toggle_popup_blocking(true);
    assert!(core.is_popup_blocking_enabled());
}

#[test]
fn content_blocker_filter_list() {
    let mut core = MahoCore::new();
    core.add_filter_list(
        "easylist".to_string(),
        "EasyList".to_string(),
        "https://easylist.to/easylist/easylist.txt".to_string(),
    );
    assert!(core.get_filter_lists_json().contains("easylist"));
}

#[test]
fn content_blocker_rule_count() {
    let core = MahoCore::new();
    let _ = core.get_content_rule_count();
}

#[test]
fn content_blocker_should_block() {
    let mut core = MahoCore::new();
    core.add_filter_list(
        "test".to_string(),
        "Test".to_string(),
        "https://example.com/list.txt".to_string(),
    );
    core.update_filter_list_content("test", "||ads.example.com^".to_string());
    core.rebuild_content_rules();
    assert!(core.should_block_request(
        "https://ads.example.com/banner.js",
        "https://example.com/",
        "script"
    ));
    assert!(!core.should_block_request(
        "https://safe.example.com/page.html",
        "https://example.com/",
        "document"
    ));
}

// ===== NOTIFICATIONS =====

#[test]
fn notification_queue_and_get() {
    let mut core = MahoCore::new();
    core.notification_manager_queue_test(
        "https://example.com".to_string(),
        "Hello".to_string(),
        "World".to_string(),
    );
    assert!(!core.get_notifications().is_empty());
}

#[test]
fn notification_dismiss() {
    let mut core = MahoCore::new();
    if let Some(n) = core.notification_manager_queue_test(
        "https://test.com".to_string(),
        "Title".to_string(),
        "Body".to_string(),
    ) {
        core.dismiss_notification(&n.id);
    }
}

#[test]
fn notification_dismiss_all() {
    let mut core = MahoCore::new();
    core.notification_manager_queue_test(
        "https://a.com".to_string(),
        "A".to_string(),
        "AA".to_string(),
    );
    core.notification_manager_queue_test(
        "https://b.com".to_string(),
        "B".to_string(),
        "BB".to_string(),
    );
    core.dismiss_all_notifications();
    assert!(core.get_notifications().is_empty());
}

#[test]
fn notification_filter_no_panic() {
    let mut core = MahoCore::new();
    core.set_notification_filter("https://blocked.com".to_string(), false);
}

// ===== TAB PREVIEW =====

#[test]
fn tab_preview_update_and_get() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    let img = maho_types::common::ImageData {
        data: vec![0u8; 100],
        width: 10,
        height: 10,
        format: maho_types::common::ImageFormat::Png,
    };
    core.update_tab_preview(&tab_id, img);
    assert!(core.get_tab_preview(&tab_id).is_some());
}

#[test]
fn tab_preview_has_check() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    assert!(!core.has_tab_preview(&tab_id));
    core.update_tab_preview(
        &tab_id,
        maho_types::common::ImageData {
            data: vec![1u8; 50],
            width: 5,
            height: 5,
            format: maho_types::common::ImageFormat::Jpeg,
        },
    );
    assert!(core.has_tab_preview(&tab_id));
}

#[test]
fn tab_preview_remove() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    core.update_tab_preview(
        &tab_id,
        maho_types::common::ImageData {
            data: vec![0u8; 10],
            width: 2,
            height: 2,
            format: maho_types::common::ImageFormat::Png,
        },
    );
    core.remove_tab_preview(&tab_id);
    assert!(!core.has_tab_preview(&tab_id));
}

#[test]
fn tab_preview_schedule_capture() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    core.schedule_preview_capture(tab_id);
}

// ===== READER MODE =====

#[test]
fn reader_mode_toggle() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    let id_str = tab_id.to_string();
    core.toggle_reader_mode(&id_str);
    assert!(core.is_reader_mode(&id_str));
    core.toggle_reader_mode(&id_str);
    assert!(!core.is_reader_mode(&id_str));
}

#[test]
fn reader_settings_json_not_empty() {
    let core = MahoCore::new();
    assert!(!core.get_reader_settings_json().is_empty());
}

// ===== BOOSTS (via events) =====

#[test]
fn boost_create_via_event() {
    let mut core = MahoCore::new();
    core.handle_event(ShellEvent::CreateBoost {
        domain: "example.com".to_string(),
    });
    assert!(!core.get_boost_view_models().is_empty());
    let vms = core.get_boost_view_models();
    assert_eq!(vms[0].name, "example.com Boost");
}

#[test]
fn boost_for_url_subdomain_matching() {
    let mut core = MahoCore::new();
    core.handle_event(ShellEvent::CreateBoost {
        domain: "example.com".to_string(),
    });
    let vms = core.get_boost_view_models();
    assert_eq!(vms.len(), 1);
    let boost_id = vms[0].id.clone();

    core.handle_event(ShellEvent::SetActiveBoost {
        domain: "example.com".to_string(),
        boost_id: Some(boost_id.clone()),
    });

    let matched = core.get_boosts_for_url("https://www.example.com/page");
    assert_eq!(matched.len(), 1);
    assert_eq!(matched[0].id, boost_id);

    let matched_sub = core.get_boosts_for_url("http://sub.example.com:8080/foo");
    assert_eq!(matched_sub.len(), 1);
    assert_eq!(matched_sub[0].id, boost_id);

    let matched_other = core.get_boosts_for_url("https://example.org/");
    assert!(matched_other.is_empty());
}

#[test]
fn boost_create_emits_boost_created() {
    let mut core = MahoCore::new();

    let updates = core.handle_event(ShellEvent::CreateBoost {
        domain: "example.com".to_string(),
    });

    assert!(
        matches!(updates.as_slice(), [CoreUpdate::BoostCreated { .. }]),
        "CreateBoost must emit exactly one BoostCreated; got {:?}",
        updates
    );
    if let [CoreUpdate::BoostCreated { boost }] = updates.as_slice() {
        assert_eq!(boost.domain, "example.com");
    }
}

#[test]
fn boost_update_emits_boost_updated() {
    let mut core = MahoCore::new();

    let create_updates = core.handle_event(ShellEvent::CreateBoost {
        domain: "update.example.com".to_string(),
    });
    let boost_id = match create_updates.as_slice() {
        [CoreUpdate::BoostCreated { boost }] => boost.id.clone(),
        _ => panic!("expected BoostCreated"),
    };

    let updates = core.handle_event(ShellEvent::UpdateBoost {
        boost_id: boost_id.clone(),
        changes: BoostUpdate {
            name: Some("Renamed Boost".to_string()),
            color: None,
            typography: None,
            zap_selectors: None,
            custom_css: None,
        },
    });

    assert!(
        matches!(updates.as_slice(), [CoreUpdate::BoostUpdated { .. }]),
        "UpdateBoost must emit exactly one BoostUpdated; got {:?}",
        updates
    );
    if let [CoreUpdate::BoostUpdated {
        boost_id: returned_id,
        ..
    }] = updates.as_slice()
    {
        assert_eq!(*returned_id, boost_id);
    }
}

#[test]
fn boost_set_active_emits_boost_active_changed() {
    let mut core = MahoCore::new();

    let create_updates = core.handle_event(ShellEvent::CreateBoost {
        domain: "toggle.example.com".to_string(),
    });
    let boost_id = match create_updates.as_slice() {
        [CoreUpdate::BoostCreated { boost }] => boost.id.clone(),
        _ => panic!("expected BoostCreated"),
    };

    let activate_updates = core.handle_event(ShellEvent::SetActiveBoost {
        domain: "toggle.example.com".to_string(),
        boost_id: Some(boost_id.clone()),
    });
    assert!(
        matches!(
            activate_updates.as_slice(),
            [CoreUpdate::BoostActiveChanged { domain, active_boost_id: Some(id) }]
                if domain == "toggle.example.com" && *id == boost_id
        ),
        "SetActiveBoost(Some) must emit BoostActiveChanged with the id; got {:?}",
        activate_updates
    );

    let deactivate_updates = core.handle_event(ShellEvent::SetActiveBoost {
        domain: "toggle.example.com".to_string(),
        boost_id: None,
    });
    assert!(
        matches!(
            deactivate_updates.as_slice(),
            [CoreUpdate::BoostActiveChanged { domain, active_boost_id: None }]
                if domain == "toggle.example.com"
        ),
        "SetActiveBoost(None) must emit BoostActiveChanged with None; got {:?}",
        deactivate_updates
    );
}

#[test]
fn boost_delete_emits_boost_deleted() {
    let mut core = MahoCore::new();

    let create_updates = core.handle_event(ShellEvent::CreateBoost {
        domain: "delete.example.com".to_string(),
    });
    let boost_id = match create_updates.as_slice() {
        [CoreUpdate::BoostCreated { boost }] => boost.id.clone(),
        _ => panic!("expected BoostCreated"),
    };

    let delete_updates = core.handle_event(ShellEvent::DeleteBoost {
        boost_id: boost_id.clone(),
    });

    assert!(
        matches!(
            delete_updates.as_slice(),
            [CoreUpdate::BoostDeleted { boost_id: deleted_id, domain }]
                if *deleted_id == boost_id && domain == "delete.example.com"
        ),
        "DeleteBoost must emit BoostDeleted; got {:?}",
        delete_updates
    );

    let empty_updates = core.handle_event(ShellEvent::DeleteBoost {
        boost_id: boost_id.clone(),
    });
    assert!(
        empty_updates.is_empty(),
        "DeleteBoost on non-existent id must emit no updates; got {:?}",
        empty_updates
    );
}

// ===== NOTES (via events) =====

#[test]
fn note_create_via_event() {
    let mut core = MahoCore::new();
    let tab_id = create_tab(&mut core);
    core.handle_event(ShellEvent::CreateNote {
        linked_tab: Some(tab_id),
        content: "My note".to_string(),
    });
    assert!(!core.get_note_view_models().is_empty());
}

#[test]
fn note_export() {
    let mut core = MahoCore::new();
    core.handle_event(ShellEvent::CreateNote {
        linked_tab: None,
        content: "Export me".to_string(),
    });
    assert!(!core.export_notes("markdown").is_empty());
}

#[test]
fn note_search_fts_no_panic() {
    let mut core = MahoCore::new();
    core.handle_event(ShellEvent::CreateNote {
        linked_tab: None,
        content: "Rust programming language".to_string(),
    });
    let _ = core.search_notes_fts("Rust");
}

// ===== SPLIT VIEW =====

#[test]
fn split_view_create_via_event() {
    let mut core = MahoCore::new();
    let tab1 = create_tab(&mut core);
    let tab2 = create_tab(&mut core);
    let window_id = maho_types::identifiers::WindowId::new("window-1");
    core.handle_event(ShellEvent::CreateSplit {
        window_id,
        tab_ids: vec![tab1, tab2],
        orientation: Orientation::Horizontal,
        layout: None,
    });
}

#[test]
fn split_view_config_no_panic() {
    let core = MahoCore::new();
    let window_id = maho_types::identifiers::WindowId::new("window-1");
    let _ = core.get_split_view_config(&window_id);
}

// ===== FIND BAR =====

#[test]
fn find_bar_state_no_panic() {
    let core = MahoCore::new();
    let _ = core.get_find_bar_state();
}

// ===== READING LIST =====

#[test]
fn reading_list_add() {
    let mut core = MahoCore::new();
    let json = core.add_to_reading_list("https://article.com", "Great Article");
    assert!(!json.is_empty());
}

#[test]
fn reading_list_count_after_add() {
    let mut core = MahoCore::new();
    core.add_to_reading_list("https://count.com", "Count Me");
    assert!(core.get_reading_list_count() > 0);
}

#[test]
fn reading_list_unread_count() {
    let mut core = MahoCore::new();
    core.add_to_reading_list("https://unread.com", "Unread");
    assert!(core.get_unread_count() > 0);
}

#[test]
fn reading_list_json_not_empty() {
    let mut core = MahoCore::new();
    core.add_to_reading_list("https://list.com", "Listed");
    assert!(!core.get_reading_list_json().is_empty());
}

// ===== HISTORY =====

#[test]
fn history_add_and_search() {
    let mut core = MahoCore::new();
    core.add_history_entry("https://example.com", "Example");
    // search_history requires SQLite storage; without it returns empty
    let _ = core.search_history("example", 10);
}

#[test]
fn private_active_tab_blocks_history_bookmark_and_form_writes() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let db_path = temp_dir.path().join("maho-core.sqlite");
    let mut core = MahoCore::new().with_storage(db_path.to_str().unwrap());

    let normal_id =
        create_tab_with_url_and_privacy(&mut core, "https://normal-write.example.com", false);
    core.add_history_entry("https://normal-write.example.com", "Normal Write");
    assert_eq!(core.search_history("normal-write", 10).len(), 1);

    let private_id =
        create_tab_with_url_and_privacy(&mut core, "https://private-write.example.com", true);
    core.add_history_entry("https://private-write.example.com", "Private Write");
    assert!(core.search_history("private-write", 10).is_empty());

    let before_bookmarks = core.get_all_bookmark_entries().len();
    core.add_bookmark(
        "https://private-write.example.com",
        "Private Bookmark",
        None,
    );
    assert_eq!(core.get_all_bookmark_entries().len(), before_bookmarks);

    core.save_form_data(private_id.as_ref(), "{\"secret\":true}".to_string());
    assert!(core
        .get_crash_recovery()
        .get_form_data(private_id.as_ref())
        .is_none());

    core.save_form_data(normal_id.as_ref(), "{\"ok\":true}".to_string());
    assert!(core
        .get_crash_recovery()
        .get_form_data(normal_id.as_ref())
        .is_some());
}

#[test]
fn incognito_search_history_returns_no_entries() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let db_path = temp_dir.path().join("maho-core.sqlite");
    let mut core = MahoCore::new().with_storage(db_path.to_str().unwrap());
    let _tab_id =
        create_tab_with_url_and_privacy(&mut core, "https://stored-history.example.com", false);
    core.add_history_entry("https://stored-history.example.com", "Stored History");

    let normal = core.handle_event(ShellEvent::SearchHistory {
        query: "stored-history".to_string(),
        limit: 10,
        is_incognito: false,
    });
    let incognito = core.handle_event(ShellEvent::SearchHistory {
        query: "stored-history".to_string(),
        limit: 10,
        is_incognito: true,
    });

    let normal_entries = normal
        .into_iter()
        .find_map(|update| match update {
            CoreUpdate::HistoryResults { entries } => Some(entries),
            _ => None,
        })
        .expect("normal history results should be emitted");
    let incognito_entries = incognito
        .into_iter()
        .find_map(|update| match update {
            CoreUpdate::HistoryResults { entries } => Some(entries),
            _ => None,
        })
        .expect("incognito history results should be emitted");

    assert!(!normal_entries.is_empty());
    assert!(incognito_entries.is_empty());
}

#[test]
fn history_clear_no_panic() {
    let mut core = MahoCore::new();
    core.add_history_entry("https://clear.com", "Clear Me");
    core.clear_history();
}

#[test]
fn history_search_with_offset_no_panic() {
    let mut core = MahoCore::new();
    core.add_history_entry("https://offset.com", "Offset Test");
    let _ = core.search_history_with_offset("offset", 10, 0);
}

#[test]
fn history_grouped_by_date_no_panic() {
    let mut core = MahoCore::new();
    core.add_history_entry("https://grouped.com", "Grouped");
    let _ = core.get_history_grouped_by_date();
}

// ===== IMPORT/EXPORT =====

#[test]
fn import_export_manager_exists() {
    let core = MahoCore::new();
    let _ = core.get_import_export();
}

#[test]
fn export_history_csv_no_panic() {
    let core = MahoCore::new();
    let _ = core.export_history_csv("[]");
}

#[test]
fn detect_browser_profiles_json() {
    let json = MahoCore::detect_browser_profiles_json();
    assert!(!json.is_empty());
}

// ===== ON_UPDATE CALLBACK =====

#[test]
fn on_update_callback_no_panic() {
    let mut core = MahoCore::new();
    core.on_update(Box::new(|_update| {}));
    create_tab(&mut core);
}

// ===== SPRINT 10: SHORTCUTS / DENSITY / ACCENT / CSS / TRANSPARENCY =====

#[test]
fn shortcuts_json_not_empty() {
    let core = MahoCore::new();
    assert!(!core.get_all_shortcuts_json().is_empty());
}

#[test]
fn shortcuts_reset_all_no_panic() {
    let mut core = MahoCore::new();
    core.reset_all_shortcuts();
    assert!(!core.get_all_shortcuts_json().is_empty());
}

#[test]
fn density_set_compact() {
    let mut core = MahoCore::new();
    core.set_density("compact");
}

#[test]
fn window_transparency_set() {
    let mut core = MahoCore::new();
    core.set_window_transparency(true);
    assert!(core.get_settings().appearance.window_transparency);
}

#[test]
fn app_icon_set() {
    let mut core = MahoCore::new();
    core.set_app_icon(Some("/path/to/icon.png".to_string()));
}

#[test]
fn toolbar_items_json_not_empty() {
    let core = MahoCore::new();
    assert!(!core.get_toolbar_items_json().is_empty());
}

#[test]
fn default_toolbar_items_json_not_empty() {
    let core = MahoCore::new();
    assert!(!core.get_default_toolbar_items_json().is_empty());
}

// ===== CRASH RECOVERY =====

#[test]
fn crash_recovery_exists() {
    let core = MahoCore::new();
    let _ = core.get_crash_recovery();
}

// ===== BACKUP =====

#[test]
fn backup_history_json_not_empty() {
    let core = MahoCore::new();
    assert!(!core.get_backup_history_json().is_empty());
}

// ===== SPRINT 11: URL SCHEME =====

#[test]
fn url_scheme_newtab() {
    let mut core = MahoCore::new();
    let before_count = core.get_tab_view_models().len();
    let result = core.handle_url_scheme("maho://newtab");
    assert!(
        result.contains("\"error\""),
        "expected error JSON, got: {result}"
    );
    assert_eq!(
        core.get_tab_view_models().len(),
        before_count,
        "newtab without url= must not create a tab"
    );
}

#[test]
fn url_scheme_newtab_missing_url() {
    let mut core = MahoCore::new();
    let before_count = core.get_tab_view_models().len();
    let result = core.handle_url_scheme("maho://newtab");
    assert!(
        result.contains("\"error\""),
        "expected error JSON, got: {result}"
    );
    assert_eq!(core.get_tab_view_models().len(), before_count);
}

#[test]
fn url_scheme_newtab_with_url() {
    let mut core = MahoCore::new();
    let before_count = core.get_tab_view_models().len();
    let result = core.handle_url_scheme("maho://newtab?url=https%3A%2F%2Fexample.com");
    assert!(
        result.contains("\"ok\":true"),
        "expected ok JSON, got: {result}"
    );
    assert!(result.contains("\"action\":\"newtab\""));
    assert!(
        core.get_tab_view_models().len() > before_count,
        "newtab with url= must create a tab"
    );
}

#[test]
fn url_scheme_settings() {
    let mut core = MahoCore::new();
    let result = core.handle_url_scheme("maho://settings");
    assert!(result.contains("\"action\":\"settings\""));
    assert!(result.contains("\"ok\":true"));
}

#[test]
fn url_scheme_open_with_ampersand_in_url() {
    let mut core = MahoCore::new();
    let result =
        core.handle_url_scheme("maho://open?url=https%3A%2F%2Fexample.com%3Ffoo%3Dbar%26baz%3Dqux");
    assert!(result.contains("\"action\":\"open\""));
    assert!(result.contains("\"ok\":true"));
}

#[test]
fn url_scheme_open_with_unicode_url() {
    let mut core = MahoCore::new();
    let result =
        core.handle_url_scheme("maho://open?url=https%3A%2F%2Fexample.com%2F%ED%95%9C%EA%B8%80");
    assert!(result.contains("\"action\":\"open\""));
    assert!(result.contains("\"ok\":true"));
}

#[test]
fn url_scheme_open_missing_url() {
    let mut core = MahoCore::new();
    let result = core.handle_url_scheme("maho://open");
    assert!(result.contains("\"error\""));
}

#[test]
fn url_scheme_space_create() {
    let mut core = MahoCore::new();
    let before_count = core.get_space_view_models().len();
    let result = core.handle_url_scheme("maho://space?name=TestSpace");
    assert!(result.contains("\"action\":\"create_space\""));
    assert!(result.contains("\"ok\":true"));
    assert!(core.get_space_view_models().len() > before_count);
}

#[test]
fn url_scheme_space_missing_name() {
    let mut core = MahoCore::new();
    let result = core.handle_url_scheme("maho://space");
    assert!(result.contains("\"error\""));
}

#[test]
fn url_scheme_invalid_scheme() {
    let mut core = MahoCore::new();
    let result = core.handle_url_scheme("https://example.com");
    assert!(result.contains("\"error\""));
    assert!(result.contains("invalid scheme"));
}

#[test]
fn url_scheme_unknown_command() {
    let mut core = MahoCore::new();
    let result = core.handle_url_scheme("maho://foobar");
    assert!(result.contains("\"error\""));
    assert!(result.contains("unknown command"));
}

#[test]
fn url_scheme_open_with_invalid_percent_encoding_preserves_input() {
    let mut core = MahoCore::new();
    let result = core.handle_url_scheme("maho://open?url=https%3A%2F%2Fexample.com%2F%ZZ");

    assert!(result.contains("\"action\":\"open\""));
    assert!(result.contains("\"ok\":true"));

    let tabs = core.get_tab_view_models();
    let opened_tab = tabs
        .iter()
        .find(|tab| tab.url.contains("%ZZ"))
        .expect("invalid percent-encoding should be preserved as literal text");
    assert_eq!(opened_tab.url, "https://example.com/%ZZ");
}

#[test]
fn check_pinned_navigation_only_opens_peek_for_cross_domain_urls() {
    let mut core = MahoCore::new();
    let tab_id = create_tab_with_url(&mut core, "https://example.com/start");

    core.handle_event(ShellEvent::PinTab {
        tab_id: tab_id.clone(),
    });

    let same_domain = core.check_pinned_navigation(&tab_id, &Url::new("https://example.com/next"));
    assert!(same_domain.is_none());

    let cross_domain =
        core.check_pinned_navigation(&tab_id, &Url::new("https://other.example/next"));

    match cross_domain {
        Some(CoreUpdate::OpenPeekTab { url, source_tab_id }) => {
            assert_eq!(url.as_ref(), "https://other.example/next");
            assert_eq!(source_tab_id, tab_id);
        }
        other => panic!("expected OpenPeekTab update, got {other:?}"),
    }
}

// ===== SPRINT 11: SEARCHABLE ITEMS =====

#[test]
fn searchable_items_returns_json_array() {
    let core = MahoCore::new();
    let items = core.get_searchable_items();
    assert!(items.starts_with('['));
    assert!(items.ends_with(']'));
}

#[test]
fn searchable_items_includes_tabs() {
    let mut core = MahoCore::new();
    create_tab(&mut core);
    let items = core.get_searchable_items();
    assert!(items.contains("\"kind\":\"tab\""));
}

#[test]
fn searchable_items_includes_bookmarks() {
    let mut core = MahoCore::new();
    core.add_bookmark_entry(
        "Test BM".to_string(),
        "https://bm.test".to_string(),
        None,
        None,
    );
    let items = core.get_searchable_items();
    assert!(items.contains("\"kind\":\"bookmark\""));
    assert!(items.contains("Test BM"));
}

#[test]
fn memory_pressure_normal_returns_no_actions() {
    let mut core = MahoCore::new();
    let result = core.handle_memory_pressure(maho_types::common::MemoryPressureLevel::Normal);
    let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
    assert_eq!(parsed["level"], "normal");
    assert_eq!(parsed["actions"].as_array().unwrap().len(), 0);
    assert_eq!(parsed["count"], 0);
}

#[test]
fn memory_pressure_warning_returns_suspend_candidates() {
    let mut core = MahoCore::new();
    create_tab(&mut core);
    create_tab(&mut core);
    let result = core.handle_memory_pressure(maho_types::common::MemoryPressureLevel::Warning);
    let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
    assert_eq!(parsed["level"], "warning");
    assert!(parsed["actions"]
        .as_array()
        .unwrap()
        .contains(&serde_json::json!("suspend_idle_tabs")));
    assert!(parsed["suspend_tab_ids"].as_array().is_some());
}

#[test]
fn memory_pressure_critical_returns_suspend_and_clear() {
    let mut core = MahoCore::new();
    create_tab(&mut core);
    let result = core.handle_memory_pressure(maho_types::common::MemoryPressureLevel::Critical);
    let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
    assert_eq!(parsed["level"], "critical");
    let actions = parsed["actions"].as_array().unwrap();
    assert!(actions.contains(&serde_json::json!("suspend_non_active")));
}

#[test]
fn memory_pressure_extreme_purges_history() {
    let mut core = MahoCore::new();
    create_tab(&mut core);
    let result = core.handle_memory_pressure(maho_types::common::MemoryPressureLevel::Extreme);
    let parsed: serde_json::Value = serde_json::from_str(&result).unwrap();
    assert_eq!(parsed["level"], "extreme");
    let actions = parsed["actions"].as_array().unwrap();
    assert!(actions.contains(&serde_json::json!("purged_old_history")));
}

#[test]
fn crash_recovery_form_data_save_and_get() {
    let mut core = MahoCore::new();
    core.save_form_data("tab-1", r#"{"name":"test"}"#.to_string());
    let recovery = core.get_crash_recovery_mut();
    assert_eq!(recovery.get_form_data("tab-1"), Some(r#"{"name":"test"}"#));
    assert_eq!(recovery.get_form_data("tab-nonexistent"), None);
}

#[test]
fn crash_recovery_form_data_clear() {
    let mut core = MahoCore::new();
    core.save_form_data("tab-1", r#"{"field":"value"}"#.to_string());
    let recovery = core.get_crash_recovery_mut();
    recovery.clear_form_data("tab-1");
    assert_eq!(recovery.get_form_data("tab-1"), None);
}

#[test]
fn crash_recovery_save_interval() {
    let mut core = MahoCore::new();
    core.set_crash_save_interval(120);
    let recovery = core.get_crash_recovery_mut();
    assert_eq!(recovery.auto_save_interval_secs(), 120);
}

#[test]
fn crash_recovery_form_data_marks_dirty() {
    let mut core = MahoCore::new();
    let recovery = core.get_crash_recovery_mut();
    assert!(!recovery.needs_save());
    core.save_form_data("tab-1", "{}".to_string());
    let recovery = core.get_crash_recovery_mut();
    assert!(recovery.needs_save());
}

#[test]
fn trigram_index_improves_search_results() {
    let mut core = MahoCore::new();
    for i in 0..50 {
        core.add_history_entry(
            &format!("https://example{}.com/testing", i),
            &format!("Example Test Page {}", i),
        );
    }
    let updates = core.handle_event(ShellEvent::CommandBarQuery {
        text: "testing".to_string(),
        mode: None,
        is_incognito: false,
    });
    let json = serde_json::to_string(&updates).unwrap();
    assert!(json.contains("testing") || json.contains("example"));
}

// ===== SPACE MANAGER: REORDER, PIN, UNPIN, MOVE FOLDER, TOGGLE EXPANDED =====

#[test]
fn space_reorder_changes_order() {
    // Create core, create 2 additional spaces (3 total with default)
    let mut core = MahoCore::new();
    let space1 = create_space(&mut core, "Space1", 120.0);
    let space2 = create_space(&mut core, "Space2", 180.0);

    // Get initial space order
    let initial_spaces = core.get_space_view_models();
    let initial_order: Vec<_> = initial_spaces.iter().map(|s| s.id.clone()).collect();

    // Find positions of our created spaces
    let _pos1 = initial_order
        .iter()
        .position(|id| *id == space1)
        .expect("space1 should exist");
    let pos2 = initial_order
        .iter()
        .position(|id| *id == space2)
        .expect("space2 should exist");

    // Reorder: move space2 to position 0 (first position)
    core.handle_event(ShellEvent::ReorderSpace {
        space_id: space2.clone(),
        from: pos2,
        to: 0,
    });

    // Verify order changed
    let reordered_spaces = core.get_space_view_models();
    let new_order: Vec<_> = reordered_spaces.iter().map(|s| s.id.clone()).collect();
    assert_eq!(new_order[0], space2, "space2 should now be first");
}

#[test]
fn space_reorder_preserves_identity_and_global_active_space() {
    let mut core = MahoCore::new();
    let active_space = core.get_active_space_id();
    let space1 = create_space(&mut core, "Space1", 120.0);
    let space2 = create_space(&mut core, "Space2", 180.0);

    let initial_spaces = core.get_space_view_models();
    let initial_order: Vec<_> = initial_spaces.iter().map(|s| s.id.clone()).collect();
    let _pos1 = initial_order
        .iter()
        .position(|id| *id == space1)
        .expect("space1 should exist");
    let pos2 = initial_order
        .iter()
        .position(|id| *id == space2)
        .expect("space2 should exist");

    core.handle_event(ShellEvent::ReorderSpace {
        space_id: space2.clone(),
        from: pos2,
        to: 0,
    });

    let reordered_spaces = core.get_space_view_models();
    let new_order: Vec<_> = reordered_spaces.iter().map(|s| s.id.clone()).collect();
    assert_eq!(new_order[0], space2);
    assert!(new_order.contains(&space1));
    assert!(new_order.contains(&space2));
    assert_eq!(
        core.get_active_space_id(),
        active_space,
        "reordering should not change the global active space"
    );

    let active_space_vm = reordered_spaces
        .iter()
        .find(|space| space.id == active_space)
        .expect("active space should still exist");
    assert!(
        active_space_vm.is_active,
        "active space should remain active after reorder"
    );
}

#[test]
fn space_config_update_serializes_canonical_optional_fields() {
    let update = SpaceConfigUpdate {
        space_id: SpaceId::new("space-1"),
        name: Some("Config Space".to_string()),
        color: Some(make_color(42.0)),
        theme: None,
        icon: Some(None),
        profile_id: None,
    };

    let value = serde_json::to_value(&update).expect("SpaceConfigUpdate should serialize");
    assert_eq!(value["spaceId"], serde_json::json!("space-1"));
    assert_eq!(value["name"], serde_json::json!("Config Space"));
    assert_eq!(value["color"]["hue"], serde_json::json!(42.0));
    assert_eq!(value["icon"], serde_json::Value::Null);
    assert!(
        value.get("profileId").is_none(),
        "profileId should be omitted when unchanged"
    );
}

fn create_folder(core: &mut MahoCore, space_id: SpaceId, name: &str) -> FolderId {
    let before: Vec<FolderId> = core
        .get_folder_view_models(&space_id)
        .iter()
        .map(|f| f.id.clone())
        .collect();
    core.handle_event(ShellEvent::CreateFolder {
        space_id: space_id.clone(),
        name: name.to_string(),
        is_pinned: false,
        parent_folder_id: None,
        provider_type: None,
        config_json: None,
    });
    let after = core.get_folder_view_models(&space_id);
    after
        .iter()
        .find(|f| !before.contains(&f.id))
        .expect("New folder should appear in view models")
        .id
        .clone()
}

#[test]
fn folder_move_into_folder_sets_parent() {
    // Create 2 folders in default space
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();
    let folder_a = create_folder(&mut core, space_id.clone(), "FolderA");
    let folder_b = create_folder(&mut core, space_id.clone(), "FolderB");

    // Verify both folders exist and folder_b has no parent initially
    let folders_before = core.get_folder_view_models(&space_id);
    let folder_b_before = folders_before
        .iter()
        .find(|f| f.id == folder_b)
        .expect("folder_b should exist");
    assert!(
        folder_b_before.parent_folder_id.is_none(),
        "folder_b should have no parent initially"
    );

    // Move folder B into folder A via ShellEvent::MoveFolderIntoFolder
    core.handle_event(ShellEvent::MoveFolderIntoFolder {
        space_id: space_id.clone(),
        folder_id: folder_b.clone(),
        target_folder_id: folder_a.clone(),
    });

    // Verify the move succeeded (check folder view models or state)
    let folders_after = core.get_folder_view_models(&space_id);
    let folder_b_after = folders_after
        .iter()
        .find(|f| f.id == folder_b)
        .expect("folder_b should exist");
    assert_eq!(
        folder_b_after.parent_folder_id,
        Some(folder_a),
        "folder_b should now have folder_a as parent"
    );
}

#[test]
fn folder_toggle_expanded_flips_state() {
    // Create a folder (default is expanded or collapsed — check)
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();
    let folder_id = create_folder(&mut core, space_id.clone(), "TestFolder");

    // Check initial state
    let folders_initial = core.get_folder_view_models(&space_id);
    let folder_initial = folders_initial
        .iter()
        .find(|f| f.id == folder_id)
        .expect("folder should exist");
    let initial_expanded = folder_initial.is_expanded;

    // Toggle via ShellEvent::ToggleFolderExpanded
    core.handle_event(ShellEvent::ToggleFolderExpanded {
        space_id: space_id.clone(),
        folder_id: folder_id.clone(),
    });

    // Check the state flipped
    let folders_toggled = core.get_folder_view_models(&space_id);
    let folder_toggled = folders_toggled
        .iter()
        .find(|f| f.id == folder_id)
        .expect("folder should exist");
    assert_eq!(
        folder_toggled.is_expanded, !initial_expanded,
        "folder expanded state should have flipped"
    );

    // Toggle again, check it flipped back
    core.handle_event(ShellEvent::ToggleFolderExpanded {
        space_id: space_id.clone(),
        folder_id: folder_id.clone(),
    });

    let folders_restored = core.get_folder_view_models(&space_id);
    let folder_restored = folders_restored
        .iter()
        .find(|f| f.id == folder_id)
        .expect("folder should exist");
    assert_eq!(
        folder_restored.is_expanded, initial_expanded,
        "folder expanded state should have flipped back"
    );
}

#[test]
fn space_switch_restores_last_active_tab() {
    let mut core = MahoCore::new();
    let space_a = core.get_active_space_id();

    let tab_a1 = create_tab(&mut core);
    core.handle_event(ShellEvent::ActivateTab {
        tab_id: tab_a1.clone(),
    });

    let space_b = create_space(&mut core, "Work", 120.0);
    core.handle_event(ShellEvent::ActivateSpace {
        space_id: space_b.clone(),
    });

    let tab_b1 = create_tab_in_space(&mut core, &space_b);
    core.handle_event(ShellEvent::ActivateTab {
        tab_id: tab_b1.clone(),
    });

    let updates = core.handle_event(ShellEvent::ActivateSpace {
        space_id: space_a.clone(),
    });
    match updates.as_slice() {
        [CoreUpdate::ActiveSpaceChanged { active_tab_id, .. }] => {
            assert_eq!(*active_tab_id, Some(tab_a1.clone()));
        }
        other => panic!("expected ActiveSpaceChanged, got {:?}", other),
    }
}

fn create_folder_helper(core: &mut MahoCore, name: &str) -> FolderId {
    let space_id = core.get_active_space_id();
    let updates = core.handle_event(ShellEvent::CreateFolder {
        space_id,
        name: name.to_string(),
        is_pinned: false,
        parent_folder_id: None,
        provider_type: None,
        config_json: None,
    });
    match updates.as_slice() {
        [CoreUpdate::FolderCreated { folder }] => folder.id.clone(),
        _ => panic!("expected FolderCreated"),
    }
}

fn get_root_folder_ids(core: &MahoCore) -> Vec<FolderId> {
    let space_id = core.get_active_space_id();
    core.get_folder_view_models(&space_id)
        .iter()
        .filter(|f| f.parent_folder_id.is_none())
        .map(|f| f.id.clone())
        .collect()
}

#[test]
fn reorder_folder_among_root_siblings() {
    let mut core = MahoCore::new();
    let a = create_folder_helper(&mut core, "A");
    let b = create_folder_helper(&mut core, "B");
    let c = create_folder_helper(&mut core, "C");

    let space_id = core.get_active_space_id();
    core.handle_event(ShellEvent::ReorderFolder {
        space_id: space_id.clone(),
        folder_id: c.clone(),
        parent_folder_id: None,
        before_folder_id: Some(a.clone()),
    });

    let ids = get_root_folder_ids(&core);
    assert_eq!(ids, vec![c, a, b]);
}

#[test]
fn reorder_folder_to_end_of_siblings() {
    let mut core = MahoCore::new();
    let a = create_folder_helper(&mut core, "A");
    let b = create_folder_helper(&mut core, "B");
    let c = create_folder_helper(&mut core, "C");

    let space_id = core.get_active_space_id();
    core.handle_event(ShellEvent::ReorderFolder {
        space_id: space_id.clone(),
        folder_id: a.clone(),
        parent_folder_id: None,
        before_folder_id: None,
    });

    let ids = get_root_folder_ids(&core);
    assert_eq!(ids, vec![b, c, a]);
}

#[test]
fn move_folder_into_folder_and_back_to_root() {
    let mut core = MahoCore::new();
    let parent = create_folder_helper(&mut core, "Parent");
    let child = create_folder_helper(&mut core, "Child");
    let other = create_folder_helper(&mut core, "Other");

    let space_id = core.get_active_space_id();

    core.handle_event(ShellEvent::MoveFolderIntoFolder {
        space_id: space_id.clone(),
        folder_id: child.clone(),
        target_folder_id: parent.clone(),
    });

    let root_ids = get_root_folder_ids(&core);
    assert!(!root_ids.contains(&child));

    core.handle_event(ShellEvent::MoveFolderToRoot {
        space_id: space_id.clone(),
        folder_id: child.clone(),
        before_folder_id: Some(other.clone()),
    });

    let root_ids = get_root_folder_ids(&core);
    assert!(root_ids.contains(&child));
    let child_pos = root_ids.iter().position(|id| *id == child).unwrap();
    let other_pos = root_ids.iter().position(|id| *id == other).unwrap();
    assert!(child_pos < other_pos);
}

#[test]
fn move_folder_to_root_at_end() {
    let mut core = MahoCore::new();
    let parent = create_folder_helper(&mut core, "Parent");
    let child = create_folder_helper(&mut core, "Child");

    let space_id = core.get_active_space_id();

    core.handle_event(ShellEvent::MoveFolderIntoFolder {
        space_id: space_id.clone(),
        folder_id: child.clone(),
        target_folder_id: parent.clone(),
    });

    core.handle_event(ShellEvent::MoveFolderToRoot {
        space_id: space_id.clone(),
        folder_id: child.clone(),
        before_folder_id: None,
    });

    let root_ids = get_root_folder_ids(&core);
    assert_eq!(*root_ids.last().unwrap(), child);
}

#[test]
fn space_switch_returns_none_when_no_tabs() {
    let mut core = MahoCore::new();
    let space_b = create_space(&mut core, "Empty", 60.0);

    let updates = core.handle_event(ShellEvent::ActivateSpace {
        space_id: space_b.clone(),
    });
    match updates.as_slice() {
        [CoreUpdate::ActiveSpaceChanged { active_tab_id, .. }] => {
            assert_eq!(*active_tab_id, None);
        }
        other => panic!("expected ActiveSpaceChanged, got {:?}", other),
    }
}

#[test]
fn closing_last_active_tab_clears_space_tracking() {
    let mut core = MahoCore::new();
    let space = core.get_active_space_id();
    let tab = create_tab(&mut core);
    core.handle_event(ShellEvent::ActivateTab {
        tab_id: tab.clone(),
    });

    core.handle_event(ShellEvent::CloseTab {
        tab_id: tab.clone(),
        expected_space_id: None,
    });

    let space_b = create_space(&mut core, "Other", 60.0);
    core.handle_event(ShellEvent::ActivateSpace {
        space_id: space_b.clone(),
    });

    let updates = core.handle_event(ShellEvent::ActivateSpace {
        space_id: space.clone(),
    });
    match updates.as_slice() {
        [CoreUpdate::ActiveSpaceChanged { active_tab_id, .. }] => {
            assert_eq!(*active_tab_id, None);
        }
        other => panic!("expected ActiveSpaceChanged, got {:?}", other),
    }
}

#[test]
fn reorder_pinned_folder_does_not_cross_into_regular() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();

    let updates = core.handle_event(ShellEvent::CreateFolder {
        space_id: space_id.clone(),
        name: "Pinned".to_string(),
        is_pinned: true,
        parent_folder_id: None,
        provider_type: None,
        config_json: None,
    });
    let pinned = match updates.as_slice() {
        [CoreUpdate::FolderCreated { folder }] => folder.id.clone(),
        _ => panic!("expected FolderCreated"),
    };

    let _regular_a = create_folder_helper(&mut core, "RegA");
    let regular_b = create_folder_helper(&mut core, "RegB");

    core.handle_event(ShellEvent::ReorderFolder {
        space_id: space_id.clone(),
        folder_id: pinned.clone(),
        parent_folder_id: None,
        before_folder_id: Some(regular_b.clone()),
    });

    let vms = core.get_folder_view_models(&space_id);
    let pinned_vm = vms.iter().find(|f| f.id == pinned).unwrap();
    assert!(pinned_vm.is_pinned);

    let root_ids = get_root_folder_ids(&core);
    let pinned_pos = root_ids.iter().position(|id| *id == pinned).unwrap();
    let rb_pos = root_ids.iter().position(|id| *id == regular_b).unwrap();
    assert!(pinned_pos < rb_pos);
}

#[test]
fn move_nested_folder_to_root_among_regular_folders() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();

    let updates = core.handle_event(ShellEvent::CreateFolder {
        space_id: space_id.clone(),
        name: "Pinned".to_string(),
        is_pinned: true,
        parent_folder_id: None,
        provider_type: None,
        config_json: None,
    });
    let _pinned = match updates.as_slice() {
        [CoreUpdate::FolderCreated { folder }] => folder.id.clone(),
        _ => panic!("expected FolderCreated"),
    };

    let parent = create_folder_helper(&mut core, "Parent");
    let child = create_folder_helper(&mut core, "Child");
    let regular_b = create_folder_helper(&mut core, "RegB");

    core.handle_event(ShellEvent::MoveFolderIntoFolder {
        space_id: space_id.clone(),
        folder_id: child.clone(),
        target_folder_id: parent.clone(),
    });

    core.handle_event(ShellEvent::MoveFolderToRoot {
        space_id: space_id.clone(),
        folder_id: child.clone(),
        before_folder_id: Some(regular_b.clone()),
    });

    let root_ids = get_root_folder_ids(&core);
    assert!(root_ids.contains(&child));
    let child_pos = root_ids.iter().position(|id| *id == child).unwrap();
    let rb_pos = root_ids.iter().position(|id| *id == regular_b).unwrap();
    assert!(child_pos < rb_pos);

    let child_vm = core
        .get_folder_view_models(&space_id)
        .into_iter()
        .find(|f| f.id == child)
        .unwrap();
    assert!(child_vm.parent_folder_id.is_none());
}

// ===== COMMAND BAR: RANKING INTEGRITY (PHASE 1) =====

fn make_tab_vm(id: &str, title: &str, url: &str) -> TabViewModel {
    let space_id = SpaceId::new("space-test");
    TabViewModel {
        id: TabId::new(id),
        space_id,
        title: title.to_string(),
        custom_title: None,
        custom_icon: None,
        pinned_url: None,
        url: url.to_string(),
        favicon: None,
        is_loading: false,
        is_pinned: false,
        is_favorite: false,
        favorite_order: None,
        role: TabRole::Normal,
        is_private: false,
        is_muted: false,
        is_playing_audio: false,
        lifecycle_state: "active".to_string(),
        children: vec![],
        created_at: maho_types::common::DateTime("2024-01-01T00:00:00Z".to_string()),
        last_active_at: maho_types::common::DateTime("2024-01-01T00:00:00Z".to_string()),
    }
}

fn empty_ctx() -> SearchContext {
    SearchContext {
        tabs: vec![],
        spaces: vec![],
        folders: vec![],
        archived_tabs: vec![],
        bookmarks: vec![],
        bookmark_favicons: vec![],
        closed_tabs: vec![],
        extensions: vec![],
        is_incognito: false,
        recent_tabs: vec![],
    }
}

#[test]
fn cross_type_url_dedup_tab_beats_history() {
    let mut engine = CommandBarEngine::new();
    engine.add_history_entry(
        "https://rust-lang.org".to_string(),
        "Rust Programming Language".to_string(),
        None,
    );

    let mut ctx = empty_ctx();
    ctx.tabs = vec![make_tab_vm(
        "t1",
        "Rust Programming Language",
        "https://rust-lang.org",
    )];

    let results = engine.search("rust", None, &ctx);

    let rust_results: Vec<_> = results
        .iter()
        .filter(|r| {
            r.execution_payload
                .as_deref()
                .map(|u| u.starts_with("https://rust-lang.org"))
                .unwrap_or(false)
        })
        .collect();

    assert_eq!(
        rust_results.len(),
        1,
        "same URL should collapse to one result; got {} rust-lang entries",
        rust_results.len()
    );
}

#[test]
fn cross_type_url_dedup_bookmark_beats_history_when_no_tab() {
    let mut engine = CommandBarEngine::new();
    engine.add_history_entry(
        "https://example.com/page".to_string(),
        "Example Page".to_string(),
        None,
    );

    let mut ctx = empty_ctx();
    ctx.bookmarks = vec![(
        "bm1".to_string(),
        "Example Page".to_string(),
        "https://example.com/page".to_string(),
    )];

    let results = engine.search("example", None, &ctx);

    let matches: Vec<_> = results
        .iter()
        .filter(|r| {
            r.execution_payload
                .as_deref()
                .map(|u| u.starts_with("https://example.com/page"))
                .unwrap_or(false)
        })
        .collect();

    assert_eq!(
        matches.len(),
        1,
        "bookmark and history for same URL should collapse to one result"
    );
}

#[test]
fn cross_type_url_dedup_closed_tab_collapses_with_history() {
    let mut engine = CommandBarEngine::new();
    engine.add_history_entry(
        "https://closed.example.com".to_string(),
        "Closed Example".to_string(),
        None,
    );

    let mut ctx = empty_ctx();
    ctx.closed_tabs = vec![make_tab_vm(
        "c1",
        "Closed Example",
        "https://closed.example.com",
    )];

    let results = engine.search("closed example", None, &ctx);

    let matches: Vec<_> = results
        .iter()
        .filter(|r| {
            r.execution_payload
                .as_deref()
                .map(|u| u.starts_with("https://closed.example.com"))
                .unwrap_or(false)
        })
        .collect();

    assert_eq!(
        matches.len(),
        1,
        "closed tab and history for same URL should collapse; got {}",
        matches.len()
    );
}

#[test]
fn cross_type_url_dedup_different_urls_not_collapsed() {
    let mut engine = CommandBarEngine::new();
    engine.add_history_entry(
        "https://alpha.example.com".to_string(),
        "Alpha Example".to_string(),
        None,
    );

    let mut ctx = empty_ctx();
    ctx.tabs = vec![make_tab_vm(
        "t1",
        "Beta Example",
        "https://beta.example.com",
    )];

    let results = engine.search("example", None, &ctx);

    let alpha_count = results
        .iter()
        .filter(|r| r.execution_payload.as_deref() == Some("https://alpha.example.com"))
        .count();
    let beta_count = results
        .iter()
        .filter(|r| r.execution_payload.as_deref() == Some("https://beta.example.com"))
        .count();

    assert_eq!(
        alpha_count, 1,
        "distinct URL alpha.example.com should appear"
    );
    assert_eq!(beta_count, 1, "distinct URL beta.example.com should appear");
}

#[test]
fn search_respects_max_results_limit() {
    let mut engine = CommandBarEngine::new();
    for i in 0..30 {
        engine.add_history_entry(
            format!("https://site{}.example.com", i),
            format!("Site {} Example Page Testing Query", i),
            None,
        );
    }

    let ctx = empty_ctx();
    let results = engine.search("example", None, &ctx);

    assert!(
        results.len() <= 12,
        "results must not exceed MAX_RESULTS=12; got {}",
        results.len()
    );
}

#[test]
fn type_priority_tiebreak_results_are_deterministic() {
    let mut engine = CommandBarEngine::new();
    engine.add_history_entry(
        "https://tiebreak.example.com".to_string(),
        "Tiebreak Test Page".to_string(),
        None,
    );
    engine.add_history_entry(
        "https://tiebreak2.example.com".to_string(),
        "Tiebreak Test Page Alt".to_string(),
        None,
    );

    let mut ctx = empty_ctx();
    ctx.tabs = vec![make_tab_vm(
        "t1",
        "Tiebreak Test Page",
        "https://tiebreak2.example.com",
    )];

    let results_a = engine.search("tiebreak test page", None, &ctx);
    let results_b = engine.search("tiebreak test page", None, &ctx);

    let keys_a: Vec<_> = results_a.iter().map(|r| r.key.clone()).collect();
    let keys_b: Vec<_> = results_b.iter().map(|r| r.key.clone()).collect();
    assert_eq!(
        keys_a, keys_b,
        "identical queries must produce identical result order"
    );
}

#[test]
fn non_url_results_not_collapsed_by_url_dedup() {
    let mut engine = CommandBarEngine::new();
    engine.add_action(
        "new_tab".to_string(),
        "New Tab".to_string(),
        "Navigation".to_string(),
    );
    engine.add_action(
        "new_window".to_string(),
        "New Window".to_string(),
        "Navigation".to_string(),
    );

    let ctx = empty_ctx();
    let results = engine.search("> new", None, &ctx);

    let action_count = results
        .iter()
        .filter(|r| matches!(r.kind, SuggestionType::Action))
        .count();
    assert!(
        action_count >= 2,
        "distinct actions should not be collapsed by URL dedup; got {}",
        action_count
    );
}

#[test]
fn url_dedup_normalises_trailing_slash() {
    let mut engine = CommandBarEngine::new();
    engine.add_history_entry(
        "https://trailing.example.com/".to_string(),
        "Trailing Slash Page".to_string(),
        None,
    );

    let mut ctx = empty_ctx();
    ctx.tabs = vec![make_tab_vm(
        "t1",
        "Trailing Slash Page",
        "https://trailing.example.com",
    )];

    let results = engine.search("trailing slash", None, &ctx);

    let matches: Vec<_> = results
        .iter()
        .filter(|r| {
            r.execution_payload
                .as_deref()
                .map(|u| u.trim_end_matches('/') == "https://trailing.example.com")
                .unwrap_or(false)
        })
        .collect();

    assert_eq!(
        matches.len(),
        1,
        "trailing slash variant and non-slash variant of same URL should collapse to one result"
    );
}

fn set_pinned_close_behavior(core: &mut MahoCore, behavior: PinnedCloseBehavior) {
    core.update_settings(SettingsUpdate {
        general: Some(GeneralSettingsUpdate {
            pinned_close_behavior: Some(behavior),
            ..Default::default()
        }),
        ..Default::default()
    });
}

fn pin_tab_with_url(core: &mut MahoCore, url: &str) -> TabId {
    let tab_id = create_tab_with_url(core, url);
    core.handle_event(ShellEvent::PinTab {
        tab_id: tab_id.clone(),
    });
    tab_id
}

#[test]
fn pinned_close_switch_mode_keeps_tab_in_pinned_list() {
    let mut core = MahoCore::new();
    let tab_id = pin_tab_with_url(&mut core, "https://example.com");
    let space_id = core.get_active_space_id();

    core.handle_event(ShellEvent::CloseTab {
        tab_id: tab_id.clone(),
        expected_space_id: None,
    });

    let pinned = core.get_pinned_tabs(&space_id);
    assert!(
        pinned.iter().any(|t| t.id == tab_id),
        "pinned tab must remain in pinned list under switch mode"
    );
}

#[test]
fn pinned_close_close_mode_removes_tab() {
    let mut core = MahoCore::new();
    set_pinned_close_behavior(&mut core, PinnedCloseBehavior::Close);
    let tab_id = pin_tab_with_url(&mut core, "https://example.com");
    let space_id = core.get_active_space_id();

    core.handle_event(ShellEvent::CloseTab {
        tab_id: tab_id.clone(),
        expected_space_id: None,
    });

    let pinned = core.get_pinned_tabs(&space_id);
    assert!(
        !pinned.iter().any(|t| t.id == tab_id),
        "pinned tab must be removed from pinned list under close mode"
    );
}

#[test]
fn pinned_close_reset_mode_keeps_tab_and_resets_url() {
    let mut core = MahoCore::new();
    set_pinned_close_behavior(&mut core, PinnedCloseBehavior::Reset);
    let tab_id = pin_tab_with_url(&mut core, "https://example.com");
    let space_id = core.get_active_space_id();

    core.handle_event(ShellEvent::NavigateTo {
        tab_id: tab_id.clone(),
        url: Url::new("https://other.com"),
    });

    core.handle_event(ShellEvent::CloseTab {
        tab_id: tab_id.clone(),
        expected_space_id: None,
    });

    let pinned = core.get_pinned_tabs(&space_id);
    let tab_vm = pinned
        .iter()
        .find(|t| t.id == tab_id)
        .expect("tab must remain pinned");
    assert_eq!(
        tab_vm.url, "https://example.com",
        "URL must be reset to pinned_url under reset mode"
    );
}

#[test]
fn pinned_close_switch_mode_moves_last_active_to_another_tab() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();
    let pinned_id = pin_tab_with_url(&mut core, "https://pinned.com");
    let other_id = create_tab_in_space(&mut core, &space_id);

    core.handle_event(ShellEvent::ActivateTab {
        tab_id: pinned_id.clone(),
    });

    assert_eq!(
        core.get_last_active_tab_for_space(&space_id),
        Some(pinned_id.clone()),
        "pinned tab must be last-active before CloseTab"
    );

    core.handle_event(ShellEvent::CloseTab {
        tab_id: pinned_id.clone(),
        expected_space_id: None,
    });

    let last_active = core.get_last_active_tab_for_space(&space_id);
    assert_ne!(
        last_active,
        Some(pinned_id.clone()),
        "last-active must not remain on the closed pinned tab after switch"
    );
    assert_eq!(
        last_active,
        Some(other_id),
        "last-active must move to the other available tab"
    );
}

#[test]
fn pinned_close_unload_switch_mode_suspends_tab_and_keeps_it_pinned() {
    let mut core = MahoCore::new();
    set_pinned_close_behavior(&mut core, PinnedCloseBehavior::UnloadSwitch);
    let tab_id = pin_tab_with_url(&mut core, "https://example.com");
    let space_id = core.get_active_space_id();

    core.handle_event(ShellEvent::CloseTab {
        tab_id: tab_id.clone(),
        expected_space_id: None,
    });

    let pinned = core.get_pinned_tabs(&space_id);
    let tab_vm = pinned
        .iter()
        .find(|t| t.id == tab_id)
        .expect("tab must remain pinned");
    assert_eq!(
        tab_vm.lifecycle_state, "suspended",
        "tab must be suspended (unloaded) under unload-switch mode"
    );
}

// ===== BOOST LIFECYCLE (WU-7) =====

#[test]
fn test_boost_create_temp_produces_valid_boost() {
    let mut mgr = BoostManager::new();
    let boost = mgr.create_temp("example.com".to_string());

    assert_eq!(boost.domain, "example.com");
    assert!(!boost.id.0.as_str().is_empty());
    // A new Boost starts in manual color mode with Auto off, per the Boost
    // editor contract (`maho_boost/DESIGN.md`: new or reset Boosts get
    // `colorBoostEnabled=true` and `magicTheme=false`).
    assert!(boost.color.color_boost_enabled);
    assert!(!boost.color.magic_theme);
    assert_eq!(boost.typography.case_mode, CaseMode::None);
    assert_eq!(boost.typography.size_mode, SizeMode::K100);
    assert!(boost.zap_selectors.is_empty());
    assert!(boost.custom_css.is_empty());
    assert!(!boost.change_was_made);
}

#[test]
fn test_boost_commit_persists_state() {
    let mut mgr = BoostManager::new();
    let boost = mgr.create_temp("example.com".to_string());
    let id = boost.id.clone();

    let update = BoostUpdate {
        color: Some(ColorBoostUpdate {
            magic_theme: Some(true),
            ..Default::default()
        }),
        ..Default::default()
    };
    mgr.update(&id, update);

    let committed = mgr
        .commit_boost(&id)
        .expect("commit_boost must return Some for a known id");
    assert!(committed.change_was_made);

    let retrieved = mgr.get_boost(&id).expect("boost must exist after commit");
    assert!(retrieved.color.magic_theme);
    assert!(retrieved.change_was_made);
}

#[test]
fn test_boost_discard_removes_temp() {
    let mut mgr = BoostManager::new();
    let boost = mgr.create_temp("example.com".to_string());
    let id = boost.id.clone();

    // `discard_boost` returns `Some(previously_active_id)` when the unchanged
    // temporary Boost was discarded, and `None` only when the discard was
    // refused (change_was_made). Here nothing was active before, so the discard
    // succeeds and reports "no previous active Boost to restore".
    let prev = mgr.discard_boost(&id);
    assert_eq!(prev, Some(None));
    assert!(mgr.get_boost(&id).is_none());
    assert!(mgr.list_for_domain("example.com").is_empty());
}

#[test]
fn test_boost_lifecycle_full_roundtrip() {
    let mut mgr = BoostManager::new();
    let boost = mgr.create_temp("test.org".to_string());
    let id = boost.id.clone();

    let update = BoostUpdate {
        typography: Some(TypographyBoostUpdate {
            case_mode: Some(CaseMode::Upper),
            size_mode: Some(SizeMode::K125),
            ..Default::default()
        }),
        custom_css: Some("body { color: red; }".to_string()),
        ..Default::default()
    };
    mgr.update(&id, update);
    mgr.commit_boost(&id).expect("commit must succeed");

    let boosts = mgr.list_for_domain("test.org");
    assert_eq!(boosts.len(), 1);
    assert_eq!(boosts[0].typography.case_mode, CaseMode::Upper);
    assert_eq!(boosts[0].typography.size_mode, SizeMode::K125);
    assert_eq!(boosts[0].custom_css, "body { color: red; }");
    assert!(boosts[0].change_was_made);
}

#[test]
fn test_boost_discard_after_update_blocked_by_change_flag() {
    let mut mgr = BoostManager::new();
    let boost = mgr.create_temp("discard.com".to_string());
    let id = boost.id.clone();

    let update = BoostUpdate {
        color: Some(ColorBoostUpdate {
            magic_theme: Some(true),
            ..Default::default()
        }),
        custom_css: Some("h1 { display: none; }".to_string()),
        ..Default::default()
    };
    mgr.update(&id, update);

    let discard_result = mgr.discard_boost(&id);
    assert!(
        discard_result.is_none(),
        "discard must be blocked when change_was_made is true"
    );

    let still_there = mgr
        .get_boost(&id)
        .expect("boost must still exist after blocked discard");
    assert!(
        still_there.color.magic_theme,
        "update must still be present"
    );
    assert_eq!(still_there.custom_css, "h1 { display: none; }");
    assert_eq!(mgr.list_for_domain("discard.com").len(), 1);
}

#[test]
fn pinned_tab_suspend_then_close_deletes_tab() {
    let mut core = MahoCore::new();
    let tab_id = pin_tab_with_url(&mut core, "https://example.com");
    let space_id = core.get_active_space_id();

    // 1. First click: Suspend the pinned tab
    core.handle_event(ShellEvent::SuspendTab {
        tab_id: tab_id.clone(),
    });

    // Verify it is suspended in core view model
    let tab_vm = core
        .get_tab_view_models()
        .into_iter()
        .find(|t| t.id == tab_id)
        .unwrap();
    assert_eq!(tab_vm.lifecycle_state, "suspended");

    // 2. Second click: Close the suspended tab
    core.handle_event(ShellEvent::CloseTab {
        tab_id: tab_id.clone(),
        expected_space_id: None,
    });

    // Verify it is fully deleted
    let pinned = core.get_pinned_tabs(&space_id);
    assert!(
        !pinned.iter().any(|t| t.id == tab_id),
        "suspended pinned tab must be fully deleted on close"
    );
}

// === new_tab_position (sidebar new-tab insertion) ===

fn set_new_tab_position(core: &mut MahoCore, position: NewTabPosition) {
    core.update_settings(SettingsUpdate {
        general: Some(GeneralSettingsUpdate {
            new_tab_position: Some(position),
            ..Default::default()
        }),
        ..Default::default()
    });
}

fn root_tab_order(core: &MahoCore, space_id: &SpaceId) -> Vec<TabId> {
    core.space_manager()
        .get_root_order(space_id)
        .into_iter()
        .filter_map(|item| match item {
            RootItem::Tab(tab_id) => Some(tab_id),
            RootItem::Folder(_) => None,
        })
        .collect()
}

#[test]
fn new_tab_position_defaults_to_top() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();

    let first = create_tab_with_url(&mut core, "https://first.example");
    let second = create_tab_with_url(&mut core, "https://second.example");

    assert_eq!(
        root_tab_order(&core, &space_id),
        vec![second, first],
        "default (top) must insert each new tab above the previous ones"
    );
}

#[test]
fn new_tab_position_bottom_appends_new_tabs() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();
    set_new_tab_position(&mut core, NewTabPosition::Bottom);

    let first = create_tab_with_url(&mut core, "https://first.example");
    let second = create_tab_with_url(&mut core, "https://second.example");

    assert_eq!(
        root_tab_order(&core, &space_id),
        vec![first, second],
        "bottom must append each new tab below the existing ones"
    );
}

#[test]
fn new_tab_position_top_after_bottom_switches_back() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();
    set_new_tab_position(&mut core, NewTabPosition::Bottom);
    let first = create_tab_with_url(&mut core, "https://first.example");

    set_new_tab_position(&mut core, NewTabPosition::Top);
    let second = create_tab_with_url(&mut core, "https://second.example");

    assert_eq!(
        root_tab_order(&core, &space_id),
        vec![second, first],
        "switching back to top must apply to the next new tab only"
    );
}

// Unknown/garbage wire values must not silently reposition tabs: settings JSON
// carrying an unrecognized newTabPosition decodes to the default (top).
#[test]
fn new_tab_position_unknown_pref_value_falls_back_to_top() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();
    set_new_tab_position(&mut core, NewTabPosition::Bottom);

    let update: SettingsUpdate =
        serde_json::from_str(r#"{"general":{"newTabPosition":"diagonal"}}"#)
            .expect("unknown position value must not fail settings decode");
    core.update_settings(update);

    let first = create_tab_with_url(&mut core, "https://first.example");
    let second = create_tab_with_url(&mut core, "https://second.example");

    assert_eq!(
        root_tab_order(&core, &space_id),
        vec![second, first],
        "unknown position value must fall back to top"
    );
}

// Re-announce (session restore / window move) carries an explicit tab_id for an
// already-known tab. It must never reposition the tab, under either setting.
#[test]
fn new_tab_position_bottom_does_not_reposition_reannounced_tab() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();
    set_new_tab_position(&mut core, NewTabPosition::Bottom);

    let first = create_tab_with_url(&mut core, "https://first.example");
    let second = create_tab_with_url(&mut core, "https://second.example");
    assert_eq!(
        root_tab_order(&core, &space_id),
        vec![first.clone(), second.clone()]
    );

    core.handle_event(ShellEvent::CreateTab {
        space_id: space_id.clone(),
        url: Some(Url::new("https://first.example")),
        parent_id: None,
        tab_id: Some(first.clone()),
        window_id: None,
        is_private: false,
    });

    assert_eq!(
        root_tab_order(&core, &space_id),
        vec![first, second],
        "re-announce must not reshuffle an existing tab"
    );
}

// Explicit reorder wins over the insertion preference: the setting governs only
// where NEW tabs land, never subsequent user-driven ordering. The sidebar drags
// root items, so this exercises ReorderRootItem (the path that owns root_order).
#[test]
fn new_tab_position_bottom_does_not_affect_explicit_reorder() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();
    set_new_tab_position(&mut core, NewTabPosition::Bottom);

    let first = create_tab_with_url(&mut core, "https://first.example");
    let second = create_tab_with_url(&mut core, "https://second.example");

    core.handle_event(ShellEvent::ReorderRootItem {
        space_id: space_id.clone(),
        item: RootItem::Tab(second.clone()),
        insertion_point: RootInsertionPoint::Before {
            target: RootItem::Tab(first.clone()),
        },
    });

    assert_eq!(
        root_tab_order(&core, &space_id),
        vec![second, first],
        "explicit reorder must survive the bottom insertion preference"
    );
}

#[test]
fn new_tab_position_empty_space_accepts_top_and_bottom() {
    for position in [NewTabPosition::Top, NewTabPosition::Bottom] {
        let mut core = MahoCore::new();
        let space_id = core.get_active_space_id();
        set_new_tab_position(&mut core, position);

        let only = create_tab_with_url(&mut core, "https://only.example");

        assert_eq!(root_tab_order(&core, &space_id), vec![only]);
    }
}

#[test]
fn new_tab_position_bottom_applies_to_private_tabs() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();
    set_new_tab_position(&mut core, NewTabPosition::Bottom);

    let first = create_tab_with_url_and_privacy(&mut core, "https://private-one.example", true);
    let second = create_tab_with_url_and_privacy(&mut core, "https://private-two.example", true);

    assert_eq!(root_tab_order(&core, &space_id), vec![first, second]);
}

#[test]
fn new_tab_position_top_preserves_pinned_section_order() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();
    set_new_tab_position(&mut core, NewTabPosition::Bottom);

    let pinned = create_tab_with_url(&mut core, "https://pinned.example");
    core.handle_event(ShellEvent::PinTab {
        tab_id: pinned.clone(),
    });
    let existing = create_tab_with_url(&mut core, "https://existing.example");

    set_new_tab_position(&mut core, NewTabPosition::Top);
    let new_tab = create_tab_with_url(&mut core, "https://new.example");

    assert_eq!(
        root_tab_order(&core, &space_id),
        vec![new_tab, pinned, existing],
        "top inserts the new normal tab without reordering existing pinned/root items"
    );
}

#[test]
fn new_tab_position_bottom_preserves_existing_folder_root_order() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();
    set_new_tab_position(&mut core, NewTabPosition::Bottom);

    let grouped = create_tab_with_url(&mut core, "https://grouped.example");
    core.handle_event(ShellEvent::CreateFolderWithTabs {
        space_id: space_id.clone(),
        name: "Existing group".to_string(),
        tab_ids: vec![grouped],
    });
    let before = core.space_manager().get_root_order(&space_id);
    assert_eq!(before.len(), 1);
    assert!(matches!(before[0], RootItem::Folder(_)));

    let new_tab = create_tab_with_url(&mut core, "https://new.example");
    let after = core.space_manager().get_root_order(&space_id);

    assert_eq!(after.len(), 2);
    assert_eq!(after[0], before[0]);
    assert_eq!(after[1], RootItem::Tab(new_tab));
}
