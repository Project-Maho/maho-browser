use std::collections::{HashMap, HashSet};

use maho_types::common::DateTime;
use maho_types::folder::Folder;
use maho_types::identifiers::{FolderId, ProfileId, SpaceId, TabId};
use maho_types::space::{
    ColorHarmony, Space, SpaceColor, SpaceConfigUpdate, SpaceTheme, ThemeColor,
};
use maho_types::space::{RootInsertionPoint, RootItem};
use maho_types::traits::shell_renderer::{
    FolderViewModel, SidebarNode, SpaceViewModel, TabViewModel,
};

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum SpaceUpdateError {
    EmptyName,
    DuplicateName,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FolderError {
    CycleDetected,
    DepthLimitExceeded,
    FolderNotFound,
    SpaceNotFound,
}

pub const MAX_FOLDER_DEPTH: usize = 5;

#[cfg(test)]
#[allow(clippy::items_after_test_module)]
mod tests {
    use super::*;
    use maho_types::folder::Folder;
    use maho_types::space::{
        ColorHarmony, RootInsertionPoint, RootItem, SpaceConfigUpdate, SpaceTheme, ThemeColor,
    };

    #[test]
    fn normalize_space_icon_maps_names_and_keeps_glyphs() {
        assert_eq!(normalize_space_icon("bulb").as_deref(), Some("💡"));
        assert_eq!(normalize_space_icon(" Barbell ").as_deref(), Some("🏋️"));
        assert_eq!(normalize_space_icon("star.fill").as_deref(), Some("⭐"));
        assert_eq!(normalize_space_icon("light-bulb").as_deref(), Some("💡"));
        assert_eq!(normalize_space_icon("🦀").as_deref(), Some("🦀"));
        assert_eq!(normalize_space_icon("🇫🇷").as_deref(), Some("🇫🇷"));
        assert_eq!(normalize_space_icon("A").as_deref(), Some("A"));
        assert_eq!(normalize_space_icon("definitely-not-an-icon"), None);
        assert_eq!(normalize_space_icon("   "), None);
    }

    #[test]
    fn update_space_config_stores_icon_names_as_glyphs() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let applied = manager
            .update_space_config(SpaceConfigUpdate {
                space_id: space_id.clone(),
                icon: Some(Some("bulb".to_string())),
                ..Default::default()
            })
            .unwrap()
            .unwrap();
        assert_eq!(applied.icon, Some(Some("💡".to_string())));
        assert_eq!(
            manager.get_space(&space_id).unwrap().icon.as_deref(),
            Some("💡")
        );

        manager
            .update_space_config(SpaceConfigUpdate {
                space_id: space_id.clone(),
                icon: Some(Some("no-such-icon".to_string())),
                ..Default::default()
            })
            .unwrap();
        assert_eq!(manager.get_space(&space_id).unwrap().icon, None);
    }

    #[test]
    fn restore_and_upsert_migrate_persisted_icon_names() {
        let mut manager = SpaceManager::new();
        let active = manager.get_active_space_id().clone();
        let mut space = manager.get_space(&active).unwrap().clone();
        space.icon = Some("bulb".to_string());
        let id = space.id.clone();
        manager.restore_spaces(vec![space.clone()], id.clone());
        assert_eq!(manager.get_space(&id).unwrap().icon.as_deref(), Some("💡"));

        space.icon = Some("layers".to_string());
        manager.upsert_space(space);
        assert_eq!(manager.get_space(&id).unwrap().icon.as_deref(), Some("🗂️"));
    }

    #[test]
    fn add_tab_to_space_ignores_duplicate_tab_ids() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        manager.add_tab_to_space(&space_id, tab_id.clone(), Some(0));

        let tab_order = manager.get_tab_order(&space_id);
        assert_eq!(tab_order.iter().filter(|id| *id == &tab_id).count(), 1);
    }

    #[test]
    fn restore_spaces_backfills_empty_root_order() {
        let mut manager = SpaceManager::new();
        let space_id = SpaceId::generate();
        let tab_id = TabId::generate();
        let folder_id = FolderId::generate();
        let child_folder_id = FolderId::generate();

        let restored = Space {
            id: space_id.clone(),
            profile_id: ProfileId::new(""),
            name: "Legacy".to_string(),
            color: SpaceColor {
                hue: 1.0,
                saturation: 0.5,
                brightness: 0.5,
                grain: 0.0,
            },
            theme: None,
            icon: None,
            tab_order: vec![tab_id.clone()],
            folders: vec![
                Folder {
                    id: folder_id.clone(),
                    name: "Root Folder".to_string(),
                    tab_ids: vec![],
                    is_expanded: true,
                    is_pinned: false,
                    parent_folder_id: None,
                    provider_type: None,
                    config_json: None,
                },
                Folder {
                    id: child_folder_id,
                    name: "Nested Folder".to_string(),
                    tab_ids: vec![],
                    is_expanded: true,
                    is_pinned: false,
                    parent_folder_id: Some(folder_id.clone()),
                    provider_type: None,
                    config_json: None,
                },
            ],
            root_order: vec![],
            atc_rules: vec![],
            is_active: true,
            created_at: DateTime::now(),
            last_active_tab_id: None,
        };

        manager.restore_spaces(vec![restored], space_id.clone());

        assert_eq!(
            manager.get_root_order(&space_id),
            vec![RootItem::Folder(folder_id), RootItem::Tab(tab_id)]
        );
    }

    #[test]
    fn restore_spaces_preserves_explicit_root_order() {
        let mut manager = SpaceManager::new();
        let space_id = SpaceId::generate();
        let tab_id = TabId::generate();
        let folder_id = FolderId::generate();
        let explicit_order = vec![
            RootItem::Folder(folder_id.clone()),
            RootItem::Tab(tab_id.clone()),
        ];

        let restored = Space {
            id: space_id.clone(),
            profile_id: ProfileId::new(""),
            name: "Modern".to_string(),
            color: SpaceColor {
                hue: 2.0,
                saturation: 0.6,
                brightness: 0.6,
                grain: 0.0,
            },
            theme: None,
            icon: None,
            tab_order: vec![tab_id.clone()],
            folders: vec![Folder {
                id: folder_id.clone(),
                name: "Root Folder".to_string(),
                tab_ids: vec![],
                is_expanded: true,
                is_pinned: false,
                parent_folder_id: None,
                provider_type: None,
                config_json: None,
            }],
            root_order: explicit_order.clone(),
            atc_rules: vec![],
            is_active: true,
            created_at: DateTime::now(),
            last_active_tab_id: None,
        };

        manager.restore_spaces(vec![restored], space_id.clone());

        assert_eq!(manager.get_root_order(&space_id), explicit_order);
    }

    #[test]
    fn add_tab_to_space_populates_root_order_once() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        manager.add_tab_to_space(&space_id, tab_id.clone(), Some(0));

        let root_order = manager.get_root_order(&space_id);
        assert_eq!(root_order, vec![RootItem::Tab(tab_id)]);
    }

    #[test]
    fn move_root_tab_to_end_of_pinned_places_after_last_pinned() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let p1 = TabId::generate();
        let n1 = TabId::generate();
        let p2 = TabId::generate();
        let x = TabId::generate();
        let n2 = TabId::generate();
        for id in [&x, &p1, &n1, &p2, &n2] {
            manager.add_tab_to_space(&space_id, id.clone(), None);
        }

        let pinned: HashSet<TabId> = [p1.clone(), p2.clone(), x.clone()].into_iter().collect();
        manager.move_root_tab_to_end_of_pinned(&space_id, &x, &pinned);

        assert_eq!(
            manager.get_root_order(&space_id),
            vec![
                RootItem::Tab(p1),
                RootItem::Tab(n1),
                RootItem::Tab(p2),
                RootItem::Tab(x),
                RootItem::Tab(n2),
            ]
        );
    }

    #[test]
    fn move_root_tab_to_end_of_pinned_inserts_at_front_when_no_other_pinned() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let n1 = TabId::generate();
        let x = TabId::generate();
        for id in [&n1, &x] {
            manager.add_tab_to_space(&space_id, id.clone(), None);
        }

        let pinned: HashSet<TabId> = [x.clone()].into_iter().collect();
        manager.move_root_tab_to_end_of_pinned(&space_id, &x, &pinned);

        assert_eq!(
            manager.get_root_order(&space_id),
            vec![RootItem::Tab(x), RootItem::Tab(n1)]
        );
    }

    #[test]
    fn remove_tab_from_space_removes_root_order_entry() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        manager.remove_tab_from_space(&space_id, &tab_id);

        assert!(manager.get_root_order(&space_id).is_empty());
    }

    #[test]
    fn remove_tab_from_space_clears_folder_references() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        let folder = manager.create_folder(&space_id, "F", false, None).unwrap();
        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        manager.add_tab_to_folder(&space_id, &folder.id, tab_id.clone());

        manager.remove_tab_from_space(&space_id, &tab_id);

        let space = manager.spaces.get(&space_id).unwrap();
        let f = space.folders.iter().find(|f| f.id == folder.id).unwrap();
        assert!(!f.tab_ids.contains(&tab_id));
        assert!(manager.get_tab_order(&space_id).is_empty());
    }

    #[test]
    fn add_tab_to_folder_removes_root_order_entry() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        let folder = manager.create_folder(&space_id, "F", false, None).unwrap();
        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        manager.add_tab_to_folder(&space_id, &folder.id, tab_id.clone());

        let root_order = manager.get_root_order(&space_id);
        assert!(!root_order.contains(&RootItem::Tab(tab_id)));
        assert!(root_order.contains(&RootItem::Folder(folder.id)));
    }

    #[test]
    fn move_tab_to_root_removes_from_folder_and_reorders_before_target() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let folder_id = FolderId::generate();
        let moved_tab = TabId::generate();
        let before_tab = TabId::generate();

        manager.add_tab_to_space(&space_id, moved_tab.clone(), None);
        manager.add_tab_to_space(&space_id, before_tab.clone(), None);

        if let Some(space) = manager.spaces.get_mut(&space_id) {
            space.folders.push(Folder {
                id: folder_id.clone(),
                name: "Folder".to_string(),
                tab_ids: vec![moved_tab.clone()],
                is_expanded: true,
                is_pinned: false,
                parent_folder_id: None,
                provider_type: None,
                config_json: None,
            });
            space.tab_order = vec![before_tab.clone(), moved_tab.clone()];
        }

        manager.move_tab_to_root(&space_id, &folder_id, &moved_tab, Some(&before_tab));

        let space = manager.spaces.get(&space_id).unwrap();
        assert!(space.folders.iter().any(|folder| folder.id == folder_id));
        assert_eq!(space.tab_order, vec![moved_tab, before_tab]);
    }

    #[test]
    fn create_folder_returns_view_model_with_correct_fields() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();

        let vm = manager
            .create_folder(&space_id, "Work", false, None)
            .unwrap();

        assert_eq!(vm.name, "Work");
        assert!(!vm.is_pinned);
        assert_eq!(vm.tab_count, 0);
        assert!(vm.tab_ids.is_empty());
        assert!(vm.parent_folder_id.is_none());
        assert!(vm.is_expanded);

        let space = manager.spaces.get(&space_id).unwrap();
        assert!(space.folders.iter().any(|f| f.id == vm.id));
    }

    #[test]
    fn create_folder_populates_root_order_once() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();

        let vm = manager
            .create_folder(&space_id, "Work", false, None)
            .unwrap();

        assert_eq!(
            manager.get_root_order(&space_id),
            vec![RootItem::Folder(vm.id)]
        );
    }

    #[test]
    fn delete_folder_removes_root_order_entry() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();

        let vm = manager
            .create_folder(&space_id, "Work", false, None)
            .unwrap();
        manager.delete_folder(&space_id, &vm.id);

        assert!(manager.get_root_order(&space_id).is_empty());
    }

    #[test]
    fn delete_folder_promotes_tabs_to_root_at_folder_position() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let before = TabId::new("before");
        let inside_a = TabId::new("inside-a");
        let inside_b = TabId::new("inside-b");
        let after = TabId::new("after");
        manager.add_tab_to_space(&space_id, before.clone(), None);
        let vm = manager
            .create_folder(&space_id, "Work", false, None)
            .unwrap();
        for tab_id in [&inside_a, &inside_b, &after] {
            manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        }
        manager.add_tab_to_folder(&space_id, &vm.id, inside_a.clone());
        manager.add_tab_to_folder(&space_id, &vm.id, inside_b.clone());

        manager.delete_folder(&space_id, &vm.id);

        assert_eq!(
            manager.get_root_order(&space_id),
            vec![
                RootItem::Tab(before),
                RootItem::Tab(inside_a),
                RootItem::Tab(inside_b),
                RootItem::Tab(after),
            ]
        );
    }

    #[test]
    fn delete_folder_promotes_subfolder_and_tabs_into_parent() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let outer = manager
            .create_folder(&space_id, "Outer", false, None)
            .unwrap();
        let middle = manager
            .create_folder(&space_id, "Middle", false, Some(outer.id.clone()))
            .unwrap();
        let inner = manager
            .create_folder(&space_id, "Inner", false, Some(middle.id.clone()))
            .unwrap();
        let tab_id = TabId::new("in-middle");
        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        manager.add_tab_to_folder(&space_id, &middle.id, tab_id.clone());

        manager.delete_folder(&space_id, &middle.id);

        let space = manager.get_space(&space_id).unwrap();
        let outer_folder = space.folders.iter().find(|f| f.id == outer.id).unwrap();
        assert_eq!(outer_folder.tab_ids, vec![tab_id]);
        let inner_folder = space.folders.iter().find(|f| f.id == inner.id).unwrap();
        assert_eq!(inner_folder.parent_folder_id, Some(outer.id.clone()));
        assert_eq!(
            manager.get_root_order(&space_id),
            vec![RootItem::Folder(outer.id)]
        );
    }

    #[test]
    fn delete_root_folder_promotes_subfolder_to_root() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let outer = manager
            .create_folder(&space_id, "Outer", false, None)
            .unwrap();
        let inner = manager
            .create_folder(&space_id, "Inner", false, Some(outer.id.clone()))
            .unwrap();

        manager.delete_folder(&space_id, &outer.id);

        let space = manager.get_space(&space_id).unwrap();
        let inner_folder = space.folders.iter().find(|f| f.id == inner.id).unwrap();
        assert_eq!(inner_folder.parent_folder_id, None);
        assert_eq!(
            manager.get_root_order(&space_id),
            vec![RootItem::Folder(inner.id)]
        );
    }

    #[test]
    fn create_folder_pinned() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();

        let vm = manager
            .create_folder(&space_id, "Pinned Folder", true, None)
            .unwrap();

        assert!(vm.is_pinned);
        let space = manager.spaces.get(&space_id).unwrap();
        let folder = space.folders.iter().find(|f| f.id == vm.id).unwrap();
        assert!(folder.is_pinned);
    }

    #[test]
    fn toggle_folder_expanded_is_strict_to_supplied_space_id() {
        let mut manager = SpaceManager::new();
        let active_space_id = manager.get_active_space_id().clone();
        let other_space = manager.create_space(
            "Other",
            SpaceColor {
                hue: 10.0,
                saturation: 0.5,
                brightness: 0.5,
                grain: 0.0,
            },
            ProfileId::new(""),
        );

        let folder = manager
            .create_folder(&active_space_id, "Research", false, None)
            .unwrap();
        assert!(folder.is_expanded);

        let toggled = manager.toggle_folder_expanded(&other_space.id, &folder.id);
        assert!(!toggled);
        assert_eq!(
            manager.find_folder_expanded(&other_space.id, &folder.id),
            None
        );
        assert_eq!(
            manager.find_folder_expanded(&active_space_id, &folder.id),
            Some(true)
        );

        let tree = manager.get_sidebar_tree(&active_space_id, |_| None);
        let research = tree.into_iter().find_map(|node| match node {
            SidebarNode::Folder {
                id, is_expanded, ..
            } if id == folder.id => Some(is_expanded),
            _ => None,
        });
        assert_eq!(research, Some(true));
    }

    #[test]
    fn set_folder_pinned_true_then_false_transition() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();

        let folder = manager
            .create_folder(&space_id, "Work", false, None)
            .unwrap();
        assert!(!folder.is_pinned);

        assert!(manager.set_folder_pinned(&space_id, &folder.id, true));
        let space = manager.spaces.get(&space_id).unwrap();
        assert!(
            space
                .folders
                .iter()
                .find(|f| f.id == folder.id)
                .unwrap()
                .is_pinned,
            "Normal->Pinned must set is_pinned=true"
        );

        assert!(manager.set_folder_pinned(&space_id, &folder.id, false));
        let space = manager.spaces.get(&space_id).unwrap();
        assert!(
            !space
                .folders
                .iter()
                .find(|f| f.id == folder.id)
                .unwrap()
                .is_pinned,
            "Pinned->Normal must set is_pinned=false"
        );
    }

    #[test]
    fn set_folder_pinned_wrong_space_is_noop() {
        let mut manager = SpaceManager::new();
        let active_space_id = manager.get_active_space_id().clone();
        let other_space = manager.create_space(
            "Other",
            SpaceColor {
                hue: 10.0,
                saturation: 0.5,
                brightness: 0.5,
                grain: 0.0,
            },
            ProfileId::new(""),
        );

        let folder = manager
            .create_folder(&active_space_id, "Research", false, None)
            .unwrap();

        assert!(!manager.set_folder_pinned(&other_space.id, &folder.id, true));
        let space = manager.spaces.get(&active_space_id).unwrap();
        assert!(
            !space
                .folders
                .iter()
                .find(|f| f.id == folder.id)
                .unwrap()
                .is_pinned,
            "wrong-space call must not mutate the folder"
        );
    }

    #[test]
    fn set_folder_pinned_unknown_folder_is_noop() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let unknown = FolderId::generate();

        assert!(!manager.set_folder_pinned(&space_id, &unknown, true));
    }

    #[test]
    fn add_tab_to_folder_adds_tab() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        let vm = manager.create_folder(&space_id, "F", false, None).unwrap();
        manager.add_tab_to_folder(&space_id, &vm.id, tab_id.clone());

        let space = manager.spaces.get(&space_id).unwrap();
        let folder = space.folders.iter().find(|f| f.id == vm.id).unwrap();
        assert_eq!(folder.tab_ids, vec![tab_id]);
    }

    #[test]
    fn add_tab_to_folder_removes_from_previous_folder() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        let folder_a = manager.create_folder(&space_id, "A", false, None).unwrap();
        let folder_b = manager.create_folder(&space_id, "B", false, None).unwrap();

        manager.add_tab_to_folder(&space_id, &folder_a.id, tab_id.clone());
        manager.add_tab_to_folder(&space_id, &folder_b.id, tab_id.clone());

        let space = manager.spaces.get(&space_id).unwrap();
        let a = space.folders.iter().find(|f| f.id == folder_a.id).unwrap();
        let b = space.folders.iter().find(|f| f.id == folder_b.id).unwrap();
        assert!(!a.tab_ids.contains(&tab_id));
        assert_eq!(b.tab_ids, vec![tab_id]);
    }

    #[test]
    fn remove_tab_from_folder_removes_tab() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        let vm = manager.create_folder(&space_id, "F", false, None).unwrap();
        manager.add_tab_to_folder(&space_id, &vm.id, tab_id.clone());
        manager.remove_tab_from_folder(&space_id, &vm.id, &tab_id);

        let space = manager.spaces.get(&space_id).unwrap();
        let folder = space.folders.iter().find(|f| f.id == vm.id).unwrap();
        assert!(!folder.tab_ids.contains(&tab_id));
    }

    #[test]
    fn reorder_tab_in_folder_changes_position() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let t1 = TabId::generate();
        let t2 = TabId::generate();
        let t3 = TabId::generate();

        let vm = manager.create_folder(&space_id, "F", false, None).unwrap();
        manager.add_tab_to_folder(&space_id, &vm.id, t1.clone());
        manager.add_tab_to_folder(&space_id, &vm.id, t2.clone());
        manager.add_tab_to_folder(&space_id, &vm.id, t3.clone());

        manager.reorder_tab_in_folder(&space_id, &vm.id, &t1, 2);

        let space = manager.spaces.get(&space_id).unwrap();
        let folder = space.folders.iter().find(|f| f.id == vm.id).unwrap();
        assert_eq!(folder.tab_ids, vec![t2, t3, t1]);
    }

    #[test]
    fn reorder_folder_root_before_sibling() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();

        let f1 = manager.create_folder(&space_id, "F1", false, None).unwrap();
        let f2 = manager.create_folder(&space_id, "F2", false, None).unwrap();
        let f3 = manager.create_folder(&space_id, "F3", false, None).unwrap();

        let _ = manager.reorder_folder(&space_id, &f3.id, None, Some(&f1.id));

        let space = manager.spaces.get(&space_id).unwrap();
        let ids: Vec<_> = space.folders.iter().map(|f| f.id.clone()).collect();
        assert_eq!(ids, vec![f3.id, f1.id, f2.id]);
    }

    #[test]
    fn reorder_folder_nested_same_parent() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();

        let parent = manager
            .create_folder(&space_id, "Parent", false, None)
            .unwrap();
        let child1 = manager.create_folder(&space_id, "C1", false, None).unwrap();
        let child2 = manager.create_folder(&space_id, "C2", false, None).unwrap();

        if let Some(space) = manager.spaces.get_mut(&space_id) {
            for f in space.folders.iter_mut() {
                if f.id == child1.id || f.id == child2.id {
                    f.parent_folder_id = Some(parent.id.clone());
                }
            }
        }

        let _ = manager.reorder_folder(&space_id, &child2.id, Some(&parent.id), Some(&child1.id));

        let space = manager.spaces.get(&space_id).unwrap();
        let children: Vec<_> = space
            .folders
            .iter()
            .filter(|f| f.parent_folder_id.as_ref() == Some(&parent.id))
            .map(|f| f.id.clone())
            .collect();
        assert_eq!(children, vec![child2.id, child1.id]);
    }

    #[test]
    fn move_folder_into_folder_removes_root_order_entry() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();

        let parent = manager
            .create_folder(&space_id, "Parent", false, None)
            .unwrap();
        let child = manager
            .create_folder(&space_id, "Child", false, None)
            .unwrap();

        let _ = manager.reorder_folder(&space_id, &child.id, Some(&parent.id), None);

        let root_order = manager.get_root_order(&space_id);
        assert!(root_order.contains(&RootItem::Folder(parent.id)));
        assert!(!root_order.contains(&RootItem::Folder(child.id)));
    }

    #[test]
    fn reorder_root_item_tab_before_folder() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab = TabId::generate();
        let folder_id = FolderId::generate();

        let tab_item = RootItem::Tab(tab.clone());
        let folder_item = RootItem::Folder(folder_id.clone());

        if let Some(space) = manager.spaces.get_mut(&space_id) {
            space.root_order = vec![folder_item.clone(), tab_item.clone()];
        }

        manager.reorder_root_item(
            &space_id,
            &tab_item,
            &RootInsertionPoint::Before {
                target: folder_item.clone(),
            },
        );

        let order = manager.get_root_order(&space_id);
        assert_eq!(order, vec![tab_item, folder_item]);
    }

    #[test]
    fn reorder_root_item_folder_before_tab() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab = TabId::generate();
        let folder_id = FolderId::generate();

        let tab_item = RootItem::Tab(tab.clone());
        let folder_item = RootItem::Folder(folder_id.clone());

        if let Some(space) = manager.spaces.get_mut(&space_id) {
            space.root_order = vec![tab_item.clone(), folder_item.clone()];
        }

        manager.reorder_root_item(
            &space_id,
            &folder_item,
            &RootInsertionPoint::Before {
                target: tab_item.clone(),
            },
        );

        let order = manager.get_root_order(&space_id);
        assert_eq!(order, vec![folder_item, tab_item]);
    }

    #[test]
    fn reorder_root_item_append_after_last() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab = TabId::generate();
        let folder_id = FolderId::generate();
        let new_tab = TabId::generate();

        let tab_item = RootItem::Tab(tab.clone());
        let folder_item = RootItem::Folder(folder_id.clone());
        let new_tab_item = RootItem::Tab(new_tab.clone());

        if let Some(space) = manager.spaces.get_mut(&space_id) {
            space.root_order = vec![tab_item.clone(), folder_item.clone()];
        }

        manager.reorder_root_item(&space_id, &new_tab_item, &RootInsertionPoint::Append);

        let order = manager.get_root_order(&space_id);
        assert_eq!(order, vec![tab_item, folder_item, new_tab_item]);
    }

    #[test]
    fn update_space_config_theme_derives_color() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();

        let primary_color = ThemeColor {
            hue: 0.6,
            saturation: 0.8,
            brightness: 0.9,
            is_custom: false,
            is_primary: true,
            c_raw: None,
            position: None,
            lightness: None,
            algorithm: None,
        };
        let theme = SpaceTheme::Gradient {
            colors: vec![primary_color],
            harmony: ColorHarmony::Analogous,
            scheme: None,
            opacity: 0.5,
            texture: 0.0,
            schema_version: 0,
        };
        let changes = SpaceConfigUpdate {
            space_id: space_id.clone(),
            name: None,
            color: None,
            theme: Some(theme.clone()),
            icon: None,
            profile_id: None,
        };

        let result = manager.update_space_config(changes).unwrap();
        assert!(result.is_some());

        let space = manager.spaces.get(&space_id).unwrap();
        let derived = theme.base_color();
        assert_eq!(space.color.hue, derived.hue);
        assert_eq!(space.color.saturation, derived.saturation);
        assert_eq!(space.color.brightness, derived.brightness);
    }

    #[test]
    fn update_space_config_explicit_color_overrides_theme_derived_color() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();

        let primary_color = ThemeColor {
            hue: 0.6,
            saturation: 0.8,
            brightness: 0.9,
            is_custom: false,
            is_primary: true,
            c_raw: None,
            position: None,
            lightness: None,
            algorithm: None,
        };
        let theme = SpaceTheme::Gradient {
            colors: vec![primary_color],
            harmony: ColorHarmony::Analogous,
            scheme: None,
            opacity: 0.5,
            texture: 0.0,
            schema_version: 0,
        };
        let override_color = SpaceColor {
            hue: 0.1,
            saturation: 0.2,
            brightness: 0.3,
            grain: 0.0,
        };
        let changes = SpaceConfigUpdate {
            space_id: space_id.clone(),
            name: None,
            color: Some(override_color.clone()),
            theme: Some(theme),
            icon: None,
            profile_id: None,
        };

        manager.update_space_config(changes).unwrap();

        let space = manager.spaces.get(&space_id).unwrap();
        assert_eq!(space.color.hue, override_color.hue);
        assert_eq!(space.color.saturation, override_color.saturation);
        assert_eq!(space.color.brightness, override_color.brightness);
    }

    #[test]
    fn update_space_config_zen_theme_derives_color() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();

        let primary_color = ThemeColor {
            hue: 0.25,
            saturation: 0.6,
            brightness: 0.75,
            is_custom: false,
            is_primary: true,
            c_raw: None,
            position: None,
            lightness: None,
            algorithm: None,
        };
        let theme = SpaceTheme::Zen {
            colors: vec![primary_color],
            harmony: None,
            scheme: None,
            opacity: 0.8,
            texture: 0.1,
            schema_version: 0,
        };
        let changes = SpaceConfigUpdate {
            space_id: space_id.clone(),
            name: None,
            color: None,
            theme: Some(theme.clone()),
            icon: None,
            profile_id: None,
        };

        manager.update_space_config(changes).unwrap();

        let space = manager.spaces.get(&space_id).unwrap();
        let derived = theme.base_color();
        assert_eq!(space.color.hue, derived.hue);
        assert_eq!(space.color.saturation, derived.saturation);
        assert_eq!(space.color.brightness, derived.brightness);
    }

    #[test]
    fn remove_tab_from_root_order_removes_the_entry() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        assert!(manager
            .get_root_order(&space_id)
            .contains(&RootItem::Tab(tab_id.clone())));

        manager.remove_tab_from_root_order(&space_id, &tab_id);

        assert!(!manager
            .get_root_order(&space_id)
            .contains(&RootItem::Tab(tab_id)));
    }

    #[test]
    fn remove_tab_from_root_order_is_idempotent() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        manager.remove_tab_from_root_order(&space_id, &tab_id);
        manager.remove_tab_from_root_order(&space_id, &tab_id);

        assert!(manager.get_root_order(&space_id).is_empty());
    }

    #[test]
    fn add_tab_to_root_order_if_absent_appends_missing_tab() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        manager.remove_tab_from_root_order(&space_id, &tab_id);
        assert!(manager.get_root_order(&space_id).is_empty());

        manager.add_tab_to_root_order_if_absent(&space_id, &tab_id);

        assert_eq!(
            manager.get_root_order(&space_id),
            vec![RootItem::Tab(tab_id)]
        );
    }

    #[test]
    fn add_tab_to_root_order_if_absent_is_idempotent() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        manager.remove_tab_from_root_order(&space_id, &tab_id);

        manager.add_tab_to_root_order_if_absent(&space_id, &tab_id);
        manager.add_tab_to_root_order_if_absent(&space_id, &tab_id);

        let order = manager.get_root_order(&space_id);
        assert_eq!(
            order
                .iter()
                .filter(|i| *i == &RootItem::Tab(tab_id.clone()))
                .count(),
            1
        );
    }

    #[test]
    fn strip_favorited_tabs_from_root_orders_removes_favorited_entries() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_a = TabId::generate();
        let tab_b = TabId::generate();

        manager.add_tab_to_space(&space_id, tab_a.clone(), None);
        manager.add_tab_to_space(&space_id, tab_b.clone(), None);

        let mut favorited = std::collections::HashSet::new();
        favorited.insert(tab_a.clone());

        manager.strip_favorited_tabs_from_root_orders(&favorited);

        let order = manager.get_root_order(&space_id);
        assert!(!order.contains(&RootItem::Tab(tab_a)));
        assert!(order.contains(&RootItem::Tab(tab_b)));
    }

    #[test]
    fn strip_favorited_tabs_from_root_orders_preserves_folders() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();
        let folder_vm = manager.create_folder(&space_id, "F", false, None).unwrap();

        manager.add_tab_to_space(&space_id, tab_id.clone(), None);

        let mut favorited = std::collections::HashSet::new();
        favorited.insert(tab_id.clone());

        manager.strip_favorited_tabs_from_root_orders(&favorited);

        let order = manager.get_root_order(&space_id);
        assert!(!order.contains(&RootItem::Tab(tab_id)));
        assert!(order.contains(&RootItem::Folder(folder_vm.id)));
    }

    #[test]
    fn restore_spaces_with_favorited_tab_in_root_order_is_normalized_by_strip() {
        let mut manager = SpaceManager::new();
        let space_id = SpaceId::generate();
        let tab_a = TabId::generate();
        let tab_b = TabId::generate();

        let restored = Space {
            id: space_id.clone(),
            profile_id: ProfileId::new(""),
            name: "Legacy".to_string(),
            color: SpaceColor {
                hue: 1.0,
                saturation: 0.5,
                brightness: 0.5,
                grain: 0.0,
            },
            theme: None,
            icon: None,
            tab_order: vec![tab_a.clone(), tab_b.clone()],
            folders: vec![],
            root_order: vec![RootItem::Tab(tab_a.clone()), RootItem::Tab(tab_b.clone())],
            atc_rules: vec![],
            is_active: true,
            created_at: maho_types::common::DateTime::now(),
            last_active_tab_id: None,
        };

        manager.restore_spaces(vec![restored], space_id.clone());

        let mut favorited = std::collections::HashSet::new();
        favorited.insert(tab_a.clone());

        manager.strip_favorited_tabs_from_root_orders(&favorited);

        let order = manager.get_root_order(&space_id);
        assert!(
            !order.contains(&RootItem::Tab(tab_a)),
            "favorited tab must be absent"
        );
        assert!(
            order.contains(&RootItem::Tab(tab_b)),
            "non-favorited tab must remain"
        );
    }

    #[test]
    fn is_tab_in_folder_returns_true_when_tab_in_folder() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        let folder = manager.create_folder(&space_id, "F", false, None).unwrap();
        manager.add_tab_to_folder(&space_id, &folder.id, tab_id.clone());

        assert!(manager.is_tab_in_folder(&space_id, &tab_id));
    }

    #[test]
    fn is_tab_in_folder_returns_false_when_tab_at_root() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        manager.add_tab_to_space(&space_id, tab_id.clone(), None);

        assert!(!manager.is_tab_in_folder(&space_id, &tab_id));
    }

    #[test]
    fn apply_tab_residency_favorite_removes_from_root_order() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        assert!(manager
            .get_root_order(&space_id)
            .contains(&RootItem::Tab(tab_id.clone())));

        manager.apply_tab_residency(&space_id, &tab_id, true, false);

        assert!(!manager
            .get_root_order(&space_id)
            .contains(&RootItem::Tab(tab_id)));
    }

    #[test]
    fn apply_tab_residency_non_favorite_root_adds_to_root_order() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        manager.remove_tab_from_root_order(&space_id, &tab_id);
        assert!(!manager
            .get_root_order(&space_id)
            .contains(&RootItem::Tab(tab_id.clone())));

        manager.apply_tab_residency(&space_id, &tab_id, false, false);

        assert!(manager
            .get_root_order(&space_id)
            .contains(&RootItem::Tab(tab_id)));
    }

    #[test]
    fn apply_tab_residency_folder_resident_removes_from_root_order() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        assert!(manager
            .get_root_order(&space_id)
            .contains(&RootItem::Tab(tab_id.clone())));

        manager.apply_tab_residency(&space_id, &tab_id, false, true);

        assert!(!manager
            .get_root_order(&space_id)
            .contains(&RootItem::Tab(tab_id)));
    }

    #[test]
    fn apply_tab_residency_non_favorite_root_is_idempotent() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        manager.apply_tab_residency(&space_id, &tab_id, false, false);
        manager.apply_tab_residency(&space_id, &tab_id, false, false);

        let count = manager
            .get_root_order(&space_id)
            .iter()
            .filter(|i| *i == &RootItem::Tab(tab_id.clone()))
            .count();
        assert_eq!(
            count, 1,
            "tab must appear exactly once after idempotent residency calls"
        );
    }

    #[test]
    fn reorder_folder_nested_to_root_updates_parent_and_root_order() {
        // Regression: promoting a nested folder to root via reorder_folder(_, None, Some(&before))
        // must both clear parent_folder_id and insert the folder into root_order at the
        // correct position.  Previously the root_order insertion was skipped when the folder
        // was not already in root_order.

        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();

        let anchor = manager
            .create_folder(&space_id, "Anchor", false, None)
            .unwrap();
        let parent = manager
            .create_folder(&space_id, "Parent", false, None)
            .unwrap();
        let nested = manager
            .create_folder(&space_id, "Nested", false, None)
            .unwrap();

        if let Some(space) = manager.spaces.get_mut(&space_id) {
            if let Some(f) = space.folders.iter_mut().find(|f| f.id == nested.id) {
                f.parent_folder_id = Some(parent.id.clone());
            }
            space
                .root_order
                .retain(|item| item != &RootItem::Folder(nested.id.clone()));
        }

        assert!(
            !manager
                .get_root_order(&space_id)
                .contains(&RootItem::Folder(nested.id.clone())),
            "precondition: nested must not be in root_order before promotion"
        );

        let _ = manager.reorder_folder(&space_id, &nested.id, None, Some(&anchor.id));

        let space = manager.spaces.get(&space_id).unwrap();
        let promoted = space.folders.iter().find(|f| f.id == nested.id).unwrap();
        assert!(
            promoted.parent_folder_id.is_none(),
            "promoted folder must have no parent"
        );

        let root_order = manager.get_root_order(&space_id);
        let nested_pos = root_order
            .iter()
            .position(|i| i == &RootItem::Folder(nested.id.clone()));
        let anchor_pos = root_order
            .iter()
            .position(|i| i == &RootItem::Folder(anchor.id.clone()));
        assert!(
            nested_pos.is_some(),
            "promoted folder must be present in root_order"
        );
        assert!(
            anchor_pos.is_some(),
            "anchor folder must still be in root_order"
        );
        assert!(
            nested_pos.unwrap() < anchor_pos.unwrap(),
            "promoted folder must appear before anchor in root_order"
        );
    }

    #[test]
    fn move_folder_in_space_rejects_cross_space_parent() {
        let mut manager = SpaceManager::new();
        let space_a = manager.get_active_space_id().clone();

        let folder_a = manager.create_folder(&space_a, "FA", false, None).unwrap();

        let space_b = manager
            .create_space(
                "SpaceB",
                maho_types::space::SpaceColor {
                    hue: 1.0,
                    saturation: 0.5,
                    brightness: 0.5,
                    grain: 0.0,
                },
                maho_types::identifiers::ProfileId::new(""),
            )
            .id;

        let parent_b = manager
            .create_folder(&space_b, "Parent", false, None)
            .unwrap();

        let res = manager.move_folder_in_space(&space_a, &folder_a.id, Some(parent_b.id.clone()));
        assert!(matches!(res, Err(FolderError::FolderNotFound)));

        let space_a_data = manager.spaces.get(&space_a).unwrap();
        let folder_in_a = space_a_data
            .folders
            .iter()
            .find(|f| f.id == folder_a.id)
            .unwrap();
        assert_eq!(
            folder_in_a.parent_folder_id, None,
            "folder in space_a should NOT have parent from space_b"
        );
    }

    #[test]
    fn remove_empty_folders_in_space_deletes_only_empty() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        let empty_folder = manager
            .create_folder(&space_id, "Empty", false, None)
            .unwrap();
        let full_folder = manager
            .create_folder(&space_id, "Full", false, None)
            .unwrap();

        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        manager.add_tab_to_folder(&space_id, &full_folder.id, tab_id.clone());

        let removed = manager.remove_empty_folders_in_space(&space_id);

        assert_eq!(removed, vec![empty_folder.id.clone()]);
        let space = manager.spaces.get(&space_id).unwrap();
        assert!(!space.folders.iter().any(|f| f.id == empty_folder.id));
        assert!(space.folders.iter().any(|f| f.id == full_folder.id));
        assert!(!space
            .root_order
            .contains(&RootItem::Folder(empty_folder.id)));
        assert!(space.root_order.contains(&RootItem::Folder(full_folder.id)));
    }

    #[test]
    fn get_all_atc_rules_respects_space_order_and_persists_across_reorder_and_restore() {
        use maho_types::identifiers::ProfileId;
        use maho_types::space::{ATCAction, ATCCondition, ATCRule, SpaceColor};

        let mut manager = SpaceManager::new();
        // Clear default space created by new() to have full control of space order.
        let default_space_id = manager.get_active_space_id().clone();

        let space_a = manager.create_space(
            "Space A",
            SpaceColor {
                hue: 10.0,
                saturation: 0.5,
                brightness: 0.5,
                grain: 0.0,
            },
            ProfileId::new("p1"),
        );
        let space_b = manager.create_space(
            "Space B",
            SpaceColor {
                hue: 20.0,
                saturation: 0.5,
                brightness: 0.5,
                grain: 0.0,
            },
            ProfileId::new("p1"),
        );
        let space_c = manager.create_space(
            "Space C",
            SpaceColor {
                hue: 30.0,
                saturation: 0.5,
                brightness: 0.5,
                grain: 0.0,
            },
            ProfileId::new("p1"),
        );

        let rule_a1 = ATCRule {
            id: "a1".to_string(),
            condition: ATCCondition::UrlContains {
                text: "shared.com".to_string(),
            },
            action: ATCAction::Route {
                space_id: space_a.id.clone(),
            },
            enabled: true,
        };
        let rule_a2 = ATCRule {
            id: "a2".to_string(),
            condition: ATCCondition::UrlContains {
                text: "shared.com".to_string(),
            },
            action: ATCAction::Route {
                space_id: space_c.id.clone(),
            },
            enabled: true,
        };
        let rule_b1 = ATCRule {
            id: "b1".to_string(),
            condition: ATCCondition::UrlContains {
                text: "shared.com".to_string(),
            },
            action: ATCAction::Route {
                space_id: space_b.id.clone(),
            },
            enabled: true,
        };
        let rule_c1 = ATCRule {
            id: "c1".to_string(),
            condition: ATCCondition::UrlContains {
                text: "c-only.com".to_string(),
            },
            action: ATCAction::Route {
                space_id: space_c.id.clone(),
            },
            enabled: true,
        };

        manager.spaces.get_mut(&space_a.id).unwrap().atc_rules =
            vec![rule_a1.clone(), rule_a2.clone()];
        manager.spaces.get_mut(&space_b.id).unwrap().atc_rules = vec![rule_b1.clone()];
        manager.spaces.get_mut(&space_c.id).unwrap().atc_rules = vec![rule_c1.clone()];
        manager.delete_space(&default_space_id);

        // 1. Initial saved order: [A, B, C]
        let rules = manager.get_all_atc_rules();
        let space_ids: Vec<SpaceId> = rules.iter().map(|(id, _)| id.clone()).collect();
        assert_eq!(space_ids, vec![space_a.id.clone(), space_b.id.clone(), space_c.id.clone()]);
        let a_rule_ids: Vec<String> = rules[0].1.iter().map(|r| r.id.clone()).collect();
        assert_eq!(a_rule_ids, vec![rule_a1.id.clone(), rule_a2.id.clone()]);

        let atc = crate::atc_manager::ATCManager::new();
        let dest = atc.route_url_by_space_rules("https://shared.com/login", &rules);
        assert_eq!(dest, Some(space_a.id.clone()));

        // 2. Reorder spaces: move Space B to index 0 -> [B, A, C]
        manager.reorder_space(&space_b.id, 1, 0);
        let rules_after_reorder = manager.get_all_atc_rules();
        let space_ids_reordered: Vec<SpaceId> = rules_after_reorder.iter().map(|(id, _)| id.clone()).collect();
        assert_eq!(
            space_ids_reordered,
            vec![space_b.id.clone(), space_a.id.clone(), space_c.id.clone()]
        );
        // Overlapping rule resolution now picks Space B first according to new saved order
        let dest_reordered =
            atc.route_url_by_space_rules("https://shared.com/login", &rules_after_reorder);
        assert_eq!(dest_reordered, Some(space_b.id.clone()));

        // 3. Restore spaces in arbitrary / new order: [C, B, A]
        let space_a_obj = manager.get_space(&space_a.id).unwrap().clone();
        let space_b_obj = manager.get_space(&space_b.id).unwrap().clone();
        let space_c_obj = manager.get_space(&space_c.id).unwrap().clone();
        manager.restore_spaces(
            vec![space_c_obj, space_b_obj, space_a_obj],
            space_c.id.clone(),
        );

        let rules_after_restore = manager.get_all_atc_rules();
        let space_ids_restored: Vec<SpaceId> = rules_after_restore.iter().map(|(id, _)| id.clone()).collect();
        assert_eq!(
            space_ids_restored,
            vec![space_c.id.clone(), space_b.id.clone(), space_a.id.clone()]
        );
        // Rule order within each space preserved
        let restored_a_rule_ids: Vec<String> = rules_after_restore
            .iter()
            .find(|(id, _)| id == &space_a.id)
            .unwrap()
            .1
            .iter()
            .map(|r| r.id.clone())
            .collect();
        assert_eq!(
            restored_a_rule_ids,
            vec![rule_a1.id, rule_a2.id]
        );
        // Overlapping rule matches B before A under [C, B, A]
        let dest_restored =
            atc.route_url_by_space_rules("https://shared.com/login", &rules_after_restore);
        assert_eq!(dest_restored, Some(space_b.id.clone()));
    }
}

