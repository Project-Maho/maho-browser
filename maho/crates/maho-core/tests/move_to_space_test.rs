use maho_core::maho_core::MahoCore;
use maho_types::events::core_update::CoreUpdate;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::{FolderId, ProfileId, SpaceId, TabId};
use maho_types::space::{RootItem, SpaceColor};
use maho_types::tab::TabRole;

fn make_color(hue: f64) -> SpaceColor {
    SpaceColor {
        hue,
        saturation: 0.8,
        brightness: 0.9,
        grain: 0.0,
    }
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
    core.get_tab_view_models()
        .iter()
        .find(|t| !before.contains(&t.id))
        .expect("new tab")
        .id
        .clone()
}

fn create_space_in_profile(
    core: &mut MahoCore,
    name: &str,
    hue: f64,
    profile_id: ProfileId,
) -> SpaceId {
    let before: Vec<SpaceId> = core
        .get_space_view_models()
        .iter()
        .map(|s| s.id.clone())
        .collect();
    core.handle_event(ShellEvent::CreateSpace {
        name: name.to_string(),
        color: make_color(hue),
        profile_id,
    });
    core.get_space_view_models()
        .iter()
        .find(|s| !before.contains(&s.id))
        .expect("new space")
        .id
        .clone()
}

fn profile_of(core: &MahoCore, space_id: &SpaceId) -> ProfileId {
    core.space_manager()
        .get_space(space_id)
        .expect("space exists")
        .profile_id
        .clone()
}

fn role_of(core: &MahoCore, tab_id: &TabId) -> TabRole {
    core.tab_manager()
        .get_tab(tab_id)
        .expect("tab exists")
        .role
        .clone()
}

fn space_id_of(core: &MahoCore, tab_id: &TabId) -> SpaceId {
    core.tab_manager()
        .get_tab(tab_id)
        .expect("tab exists")
        .space_id
        .clone()
}

fn create_folder_with_tabs(
    core: &mut MahoCore,
    space_id: &SpaceId,
    name: &str,
    tab_ids: Vec<TabId>,
) -> FolderId {
    let updates = core.handle_event(ShellEvent::CreateFolderWithTabs {
        space_id: space_id.clone(),
        name: name.to_string(),
        tab_ids,
    });
    updates
        .iter()
        .find_map(|u| match u {
            CoreUpdate::FolderCreated { folder } => Some(folder.id.clone()),
            _ => None,
        })
        .expect("FolderCreated")
}

fn folder_ids(core: &MahoCore, space_id: &SpaceId) -> Vec<FolderId> {
    core.space_manager()
        .get_space(space_id)
        .map(|s| s.folders.iter().map(|f| f.id.clone()).collect())
        .unwrap_or_default()
}

fn root_has_folder(core: &MahoCore, space_id: &SpaceId, folder_id: &FolderId) -> bool {
    core.space_manager()
        .get_root_order(space_id)
        .iter()
        .any(|item| matches!(item, RootItem::Folder(fid) if fid == folder_id))
}

#[test]
fn move_folder_to_space_relocates_folder_and_tabs() {
    let mut core = MahoCore::new();
    let space_a = core.get_active_space_id().clone();
    let profile = profile_of(&core, &space_a);
    let space_b = create_space_in_profile(&mut core, "B", 30.0, profile);

    let t1 = create_tab_in_space(&mut core, &space_a);
    let t2 = create_tab_in_space(&mut core, &space_a);
    let folder = create_folder_with_tabs(&mut core, &space_a, "F", vec![t1.clone(), t2.clone()]);

    core.handle_event(ShellEvent::MoveFolderToSpace {
        source_space_id: space_a.clone(),
        folder_id: folder.clone(),
        target_space_id: space_b.clone(),
    });

    // Folder record relocated to target, gone from source.
    assert!(folder_ids(&core, &space_b).contains(&folder));
    assert!(!folder_ids(&core, &space_a).contains(&folder));
    // Top-level folder appears in the target's root order.
    assert!(root_has_folder(&core, &space_b, &folder));
    // Both contained tabs now belong to the target space.
    assert_eq!(space_id_of(&core, &t1), space_b);
    assert_eq!(space_id_of(&core, &t2), space_b);
}