impl std::fmt::Display for SpaceUpdateError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::EmptyName => write!(f, "Space name cannot be empty"),
            Self::DuplicateName => write!(f, "Space name must be unique"),
        }
    }
}

impl SpaceUpdateError {
    pub fn code(&self) -> &'static str {
        match self {
            Self::EmptyName => "INVALID_NAME",
            Self::DuplicateName => "DUPLICATE_NAME",
        }
    }
}

pub struct SpaceManager {
    spaces: HashMap<SpaceId, Space>,
    space_order: Vec<SpaceId>,
    active_space_id: SpaceId,
    pub is_restoring: bool,
}

/// Canonicalizes a space icon to a renderable glyph.
///
/// Every shell (desktop sidebar, iOS, Android) draws `Space.icon` as text, so
/// the stored value must be a glyph. Browser importers hand over icon *names*
/// (Arc `iconType.icon`: "bulb", "barbell", ...), which then painted as literal
/// words. Known names map to an equivalent emoji; unknown multi-letter ASCII
/// names are dropped so shells fall back to the color dot; non-ASCII values
/// (emoji, flags, CJK) and single-character monograms are kept verbatim.
pub fn normalize_space_icon(raw: &str) -> Option<String> {
    let trimmed = raw.trim();
    if trimmed.is_empty() {
        return None;
    }
    if !trimmed.is_ascii() || trimmed.chars().count() == 1 {
        return Some(trimmed.to_string());
    }
    let key: String = trimmed
        .to_ascii_lowercase()
        .trim_end_matches(".fill")
        .chars()
        .filter(|c| c.is_ascii_alphanumeric())
        .collect();
    let emoji = match key.as_str() {
        "bulb" | "lightbulb" | "idea" => "💡",
        "barbell" | "dumbbell" | "fitness" | "gym" => "🏋️",
        "flash" | "bolt" | "lightning" | "zap" => "⚡",
        "layers" | "stack" | "squarestack" => "🗂️",
        "star" => "⭐",
        "video" | "film" | "movie" | "play" => "🎬",
        "briefcase" | "work" => "💼",
        "compass" | "explore" => "🧭",
        "house" | "home" => "🏠",
        "book" | "books" | "read" => "📚",
        "music" | "musicnote" => "🎵",
        "heart" | "love" => "❤️",
        "code" | "laptop" | "computer" | "terminal" => "💻",
        "globe" | "world" | "earth" => "🌐",
        "rocket" => "🚀",
        "palette" | "paintbrush" | "paint" | "art" | "design" => "🎨",
        "game" | "games" | "gamecontroller" | "gamepad" => "🎮",
        "cart" | "shopping" | "bag" | "shop" => "🛒",
        "flag" => "🚩",
        "leaf" | "plant" | "nature" => "🌿",
        "sun" | "sunmax" => "☀️",
        "moon" | "night" => "🌙",
        "cloud" => "☁️",
        "flame" | "fire" => "🔥",
        "bell" => "🔔",
        "gear" | "settings" | "cog" => "⚙️",
        "person" | "user" | "profile" => "👤",
        "people" | "users" | "team" | "group" => "👥",
        "mail" | "envelope" | "email" | "inbox" => "✉️",
        "calendar" => "📅",
        "chart" | "graph" | "barchart" | "stats" => "📊",
        "money" | "dollar" | "dollarsign" | "finance" => "💵",
        "graduationcap" | "school" | "education" | "study" => "🎓",
        "airplane" | "plane" | "travel" => "✈️",
        "coffee" | "cup" | "mug" => "☕",
        "pencil" | "pen" | "edit" | "write" => "✏️",
        "sparkles" | "sparkle" | "magic" => "✨",
        "trophy" => "🏆",
        "target" => "🎯",
        "key" => "🔑",
        "lock" => "🔒",
        "box" | "package" | "cube" => "📦",
        "folder" => "📁",
        "camera" | "photo" => "📷",
        "paw" | "pet" | "pawprint" => "🐾",
        "brain" => "🧠",
        "search" | "magnifyingglass" => "🔍",
        "news" | "newspaper" => "📰",
        "doc" | "document" | "file" => "📄",
        "shield" => "🛡️",
        "wrench" | "tool" | "tools" | "hammer" => "🔧",
        _ => return None,
    };
    Some(emoji.to_string())
}

impl Default for SpaceManager {
    fn default() -> Self {
        Self::new()
    }
}

// Out-of-box default Space theme: a translucent pink->orange gradient.
// Fractional hue (0..1): pink 0.92 (~331deg) primary, orange 0.07 (~25deg);
// opacity 0.8 keeps it translucent so the glass vibrancy shows through.
fn default_space_theme() -> SpaceTheme {
    SpaceTheme::Gradient {
        colors: vec![
            ThemeColor {
                hue: 0.92,
                saturation: 0.75,
                brightness: 0.95,
                is_custom: false,
                is_primary: true,
                c_raw: None,
                position: None,
                lightness: None,
                algorithm: None,
            },
            ThemeColor {
                hue: 0.07,
                saturation: 0.85,
                brightness: 1.0,
                is_custom: false,
                is_primary: false,
                c_raw: None,
                position: None,
                lightness: None,
                algorithm: None,
            },
        ],
        harmony: ColorHarmony::Analogous,
        scheme: None,
        opacity: 0.8,
        texture: 0.0,
        schema_version: 0,
    }
}

impl SpaceManager {
    pub fn new() -> Self {
        let mut mgr = Self {
            spaces: HashMap::new(),
            space_order: vec![],
            active_space_id: SpaceId::new(""),
            is_restoring: false,
        };
        let default_space = mgr.create_space_internal(
            "Personal",
            SpaceColor {
                hue: 220.0,
                saturation: 0.7,
                brightness: 0.9,
                grain: 0.0,
            },
            ProfileId::new(""),
        );
        let default_space_id = default_space.id.clone();
        if let Some(s) = mgr.spaces.get_mut(&default_space_id) {
            s.theme = Some(default_space_theme());
        }
        mgr.active_space_id = default_space_id;
        mgr
    }