#[test]
fn move_folder_to_space_preserves_nested_structure() {
    let mut core = MahoCore::new();
    let space_a = core.get_active_space_id().clone();
    let profile = profile_of(&core, &space_a);
    let space_b = create_space_in_profile(&mut core, "B", 30.0, profile);

    let t1 = create_tab_in_space(&mut core, &space_a);
    let t2 = create_tab_in_space(&mut core, &space_a);
    let parent = create_folder_with_tabs(&mut core, &space_a, "Parent", vec![t1.clone()]);

    let child_updates = core.handle_event(ShellEvent::CreateFolder {
        space_id: space_a.clone(),
        name: "Child".to_string(),
        is_pinned: false,
        parent_folder_id: Some(parent.clone()),
        provider_type: None,
        config_json: None,
    });
    let child = child_updates
        .iter()
        .find_map(|u| match u {
            CoreUpdate::FolderCreated { folder } => Some(folder.id.clone()),
            _ => None,
        })
        .expect("child FolderCreated");
    core.handle_event(ShellEvent::MoveTabToFolder {
        space_id: space_a.clone(),
        folder_id: child.clone(),
        tab_id: t2.clone(),
    });

    core.handle_event(ShellEvent::MoveFolderToSpace {
        source_space_id: space_a.clone(),
        folder_id: parent.clone(),
        target_space_id: space_b.clone(),
    });

    // Both folders relocated, source emptied.
    let target_folders = folder_ids(&core, &space_b);
    assert!(target_folders.contains(&parent));
    assert!(target_folders.contains(&child));
    assert!(folder_ids(&core, &space_a).is_empty());

    // Nesting preserved: child still points at parent; only the top folder is
    // promoted to the target root order.
    let child_parent = core
        .space_manager()
        .get_space(&space_b)
        .and_then(|s| s.folders.iter().find(|f| f.id == child).cloned())
        .and_then(|f| f.parent_folder_id);
    assert_eq!(child_parent, Some(parent.clone()));
    assert!(root_has_folder(&core, &space_b, &parent));
    assert!(!root_has_folder(&core, &space_b, &child));

    // Tabs at every depth migrated.
    assert_eq!(space_id_of(&core, &t1), space_b);
    assert_eq!(space_id_of(&core, &t2), space_b);
}

#[test]
fn move_favorite_cross_profile_downgrades_when_target_at_limit() {
    let limit = maho_core::tab_lifecycle::MAX_FAVORITES;
    let mut core = MahoCore::new();
    let space_a = core.get_active_space_id().clone();

    let other_profile = core
        .create_profile_persisted("Other".to_string())
        .unwrap()
        .id;
    let space_b = create_space_in_profile(&mut core, "B", 30.0, other_profile);

    // Fill the target profile to its favorite cap.
    for _ in 0..limit {
        let t = create_tab_in_space(&mut core, &space_b);
        core.handle_event(ShellEvent::FavoriteTab { tab_id: t });
    }

    // A favorite in the source profile moved into the full target profile must
    // be downgraded to Normal rather than overflowing the cap.
    let fav = create_tab_in_space(&mut core, &space_a);
    core.handle_event(ShellEvent::FavoriteTab {
        tab_id: fav.clone(),
    });
    assert!(matches!(role_of(&core, &fav), TabRole::Favorite { .. }));

    core.handle_event(ShellEvent::MoveTabToSpace {
        tab_id: fav.clone(),
        target_space_id: space_b.clone(),
        section: String::new(),
    });

    assert_eq!(role_of(&core, &fav), TabRole::Normal);
    assert_eq!(space_id_of(&core, &fav), space_b);
}

#[test]
fn move_favorite_cross_profile_keeps_role_when_room() {
    let mut core = MahoCore::new();
    let space_a = core.get_active_space_id().clone();

    let other_profile = core
        .create_profile_persisted("Other".to_string())
        .unwrap()
        .id;
    let space_b = create_space_in_profile(&mut core, "B", 30.0, other_profile);

    let fav = create_tab_in_space(&mut core, &space_a);
    core.handle_event(ShellEvent::FavoriteTab {
        tab_id: fav.clone(),
    });

    core.handle_event(ShellEvent::MoveTabToSpace {
        tab_id: fav.clone(),
        target_space_id: space_b.clone(),
        section: String::new(),
    });

    assert!(matches!(role_of(&core, &fav), TabRole::Favorite { .. }));
    assert_eq!(space_id_of(&core, &fav), space_b);
}

#[test]
fn move_favorite_same_profile_keeps_role() {
    let mut core = MahoCore::new();
    let space_a = core.get_active_space_id().clone();
    let profile = profile_of(&core, &space_a);
    let space_b = create_space_in_profile(&mut core, "B", 30.0, profile);

    let fav = create_tab_in_space(&mut core, &space_a);
    core.handle_event(ShellEvent::FavoriteTab {
        tab_id: fav.clone(),
    });

    core.handle_event(ShellEvent::MoveTabToSpace {
        tab_id: fav.clone(),
        target_space_id: space_b.clone(),
        section: String::new(),
    });

    assert!(matches!(role_of(&core, &fav), TabRole::Favorite { .. }));
    assert_eq!(space_id_of(&core, &fav), space_b);
}