    fn create_space_internal(
        &mut self,
        name: &str,
        color: SpaceColor,
        profile_id: ProfileId,
    ) -> Space {
        let now = DateTime::now();
        let space = Space {
            id: SpaceId::generate(),
            profile_id,
            name: name.to_string(),
            color,
            theme: None,
            icon: None,
            tab_order: vec![],
            folders: vec![],
            root_order: vec![],
            atc_rules: vec![],
            is_active: true,
            created_at: now,
            last_active_tab_id: None,
        };
        self.spaces.insert(space.id.clone(), space.clone());
        self.space_order.push(space.id.clone());
        space
    }

    pub fn create_space(&mut self, name: &str, color: SpaceColor, profile_id: ProfileId) -> Space {
        let space = self.create_space_internal(name, color, profile_id);
        if let Some(s) = self.spaces.get_mut(&space.id) {
            s.is_active = false;
        }
        space
    }

    pub fn delete_space(&mut self, space_id: &SpaceId) -> Option<SpaceId> {
        if self.spaces.len() <= 1 {
            return None;
        }
        let was_active = self.active_space_id == *space_id;
        self.spaces.remove(space_id);
        self.space_order.retain(|id| id != space_id);
        if was_active {
            if let Some(first_id) = self.space_order.first().cloned() {
                self.active_space_id = first_id.clone();
                for (id, space) in self.spaces.iter_mut() {
                    space.is_active = *id == first_id;
                }
                return Some(first_id);
            }
        }
        None
    }

    pub fn activate_space(&mut self, space_id: &SpaceId) {
        if !self.spaces.contains_key(space_id) {
            return;
        }
        if let Some(prev) = self.spaces.get_mut(&self.active_space_id) {
            prev.is_active = false;
        }
        self.active_space_id = space_id.clone();
        if let Some(space) = self.spaces.get_mut(space_id) {
            space.is_active = true;
        }
    }

    /// Record the currently visible tab for a space so it can be restored later.
    pub fn set_last_active_tab(&mut self, space_id: &SpaceId, tab_id: Option<TabId>) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            space.last_active_tab_id = tab_id;
        }
    }

    /// Get the last active tab for a space.
    pub fn get_last_active_tab(&self, space_id: &SpaceId) -> Option<&TabId> {
        self.spaces
            .get(space_id)
            .and_then(|s| s.last_active_tab_id.as_ref())
    }

    pub fn rename_space(&mut self, space_id: &SpaceId, name: &str) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            space.name = name.to_string();
        }
    }

    pub fn recolor_space(&mut self, space_id: &SpaceId, color: SpaceColor) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            space.color = color.clone();
        }
    }

    pub fn get_space(&self, space_id: &SpaceId) -> Option<&Space> {
        self.spaces.get(space_id)
    }

    pub fn get_all_spaces(&self) -> Vec<&Space> {
        self.space_order
            .iter()
            .filter_map(|id| self.spaces.get(id))
            .collect()
    }

    pub fn restore_spaces(&mut self, spaces: Vec<Space>, active_space_id: SpaceId) {
        self.is_restoring = true;
        self.spaces.clear();
        self.space_order.clear();
        for space in &spaces {
            self.space_order.push(space.id.clone());
        }
        for mut space in spaces {
            if space.root_order.is_empty() {
                space.root_order = Self::synthesize_root_order(&space);
            }
            // Migrates icon names persisted by older importers ("bulb").
            space.icon = space.icon.as_deref().and_then(normalize_space_icon);
            self.spaces.insert(space.id.clone(), space);
        }
        if self.spaces.contains_key(&active_space_id) {
            self.active_space_id = active_space_id.clone();
            for (id, space) in self.spaces.iter_mut() {
                space.is_active = *id == active_space_id;
            }
        } else if let Some(first_id) = self.space_order.first().cloned() {
            self.active_space_id = first_id.clone();
            if let Some(active_space) = self.spaces.get_mut(&first_id) {
                active_space.is_active = true;
            }
        }
        self.is_restoring = false;
    }
    pub fn upsert_space(&mut self, mut space: Space) {
        space.icon = space.icon.as_deref().and_then(normalize_space_icon);
        if space.root_order.is_empty() {
            space.root_order = Self::synthesize_root_order(&space);
        }
        let id = space.id.clone();
        if !self.space_order.contains(&id) {
            self.space_order.push(id.clone());
        }
        self.spaces.insert(id, space);
    }

    fn synthesize_root_order(space: &Space) -> Vec<RootItem> {
        let mut root_order = Vec::with_capacity(space.tab_order.len() + space.folders.len());
        let mut tab_ids_in_folders = HashSet::new();
        for folder in &space.folders {
            tab_ids_in_folders.extend(folder.tab_ids.iter().cloned());
        }

        for folder in &space.folders {
            if folder.parent_folder_id.is_none() {
                root_order.push(RootItem::Folder(folder.id.clone()));
            }
        }
        for tab_id in &space.tab_order {
            if !tab_ids_in_folders.contains(tab_id) {
                root_order.push(RootItem::Tab(tab_id.clone()));
            }
        }
        root_order
    }

    pub fn get_active_space(&self) -> &Space {
        self.spaces
            .get(&self.active_space_id)
            .expect("No active space")
    }

    pub fn get_active_space_id(&self) -> &SpaceId {
        &self.active_space_id
    }

    pub fn reorder_space(&mut self, space_id: &SpaceId, from: usize, to: usize) {
        if from >= self.space_order.len() || to >= self.space_order.len() {
            return;
        }
        if self.space_order.get(from) != Some(space_id) {
            return;
        }
        self.space_order.remove(from);
        self.space_order.insert(to, space_id.clone());
    }

    pub fn get_space_order(&self) -> &[SpaceId] {
        &self.space_order
    }

    pub fn add_tab_to_space(&mut self, space_id: &SpaceId, tab_id: TabId, position: Option<usize>) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            let tab_exists = space.tab_order.iter().any(|existing| existing == &tab_id);
            if !tab_exists {
                match position {
                    Some(pos) if pos <= space.tab_order.len() => {
                        space.tab_order.insert(pos, tab_id.clone());
                    }
                    _ => {
                        space.tab_order.push(tab_id.clone());
                    }
                }
            }

            let root_item = RootItem::Tab(tab_id);
            if !space.root_order.iter().any(|item| item == &root_item) {
                match position {
                    Some(pos) if pos <= space.root_order.len() => {
                        space.root_order.insert(pos, root_item);
                    }
                    _ => {
                        space.root_order.push(root_item);
                    }
                }
            }
        }
    }

    /// Moves `tab_id` to just after the last pinned root item. The sidebar
    /// splits `root_order` by pinned flag but keeps each section in root_order
    /// sequence, so this places the tab at the bottom of the pinned section.
    /// `pinned_tab_ids` carries tab pinned-state (owned by the tab manager);
    /// folder pinned-state is read locally. No-op if the tab is not in
    /// `root_order` (e.g. folder-resident tabs).
    pub fn move_root_tab_to_end_of_pinned(
        &mut self,
        space_id: &SpaceId,
        tab_id: &TabId,
        pinned_tab_ids: &HashSet<TabId>,
    ) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            let target = RootItem::Tab(tab_id.clone());
            if !space.root_order.iter().any(|item| item == &target) {
                return;
            }
            let pinned_folder_ids: HashSet<FolderId> = space
                .folders
                .iter()
                .filter(|folder| folder.is_pinned)
                .map(|folder| folder.id.clone())
                .collect();
            space.root_order.retain(|item| item != &target);
            let last_pinned = space.root_order.iter().rposition(|item| match item {
                RootItem::Tab(tid) => pinned_tab_ids.contains(tid),
                RootItem::Folder(fid) => pinned_folder_ids.contains(fid),
            });
            let insert_at = last_pinned.map(|pos| pos + 1).unwrap_or(0);
            space.root_order.insert(insert_at, target);
        }
    }

    pub fn remove_tab_from_space(&mut self, space_id: &SpaceId, tab_id: &TabId) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(idx) = space.tab_order.iter().position(|id| id == tab_id) {
                space.tab_order.remove(idx);
            }
            space
                .root_order
                .retain(|item| item != &RootItem::Tab(tab_id.clone()));
            for folder in &mut space.folders {
                folder.tab_ids.retain(|id| id != tab_id);
            }
        }
    }

    pub fn reorder_tab(
        &mut self,
        space_id: &SpaceId,
        tab_id: &TabId,
        before_tab_id: Option<&TabId>,
    ) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(current_pos) = space.tab_order.iter().position(|id| id == tab_id) {
                space.tab_order.remove(current_pos);
                let insert_at = match before_tab_id {
                    Some(before_id) => space
                        .tab_order
                        .iter()
                        .position(|id| id == before_id)
                        .unwrap_or(space.tab_order.len()),
                    None => space.tab_order.len(),
                };
                space.tab_order.insert(insert_at, tab_id.clone());
            }
        }
    }

    pub fn move_tab_to_root(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        tab_id: &TabId,
        before_tab_id: Option<&TabId>,
    ) {
        self.remove_tab_from_folder(space_id, folder_id, tab_id);
        self.reorder_tab(space_id, tab_id, before_tab_id);

        let insertion_point = before_tab_id
            .map(|target| RootInsertionPoint::Before {
                target: RootItem::Tab(target.clone()),
            })
            .unwrap_or(RootInsertionPoint::Append);
        self.reorder_root_item(space_id, &RootItem::Tab(tab_id.clone()), &insertion_point);
    }

    pub fn reorder_folder(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        parent_folder_id: Option<&FolderId>,
        before_folder_id: Option<&FolderId>,
    ) -> Result<(), FolderError> {
        if let Some(pid) = parent_folder_id {
            if !self
                .spaces
                .get(space_id)
                .map(|s| s.folders.iter().any(|f| f.id == *pid))
                .unwrap_or(false)
            {
                return Err(FolderError::FolderNotFound);
            }
            if pid == folder_id || self.is_descendant_of(space_id, pid, folder_id) {
                return Err(FolderError::CycleDetected);
            }
        }
        let subtree_height = self.get_subtree_height(space_id, folder_id);
        let parent_depth = self.get_max_depth_under_parent(space_id, parent_folder_id);
        if parent_depth + subtree_height > MAX_FOLDER_DEPTH {
            return Err(FolderError::DepthLimitExceeded);
        }
        {
            let space = match self.spaces.get_mut(space_id) {
                Some(s) => s,
                None => return Err(FolderError::SpaceNotFound),
            };

            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                folder.parent_folder_id = parent_folder_id.cloned();
            } else {
                return Err(FolderError::FolderNotFound);
            }
            if parent_folder_id.is_some() {
                space
                    .root_order
                    .retain(|item| item != &RootItem::Folder(folder_id.clone()));
            }

            let folder_pos = match space.folders.iter().position(|f| f.id == *folder_id) {
                Some(p) => p,
                None => return Err(FolderError::FolderNotFound),
            };
            let folder = space.folders.remove(folder_pos);

            let insert_at = match before_folder_id {
                Some(before_id) => space
                    .folders
                    .iter()
                    .position(|f| f.id == *before_id)
                    .unwrap_or(space.folders.len()),
                None => {
                    let last_sibling = space
                        .folders
                        .iter()
                        .rposition(|f| f.parent_folder_id.as_ref() == parent_folder_id);
                    match last_sibling {
                        Some(pos) => pos + 1,
                        None => space.folders.len(),
                    }
                }
            };

            space.folders.insert(insert_at, folder);
        }

        if parent_folder_id.is_none() {
            let insertion_point = before_folder_id
                .map(|target| RootInsertionPoint::Before {
                    target: RootItem::Folder(target.clone()),
                })
                .unwrap_or(RootInsertionPoint::Append);
            self.reorder_root_item(
                space_id,
                &RootItem::Folder(folder_id.clone()),
                &insertion_point,
            );
        }
        Ok(())
    }

    pub fn move_folder_to_root(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        before_folder_id: Option<&FolderId>,
    ) {
        let space = match self.spaces.get_mut(space_id) {
            Some(s) => s,
            None => return,
        };

        if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
            folder.parent_folder_id = None;
        }

        let folder_pos = match space.folders.iter().position(|f| f.id == *folder_id) {
            Some(p) => p,
            None => return,
        };
        let folder = space.folders.remove(folder_pos);

        let insert_at = match before_folder_id {
            Some(before_id) => space
                .folders
                .iter()
                .position(|f| f.id == *before_id)
                .unwrap_or(space.folders.len()),
            None => {
                let last_root = space
                    .folders
                    .iter()
                    .rposition(|f| f.parent_folder_id.is_none());
                match last_root {
                    Some(pos) => pos + 1,
                    None => space.folders.len(),
                }
            }
        };

        space.folders.insert(insert_at, folder);

        let insertion_point = before_folder_id
            .map(|target| RootInsertionPoint::Before {
                target: RootItem::Folder(target.clone()),
            })
            .unwrap_or(RootInsertionPoint::Append);
        self.reorder_root_item(
            space_id,
            &RootItem::Folder(folder_id.clone()),
            &insertion_point,
        );
    }

    pub fn reorder_root_item(
        &mut self,
        space_id: &SpaceId,
        item: &RootItem,
        insertion_point: &RootInsertionPoint,
    ) {
        let space = match self.spaces.get_mut(space_id) {
            Some(s) => s,
            None => return,
        };

        space.root_order.retain(|i| i != item);

        let insert_at = match insertion_point {
            RootInsertionPoint::Append => space.root_order.len(),
            RootInsertionPoint::Before { target } => space
                .root_order
                .iter()
                .position(|i| i == target)
                .unwrap_or(space.root_order.len()),
        };

        space.root_order.insert(insert_at, item.clone());
    }

    pub fn get_root_order(&self, space_id: &SpaceId) -> Vec<RootItem> {
        self.spaces
            .get(space_id)
            .map(|s| s.root_order.clone())
            .unwrap_or_default()
    }

    pub fn remove_tab_from_root_order(&mut self, space_id: &SpaceId, tab_id: &TabId) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            space
                .root_order
                .retain(|item| item != &RootItem::Tab(tab_id.clone()));
        }
    }

    pub fn add_tab_to_root_order_if_absent(&mut self, space_id: &SpaceId, tab_id: &TabId) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            let item = RootItem::Tab(tab_id.clone());
            if !space.root_order.iter().any(|i| i == &item) {
                space.root_order.push(item);
            }
        }
    }

    pub fn strip_favorited_tabs_from_root_orders(&mut self, favorited_tab_ids: &HashSet<TabId>) {
        for space in self.spaces.values_mut() {
            space.root_order.retain(|item| match item {
                RootItem::Tab(tid) => !favorited_tab_ids.contains(tid),
                RootItem::Folder(_) => true,
            });
        }
    }

    /// Centralized residency helper: enforces "favorites XOR root_order" for a single tab.
    ///
    /// Rules:
    /// - `is_favorite = true`  → remove from root_order (favorites live in their own bucket). // L3-EXEMPT
    /// - `is_favorite = false, is_in_folder = false` → ensure present in root_order once. // L3-EXEMPT
    /// - `is_favorite = false, is_in_folder = true`  → remove from root_order (folder owns it). // L3-EXEMPT
    ///
    /// This is the single authoritative seam for bucket placement.  All event paths that
    /// change a tab's favorite status or move it across spaces must call this after the move. // L3-EXEMPT
    pub fn apply_tab_residency(
        &mut self,
        space_id: &SpaceId,
        tab_id: &TabId,
        is_favorite: bool, // L3-EXEMPT
        is_in_folder: bool,
    ) {
        if is_favorite || is_in_folder {
            // L3-EXEMPT
            // Must not appear in root_order.
            self.remove_tab_from_root_order(space_id, tab_id);
        } else {
            // Non-favorite, root-level tab: ensure it is present exactly once.
            self.add_tab_to_root_order_if_absent(space_id, tab_id);
        }
    }

    pub fn is_tab_in_folder(&self, space_id: &SpaceId, tab_id: &TabId) -> bool {
        self.spaces
            .get(space_id)
            .map(|s| s.folders.iter().any(|f| f.tab_ids.contains(tab_id)))
            .unwrap_or(false)
    }

    pub fn to_view_model(&self, space: &Space) -> SpaceViewModel {
        SpaceViewModel {
            id: space.id.clone(),
            name: space.name.clone(),
            color: space.color.clone(),
            theme: space.theme.clone(),
            tab_count: space.tab_order.len(),
            is_active: space.is_active,
            icon: space.icon.clone(),
            order_index: self.space_order.iter().position(|id| id == &space.id),
            profile_id: Some(space.profile_id.clone()),
            profile_name: None,
        }
    }

    pub fn create_folder(
        &mut self,
        space_id: &SpaceId,
        name: &str,
        is_pinned: bool,
        parent_folder_id: Option<FolderId>,
    ) -> Option<FolderViewModel> {
        self.create_folder_with_provider(space_id, name, is_pinned, parent_folder_id, None, None)
    }

    pub fn create_folder_with_provider(
        &mut self,
        space_id: &SpaceId,
        name: &str,
        is_pinned: bool,
        parent_folder_id: Option<FolderId>,
        provider_type: Option<String>,
        config_json: Option<String>,
    ) -> Option<FolderViewModel> {
        if let Some(ref parent_id) = parent_folder_id {
            let parent_depth = self.get_max_depth_under_parent(space_id, Some(parent_id));
            if parent_depth + 1 > MAX_FOLDER_DEPTH {
                return None;
            }
        }
        let space = self.spaces.get_mut(space_id)?;
        if let Some(ref parent_id) = parent_folder_id {
            if !space.folders.iter().any(|f| f.id == *parent_id) {
                return None;
            }
        }
        let folder = Folder {
            id: FolderId::generate(),
            name: name.to_string(),
            tab_ids: vec![],
            is_expanded: true,
            is_pinned,
            parent_folder_id: parent_folder_id.clone(),
            provider_type: provider_type.clone(),
            config_json: config_json.clone(),
        };
        let vm = FolderViewModel {
            id: folder.id.clone(),
            name: folder.name.clone(),
            tab_count: folder.tab_ids.len(),
            is_expanded: folder.is_expanded,
            is_pinned: folder.is_pinned,
            tab_ids: folder.tab_ids.clone(),
            parent_folder_id: folder.parent_folder_id.clone(),
            provider_type: provider_type,
            config_json: config_json,
        };
        if parent_folder_id.is_none() {
            if !space
                .root_order
                .iter()
                .any(|item| item == &RootItem::Folder(folder.id.clone()))
            {
                space.root_order.push(RootItem::Folder(folder.id.clone()));
            }
        }
        space.folders.push(folder);
        Some(vm)
    }

    pub fn rename_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId, name: &str) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                folder.name = name.to_string();
            }
        }
    }

    /// Deletes the folder itself, never its contents: its tabs and subfolders
    /// are promoted into the folder's parent (or into root_order at the
    /// folder's position). The sidebar tree is built only from root_order and
    /// folder membership, so contents left behind would stay open yet be
    /// unreachable from the sidebar.
    pub fn delete_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId) {
        let Some(space) = self.spaces.get_mut(space_id) else {
            return;
        };
        let Some(pos) = space.folders.iter().position(|f| f.id == *folder_id) else {
            return;
        };
        let folder = space.folders.remove(pos);

        let mut promoted_folders = Vec::new();
        for child in space.folders.iter_mut() {
            if child.parent_folder_id.as_ref() == Some(folder_id) {
                child.parent_folder_id = folder.parent_folder_id.clone();
                promoted_folders.push(child.id.clone());
            }
        }

        if let Some(ref parent_id) = folder.parent_folder_id {
            if let Some(parent) = space.folders.iter_mut().find(|f| f.id == *parent_id) {
                for tab_id in folder.tab_ids {
                    if !parent.tab_ids.contains(&tab_id) {
                        parent.tab_ids.push(tab_id);
                    }
                }
            }
            return;
        }

        let folder_item = RootItem::Folder(folder_id.clone());
        let insert_at = space
            .root_order
            .iter()
            .position(|item| item == &folder_item)
            .unwrap_or(space.root_order.len());
        space.root_order.retain(|item| item != &folder_item);
        let promoted: Vec<RootItem> = folder
            .tab_ids
            .into_iter()
            .map(RootItem::Tab)
            .chain(promoted_folders.into_iter().map(RootItem::Folder))
            .filter(|item| !space.root_order.contains(item))
            .collect();
        let insert_at = insert_at.min(space.root_order.len());
        space.root_order.splice(insert_at..insert_at, promoted);
    }

    pub fn add_tab_to_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId, tab_id: TabId) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            // Ensure tab belongs to at most one folder within a space.
            for folder in space.folders.iter_mut() {
                if folder.id != *folder_id {
                    folder.tab_ids.retain(|t| t != &tab_id);
                }
            }
            space
                .root_order
                .retain(|item| item != &RootItem::Tab(tab_id.clone()));
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                if !folder.tab_ids.contains(&tab_id) {
                    folder.tab_ids.push(tab_id);
                }
            }
        }
    }

    pub fn reorder_tab_in_folder(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        tab_id: &TabId,
        to: usize,
    ) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                if let Some(from) = folder.tab_ids.iter().position(|t| t == tab_id) {
                    let tab = folder.tab_ids.remove(from);
                    let insert_at = to.min(folder.tab_ids.len());
                    folder.tab_ids.insert(insert_at, tab);
                }
            }
        }
    }

    pub fn remove_tab_from_folder(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        tab_id: &TabId,
    ) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                folder.tab_ids.retain(|id| id != tab_id);
            }
        }
    }

    pub fn toggle_folder_expanded(&mut self, space_id: &SpaceId, folder_id: &FolderId) -> bool {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                folder.is_expanded = !folder.is_expanded;
                return true;
            }
        }
        false
    }

    /// Find a folder by ID across all spaces. Returns its current `is_expanded`
    /// value, or None if not found anywhere.
    pub fn find_folder_expanded(&self, space_id: &SpaceId, folder_id: &FolderId) -> Option<bool> {
        if let Some(space) = self.spaces.get(space_id) {
            if let Some(folder) = space.folders.iter().find(|f| f.id == *folder_id) {
                return Some(folder.is_expanded);
            }
        }
        None
    }

    pub fn set_folder_pinned(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        is_pinned: bool,
    ) -> bool {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                folder.is_pinned = is_pinned;
                return true;
            }
        }
        false
    }

    pub fn get_folder_view_models(&self, space_id: &SpaceId) -> Vec<FolderViewModel> {
        self.spaces
            .get(space_id)
            .map(|space| {
                space
                    .folders
                    .iter()
                    .map(|f| self.folder_to_view_model(f))
                    .collect()
            })
            .unwrap_or_default()
    }

    pub fn move_folder(
        &mut self,
        folder_id: &FolderId,
        new_parent: Option<FolderId>,
    ) -> Result<(), FolderError> {
        let target_space_id = self.spaces.iter().find_map(|(space_id, space)| {
            if space.folders.iter().any(|f| f.id == *folder_id) {
                Some(space_id.clone())
            } else {
                None
            }
        });
        if let Some(space_id) = target_space_id {
            if let Some(ref pid) = new_parent {
                if !self
                    .spaces
                    .get(&space_id)
                    .map(|s| s.folders.iter().any(|f| f.id == *pid))
                    .unwrap_or(false)
                {
                    return Err(FolderError::FolderNotFound);
                }
                if pid == folder_id || self.is_descendant_of(&space_id, pid, folder_id) {
                    return Err(FolderError::CycleDetected);
                }
            }
            let subtree_height = self.get_subtree_height(&space_id, folder_id);
            let parent_depth = self.get_max_depth_under_parent(&space_id, new_parent.as_ref());
            if parent_depth + subtree_height > MAX_FOLDER_DEPTH {
                return Err(FolderError::DepthLimitExceeded);
            }
            if let Some(space) = self.spaces.get_mut(&space_id) {
                if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                    folder.parent_folder_id = new_parent.clone();
                    if new_parent.is_some() {
                        space
                            .root_order
                            .retain(|item| item != &RootItem::Folder(folder_id.clone()));
                    }
                    Ok(())
                } else {
                    Err(FolderError::FolderNotFound)
                }
            } else {
                Err(FolderError::SpaceNotFound)
            }
        } else {
            Err(FolderError::FolderNotFound)
        }
    }

    pub fn move_folder_in_space(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        new_parent: Option<FolderId>,
    ) -> Result<(), FolderError> {
        if let Some(ref pid) = new_parent {
            if !self
                .spaces
                .get(space_id)
                .map(|s| s.folders.iter().any(|f| f.id == *pid))
                .unwrap_or(false)
            {
                return Err(FolderError::FolderNotFound);
            }
            if pid == folder_id || self.is_descendant_of(space_id, pid, folder_id) {
                return Err(FolderError::CycleDetected);
            }
        }
        let subtree_height = self.get_subtree_height(space_id, folder_id);
        let parent_depth = self.get_max_depth_under_parent(space_id, new_parent.as_ref());
        if parent_depth + subtree_height > MAX_FOLDER_DEPTH {
            return Err(FolderError::DepthLimitExceeded);
        }
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                folder.parent_folder_id = new_parent.clone();
                if new_parent.is_some() {
                    space
                        .root_order
                        .retain(|item| item != &RootItem::Folder(folder_id.clone()));
                }
                Ok(())
            } else {
                Err(FolderError::FolderNotFound)
            }
        } else {
            Err(FolderError::SpaceNotFound)
        }
    }

    pub fn move_folder_to_space(
        &mut self,
        folder_id: &FolderId,
        source_space_id: &SpaceId,
        target_space_id: &SpaceId,
    ) {
        let folder_opt = if let Some(source_space) = self.spaces.get_mut(source_space_id) {
            if let Some(pos) = source_space.folders.iter().position(|f| f.id == *folder_id) {
                let folder = source_space.folders.remove(pos);
                source_space
                    .root_order
                    .retain(|item| item != &RootItem::Folder(folder_id.clone()));
                Some(folder)
            } else {
                None
            }
        } else {
            None
        };

        if let Some(folder) = folder_opt {
            if let Some(target_space) = self.spaces.get_mut(target_space_id) {
                if folder.parent_folder_id.is_none() {
                    if !target_space
                        .root_order
                        .contains(&RootItem::Folder(folder.id.clone()))
                    {
                        target_space
                            .root_order
                            .push(RootItem::Folder(folder.id.clone()));
                    }
                }
                target_space.folders.push(folder);
            }
        }
    }

    pub fn get_subfolders(&self, space_id: &SpaceId, parent_id: Option<&FolderId>) -> Vec<&Folder> {
        self.spaces
            .get(space_id)
            .map(|space| {
                space
                    .folders
                    .iter()
                    .filter(|f| f.parent_folder_id.as_ref() == parent_id)
                    .collect()
            })
            .unwrap_or_default()
    }

    pub fn is_descendant_of(
        &self,
        space_id: &SpaceId,
        descendant: &FolderId,
        ancestor: &FolderId,
    ) -> bool {
        if descendant == ancestor {
            return true;
        }
        let space = match self.spaces.get(space_id) {
            Some(s) => s,
            None => return false,
        };
        let mut visited = HashSet::new();
        let mut current = descendant.clone();
        visited.insert(current.clone());
        while let Some(folder) = space.folders.iter().find(|f| f.id == current) {
            if let Some(ref parent_id) = folder.parent_folder_id {
                if parent_id == ancestor {
                    return true;
                }
                if visited.contains(parent_id) {
                    break;
                }
                visited.insert(parent_id.clone());
                current = parent_id.clone();
            } else {
                break;
            }
        }
        false
    }

    pub fn get_max_depth_under_parent(
        &self,
        space_id: &SpaceId,
        parent_id: Option<&FolderId>,
    ) -> usize {
        match parent_id {
            None => 0,
            Some(pid) => {
                let mut depth = 0;
                let mut current = Some(pid.clone());
                let mut visited = HashSet::new();
                if let Some(space) = self.spaces.get(space_id) {
                    while let Some(id) = current {
                        if visited.contains(&id) {
                            break; // Cycle guard
                        }
                        visited.insert(id.clone());
                        if let Some(folder) = space.folders.iter().find(|f| f.id == id) {
                            depth += 1;
                            current = folder.parent_folder_id.clone();
                        } else {
                            break;
                        }
                    }
                }
                depth
            }
        }
    }

    pub fn get_subtree_height(&self, space_id: &SpaceId, folder_id: &FolderId) -> usize {
        let mut visited = HashSet::new();
        self.get_subtree_height_recursive(space_id, folder_id, &mut visited)
    }

    fn get_subtree_height_recursive(
        &self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        visited: &mut HashSet<FolderId>,
    ) -> usize {
        if visited.contains(folder_id) {
            return 0; // Cycle guard
        }
        visited.insert(folder_id.clone());
        let mut max_child_height = 0;
        if let Some(space) = self.spaces.get(space_id) {
            for folder in &space.folders {
                if folder.parent_folder_id.as_ref() == Some(folder_id) {
                    let height = self.get_subtree_height_recursive(space_id, &folder.id, visited);
                    if height > max_child_height {
                        max_child_height = height;
                    }
                }
            }
        }
        visited.remove(folder_id);
        max_child_height + 1
    }

    fn folder_to_view_model(&self, folder: &Folder) -> FolderViewModel {
        FolderViewModel {
            id: folder.id.clone(),
            name: folder.name.clone(),
            tab_count: folder.tab_ids.len(),
            is_expanded: folder.is_expanded,
            is_pinned: folder.is_pinned,
            tab_ids: folder.tab_ids.clone(),
            parent_folder_id: folder.parent_folder_id.clone(),
            provider_type: folder.provider_type.clone(),
            config_json: folder.config_json.clone(),
        }
    }

    pub fn get_sidebar_tree<F>(&self, space_id: &SpaceId, get_tab_vm: F) -> Vec<SidebarNode>
    where
        F: Fn(&TabId) -> Option<TabViewModel> + Copy,
    {
        let root_order = self.get_root_order(space_id);
        let mut tree = Vec::new();
        for item in root_order {
            if let Some(node) = self.build_sidebar_node_recursive(space_id, &item, get_tab_vm) {
                tree.push(node);
            }
        }
        tree
    }

    fn build_sidebar_node_recursive<F>(
        &self,
        space_id: &SpaceId,
        item: &RootItem,
        get_tab_vm: F,
    ) -> Option<SidebarNode>
    where
        F: Fn(&TabId) -> Option<TabViewModel> + Copy,
    {
        match item {
            RootItem::Tab(tab_id) => {
                let tab_vm = get_tab_vm(tab_id)?;
                Some(SidebarNode::Tab { tab: tab_vm })
            }
            RootItem::Folder(folder_id) => {
                let space = self.get_space(space_id)?;

                let folder = space.folders.iter().find(|f| f.id == *folder_id)?;

                let mut children = Vec::new();
                for child_tab_id in &folder.tab_ids {
                    if let Some(tab_vm) = get_tab_vm(child_tab_id) {
                        children.push(SidebarNode::Tab { tab: tab_vm });
                    }
                }

                for child_folder in &space.folders {
                    if child_folder.parent_folder_id.as_ref() == Some(folder_id) {
                        if let Some(child_node) = self.build_sidebar_node_recursive(
                            space_id,
                            &RootItem::Folder(child_folder.id.clone()),
                            get_tab_vm,
                        ) {
                            children.push(child_node);
                        }
                    }
                }

                Some(SidebarNode::Folder {
                    id: folder.id.clone(),
                    name: folder.name.clone(),
                    is_expanded: folder.is_expanded,
                    is_pinned: folder.is_pinned,
                    parent_folder_id: folder.parent_folder_id.clone(),
                    provider_type: folder.provider_type.clone(),
                    config_json: folder.config_json.clone(),
                    children,
                })
            }
        }
    }

    pub fn update_space_config(
        &mut self,
        changes: SpaceConfigUpdate,
    ) -> Result<Option<SpaceConfigUpdate>, SpaceUpdateError> {
        if let Some(ref name) = changes.name {
            let normalized = name.trim();
            if normalized.is_empty() {
                return Err(SpaceUpdateError::EmptyName);
            }
            if self
                .spaces
                .values()
                .any(|space| space.id != changes.space_id && space.name == normalized)
            {
                return Err(SpaceUpdateError::DuplicateName);
            }
        }

        if let Some(space) = self.spaces.get_mut(&changes.space_id) {
            let mut applied = SpaceConfigUpdate {
                space_id: changes.space_id.clone(),
                name: None,
                color: None,
                theme: None,
                icon: None,
                profile_id: None,
            };
            if let Some(ref n) = changes.name {
                let normalized = n.trim().to_string();
                space.name = normalized.clone();
                applied.name = Some(normalized);
            }
            if let Some(ref theme) = changes.theme {
                space.theme = Some(theme.clone());
                space.color = theme.base_color();
                applied.theme = Some(theme.clone());
                if changes.color.is_none() {
                    applied.color = Some(space.color.clone());
                }
            }
            if let Some(ref c) = changes.color {
                space.color = c.clone();
                applied.color = Some(c.clone());
            }
            if let Some(ref i) = changes.icon {
                let normalized = i.as_deref().and_then(normalize_space_icon);
                space.icon = normalized.clone();
                applied.icon = Some(normalized);
            }
            if let Some(ref profile_id) = changes.profile_id {
                space.profile_id = profile_id.clone();
                applied.profile_id = Some(profile_id.clone());
            }
            Ok(Some(applied))
        } else {
            Ok(None)
        }
    }

    pub fn get_space_configs(&self) -> Vec<(SpaceId, String, SpaceColor)> {
        self.space_order
            .iter()
            .filter_map(|id| {
                self.spaces
                    .get(id)
                    .map(|s| (s.id.clone(), s.name.clone(), s.color.clone()))
            })
            .collect()
    }

    pub fn get_space_ids_for_profile(
        &self,
        profile_id: &ProfileId,
    ) -> std::collections::HashSet<SpaceId> {
        self.spaces
            .values()
            .filter(|s| s.profile_id == *profile_id)
            .map(|s| s.id.clone())
            .collect()
    }

    pub fn get_tab_order(&self, space_id: &SpaceId) -> Vec<TabId> {
        self.spaces
            .get(space_id)
            .map(|s| s.tab_order.clone())
            .unwrap_or_default()
    }

    pub fn get_all_atc_rules(&self) -> Vec<(SpaceId, Vec<maho_types::space::ATCRule>)> {
        self.space_order
            .iter()
            .filter_map(|id| {
                self.spaces
                    .get(id)
                    .map(|s| (s.id.clone(), s.atc_rules.clone()))
            })
            .collect()
    }

    /// Remove all folders in the given space whose `tab_ids` is empty.
    /// Returns the list of removed folder IDs so callers can emit updates.
    pub fn remove_empty_folders_in_space(&mut self, space_id: &SpaceId) -> Vec<FolderId> {
        let space = match self.spaces.get(space_id) {
            Some(s) => s,
            None => return vec![],
        };

        let empty_ids: Vec<FolderId> = space
            .folders
            .iter()
            .filter(|f| f.tab_ids.is_empty())
            .map(|f| f.id.clone())
            .collect();

        for folder_id in &empty_ids {
            self.delete_folder(space_id, folder_id);
        }

        empty_ids
    }
}

impl SpaceManager {
    /// Enforces correct tab bucket placement for all tabs in the provided map.
    /// `is_favorite_fn` receives a tab_id and returns whether it is currently favorited. // L3-EXEMPT
    /// For each (tab_id → space_id) pair, calls apply_tab_residency with the real status.
    pub fn enforce_cross_space_tab_residency(
        &mut self,
        tab_owners: &std::collections::HashMap<
            maho_types::identifiers::TabId,
            maho_types::identifiers::SpaceId,
        >,
        is_favorite_fn: &dyn Fn(&maho_types::identifiers::TabId) -> bool, // L3-EXEMPT
    ) {
        for (tab_id, space_id) in tab_owners {
            let is_favorite = is_favorite_fn(tab_id); // L3-EXEMPT
            let is_in_folder = self.is_tab_in_folder(space_id, tab_id);
            self.apply_tab_residency(space_id, tab_id, is_favorite, is_in_folder);
            // L3-EXEMPT
        }
    }
}
