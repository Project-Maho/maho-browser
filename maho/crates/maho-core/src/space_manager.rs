use std::collections::{HashMap, HashSet};

use maho_types::common::DateTime;
use maho_types::folder::Folder;
use maho_types::identifiers::{FolderId, ProfileId, SpaceId, TabId};
use maho_types::space::{Space, SpaceColor, SpaceConfigUpdate};
use maho_types::space::{RootInsertionPoint, RootItem};
use maho_types::traits::shell_renderer::{FolderViewModel, SpaceViewModel};

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum SpaceUpdateError {
    EmptyName,
    DuplicateName,
}

#[cfg(test)]
#[allow(clippy::items_after_test_module)]
mod tests {
    use super::*;
    use maho_types::folder::Folder;
    use maho_types::space::{
        ColorHarmony, RootInsertionPoint, RootItem, SpaceConfigUpdate, SpaceTheme, ThemeColor,
    };

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
            color: SpaceColor { hue: 1.0, saturation: 0.5, brightness: 0.5, grain: 0.0 },
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
                },
                Folder {
                    id: child_folder_id,
                    name: "Nested Folder".to_string(),
                    tab_ids: vec![],
                    is_expanded: true,
                    is_pinned: false,
                    parent_folder_id: Some(folder_id.clone()),
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
            vec![RootItem::Tab(tab_id), RootItem::Folder(folder_id)]
        );
    }

    #[test]
    fn restore_spaces_preserves_explicit_root_order() {
        let mut manager = SpaceManager::new();
        let space_id = SpaceId::generate();
        let tab_id = TabId::generate();
        let folder_id = FolderId::generate();
        let explicit_order = vec![RootItem::Folder(folder_id.clone()), RootItem::Tab(tab_id.clone())];

        let restored = Space {
            id: space_id.clone(),
            profile_id: ProfileId::new(""),
            name: "Modern".to_string(),
            color: SpaceColor { hue: 2.0, saturation: 0.6, brightness: 0.6, grain: 0.0 },
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

        let folder = manager.create_folder(&space_id, "F", false).unwrap();
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

        let folder = manager.create_folder(&space_id, "F", false).unwrap();
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
            });
            space.tab_order = vec![before_tab.clone(), moved_tab.clone()];
        }

        manager.move_tab_to_root(&space_id, &folder_id, &moved_tab, Some(&before_tab));

        let space = manager.spaces.get(&space_id).unwrap();
        assert!(space.folders.iter().all(|folder| folder.id != folder_id));
        assert_eq!(space.tab_order, vec![moved_tab, before_tab]);
    }

    #[test]
    fn create_folder_returns_view_model_with_correct_fields() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();

        let vm = manager.create_folder(&space_id, "Work", false).unwrap();

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

        let vm = manager.create_folder(&space_id, "Work", false).unwrap();

        assert_eq!(manager.get_root_order(&space_id), vec![RootItem::Folder(vm.id)]);
    }

    #[test]
    fn delete_folder_removes_root_order_entry() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();

        let vm = manager.create_folder(&space_id, "Work", false).unwrap();
        manager.delete_folder(&space_id, &vm.id);

        assert!(manager.get_root_order(&space_id).is_empty());
    }

    #[test]
    fn create_folder_pinned() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();

        let vm = manager.create_folder(&space_id, "Pinned Folder", true).unwrap();

        assert!(vm.is_pinned);
        let space = manager.spaces.get(&space_id).unwrap();
        let folder = space.folders.iter().find(|f| f.id == vm.id).unwrap();
        assert!(folder.is_pinned);
    }

    #[test]
    fn add_tab_to_folder_adds_tab() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        let vm = manager.create_folder(&space_id, "F", false).unwrap();
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

        let folder_a = manager.create_folder(&space_id, "A", false).unwrap();
        let folder_b = manager.create_folder(&space_id, "B", false).unwrap();

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

        let vm = manager.create_folder(&space_id, "F", false).unwrap();
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

        let vm = manager.create_folder(&space_id, "F", false).unwrap();
        manager.add_tab_to_folder(&space_id, &vm.id, t1.clone());
        manager.add_tab_to_folder(&space_id, &vm.id, t2.clone());
        manager.add_tab_to_folder(&space_id, &vm.id, t3.clone());

        manager.reorder_tab_in_folder(&space_id, &vm.id, &t1, 2);

        let space = manager.spaces.get(&space_id).unwrap();
        let folder = space.folders.iter().find(|f| f.id == vm.id).unwrap();
        assert_eq!(folder.tab_ids, vec![t2, t3, t1]);
    }

    #[test]
    fn pin_folder_sets_pinned_flag() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();

        let vm = manager.create_folder(&space_id, "F", false).unwrap();
        assert!(!vm.is_pinned);

        manager.pin_folder(&space_id, &vm.id);

        let space = manager.spaces.get(&space_id).unwrap();
        let folder = space.folders.iter().find(|f| f.id == vm.id).unwrap();
        assert!(folder.is_pinned);
    }

    #[test]
    fn unpin_folder_clears_pinned_flag() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();

        let vm = manager.create_folder(&space_id, "F", true).unwrap();
        manager.unpin_folder(&space_id, &vm.id);

        let space = manager.spaces.get(&space_id).unwrap();
        let folder = space.folders.iter().find(|f| f.id == vm.id).unwrap();
        assert!(!folder.is_pinned);
    }

    #[test]
    fn reorder_folder_root_before_sibling() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();

        let f1 = manager.create_folder(&space_id, "F1", false).unwrap();
        let f2 = manager.create_folder(&space_id, "F2", false).unwrap();
        let f3 = manager.create_folder(&space_id, "F3", false).unwrap();

        manager.reorder_folder(&space_id, &f3.id, None, Some(&f1.id));

        let space = manager.spaces.get(&space_id).unwrap();
        let ids: Vec<_> = space.folders.iter().map(|f| f.id.clone()).collect();
        assert_eq!(ids, vec![f3.id, f1.id, f2.id]);
    }

    #[test]
    fn reorder_folder_nested_same_parent() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();

        let parent = manager.create_folder(&space_id, "Parent", false).unwrap();
        let child1 = manager.create_folder(&space_id, "C1", false).unwrap();
        let child2 = manager.create_folder(&space_id, "C2", false).unwrap();

        if let Some(space) = manager.spaces.get_mut(&space_id) {
            for f in space.folders.iter_mut() {
                if f.id == child1.id || f.id == child2.id {
                    f.parent_folder_id = Some(parent.id.clone());
                }
            }
        }

        manager.reorder_folder(&space_id, &child2.id, Some(&parent.id), Some(&child1.id));

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

        let parent = manager.create_folder(&space_id, "Parent", false).unwrap();
        let child = manager.create_folder(&space_id, "Child", false).unwrap();

        manager.reorder_folder(&space_id, &child.id, Some(&parent.id), None);

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
            &RootInsertionPoint::Before { target: folder_item.clone() },
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
            &RootInsertionPoint::Before { target: tab_item.clone() },
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

        manager.reorder_root_item(
            &space_id,
            &new_tab_item,
            &RootInsertionPoint::Append,
        );

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
        };
        let theme = SpaceTheme::Gradient {
            colors: vec![primary_color],
            harmony: ColorHarmony::Analogous,
            scheme: None,
            opacity: 0.5,
            texture: 0.0,
            schema_version: 0,
        };
        let override_color = SpaceColor { hue: 0.1, saturation: 0.2, brightness: 0.3, grain: 0.0 };
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
        assert!(manager.get_root_order(&space_id).contains(&RootItem::Tab(tab_id.clone())));

        manager.remove_tab_from_root_order(&space_id, &tab_id);

        assert!(!manager.get_root_order(&space_id).contains(&RootItem::Tab(tab_id)));
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

        assert_eq!(manager.get_root_order(&space_id), vec![RootItem::Tab(tab_id)]);
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
        assert_eq!(order.iter().filter(|i| *i == &RootItem::Tab(tab_id.clone())).count(), 1);
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
        let folder_vm = manager.create_folder(&space_id, "F", false).unwrap();

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
            color: SpaceColor { hue: 1.0, saturation: 0.5, brightness: 0.5, grain: 0.0 },
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
        assert!(!order.contains(&RootItem::Tab(tab_a)), "favorited tab must be absent");
        assert!(order.contains(&RootItem::Tab(tab_b)), "non-favorited tab must remain");
    }

    #[test]
    fn is_tab_in_folder_returns_true_when_tab_in_folder() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        let folder = manager.create_folder(&space_id, "F", false).unwrap();
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
        assert!(manager.get_root_order(&space_id).contains(&RootItem::Tab(tab_id.clone())));

        manager.apply_tab_residency(&space_id, &tab_id, true, false);

        assert!(!manager.get_root_order(&space_id).contains(&RootItem::Tab(tab_id)));
    }

    #[test]
    fn apply_tab_residency_non_favorite_root_adds_to_root_order() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        manager.remove_tab_from_root_order(&space_id, &tab_id);
        assert!(!manager.get_root_order(&space_id).contains(&RootItem::Tab(tab_id.clone())));

        manager.apply_tab_residency(&space_id, &tab_id, false, false);

        assert!(manager.get_root_order(&space_id).contains(&RootItem::Tab(tab_id)));
    }

    #[test]
    fn apply_tab_residency_folder_resident_removes_from_root_order() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        assert!(manager.get_root_order(&space_id).contains(&RootItem::Tab(tab_id.clone())));

        manager.apply_tab_residency(&space_id, &tab_id, false, true);

        assert!(!manager.get_root_order(&space_id).contains(&RootItem::Tab(tab_id)));
    }

    #[test]
    fn apply_tab_residency_non_favorite_root_is_idempotent() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        manager.apply_tab_residency(&space_id, &tab_id, false, false);
        manager.apply_tab_residency(&space_id, &tab_id, false, false);

        let count = manager.get_root_order(&space_id)
            .iter()
            .filter(|i| *i == &RootItem::Tab(tab_id.clone()))
            .count();
        assert_eq!(count, 1, "tab must appear exactly once after idempotent residency calls");
    }

    #[test]
    fn reorder_folder_nested_to_root_updates_parent_and_root_order() {
        // Regression: promoting a nested folder to root via reorder_folder(_, None, Some(&before))
        // must both clear parent_folder_id and insert the folder into root_order at the
        // correct position.  Previously the root_order insertion was skipped when the folder
        // was not already in root_order.

        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();

        let anchor = manager.create_folder(&space_id, "Anchor", false).unwrap();
        let parent = manager.create_folder(&space_id, "Parent", false).unwrap();
        let nested = manager.create_folder(&space_id, "Nested", false).unwrap();

        if let Some(space) = manager.spaces.get_mut(&space_id) {
            if let Some(f) = space.folders.iter_mut().find(|f| f.id == nested.id) {
                f.parent_folder_id = Some(parent.id.clone());
            }
            space.root_order.retain(|item| item != &RootItem::Folder(nested.id.clone()));
        }

        assert!(
            !manager.get_root_order(&space_id).contains(&RootItem::Folder(nested.id.clone())),
            "precondition: nested must not be in root_order before promotion"
        );

        manager.reorder_folder(&space_id, &nested.id, None, Some(&anchor.id));

        let space = manager.spaces.get(&space_id).unwrap();
        let promoted = space.folders.iter().find(|f| f.id == nested.id).unwrap();
        assert!(
            promoted.parent_folder_id.is_none(),
            "promoted folder must have no parent"
        );

        let root_order = manager.get_root_order(&space_id);
        let nested_pos = root_order.iter().position(|i| i == &RootItem::Folder(nested.id.clone()));
        let anchor_pos = root_order.iter().position(|i| i == &RootItem::Folder(anchor.id.clone()));
        assert!(nested_pos.is_some(), "promoted folder must be present in root_order");
        assert!(anchor_pos.is_some(), "anchor folder must still be in root_order");
        assert!(
            nested_pos.unwrap() < anchor_pos.unwrap(),
            "promoted folder must appear before anchor in root_order"
        );
    }

    #[test]
    fn move_folder_in_space_does_not_affect_same_folder_id_in_other_space() {
        let mut manager = SpaceManager::new();
        let space_a = manager.get_active_space_id().clone();

        let folder_a = manager.create_folder(&space_a, "FA", false).unwrap();

        let space_b = manager
            .create_space("SpaceB", maho_types::space::SpaceColor { hue: 1.0, saturation: 0.5, brightness: 0.5, grain: 0.0 }, maho_types::identifiers::ProfileId::new(""))
            .id;

        let parent_b = manager.create_folder(&space_b, "Parent", false).unwrap();

        manager.move_folder_in_space(&space_a, &folder_a.id, Some(parent_b.id.clone()));

        let space_a_data = manager.spaces.get(&space_a).unwrap();
        let folder_in_a = space_a_data.folders.iter().find(|f| f.id == folder_a.id).unwrap();
        assert_eq!(
            folder_in_a.parent_folder_id,
            Some(parent_b.id.clone()),
            "folder in space_a should have new parent"
        );

        let space_b_data = manager.spaces.get(&space_b).unwrap();
        let parent_in_b = space_b_data.folders.iter().find(|f| f.id == parent_b.id).unwrap();
        assert!(
            parent_in_b.parent_folder_id.is_none(),
            "parent folder in space_b must be unaffected"
        );
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
}

impl Default for SpaceManager {
    fn default() -> Self {
        Self::new()
    }
}

impl SpaceManager {
    pub fn new() -> Self {
        let mut mgr = Self {
            spaces: HashMap::new(),
            space_order: vec![],
            active_space_id: SpaceId::new(""),
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
        mgr.active_space_id = default_space.id;
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
        self.spaces.clear();
        self.space_order.clear();
        for space in &spaces {
            self.space_order.push(space.id.clone());
        }
        for mut space in spaces {
            if space.root_order.is_empty() {
                space.root_order = Self::synthesize_root_order(&space);
            }
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
    }

    fn synthesize_root_order(space: &Space) -> Vec<RootItem> {
        let mut root_order = Vec::with_capacity(space.tab_order.len() + space.folders.len());
        let mut tab_ids_in_folders = HashSet::new();
        for folder in &space.folders {
            tab_ids_in_folders.extend(folder.tab_ids.iter().cloned());
        }

        for tab_id in &space.tab_order {
            if !tab_ids_in_folders.contains(tab_id) {
                root_order.push(RootItem::Tab(tab_id.clone()));
            }
        }
        for folder in &space.folders {
            if folder.parent_folder_id.is_none() {
                root_order.push(RootItem::Folder(folder.id.clone()));
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

    pub fn remove_tab_from_space(&mut self, space_id: &SpaceId, tab_id: &TabId) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(idx) = space.tab_order.iter().position(|id| id == tab_id) {
                space.tab_order.remove(idx);
            }
            space.root_order.retain(|item| item != &RootItem::Tab(tab_id.clone()));
            for folder in &mut space.folders {
                folder.tab_ids.retain(|id| id != tab_id);
            }
        }
    }

    pub fn reorder_tab(&mut self, space_id: &SpaceId, tab_id: &TabId, before_tab_id: Option<&TabId>) {
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

        let should_delete_folder = self
            .spaces
            .get(space_id)
            .and_then(|space| space.folders.iter().find(|folder| folder.id == *folder_id))
            .is_some_and(|folder| folder.tab_ids.is_empty());

        if should_delete_folder {
            self.delete_folder(space_id, folder_id);
        }
    }

    pub fn reorder_folder(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        parent_folder_id: Option<&FolderId>,
        before_folder_id: Option<&FolderId>,
    ) {
        {
            let space = match self.spaces.get_mut(space_id) {
                Some(s) => s,
                None => return,
            };

            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                folder.parent_folder_id = parent_folder_id.cloned();
            }
            if parent_folder_id.is_some() {
                space
                    .root_order
                    .retain(|item| item != &RootItem::Folder(folder_id.clone()));
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
            self.reorder_root_item(space_id, &RootItem::Folder(folder_id.clone()), &insertion_point);
        }
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
        self.reorder_root_item(space_id, &RootItem::Folder(folder_id.clone()), &insertion_point);
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
            space.root_order.retain(|item| item != &RootItem::Tab(tab_id.clone()));
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
    /// - `is_favorite = true`  → remove from root_order (favorites live in their own bucket).
    /// - `is_favorite = false, is_in_folder = false` → ensure present in root_order once.
    /// - `is_favorite = false, is_in_folder = true`  → remove from root_order (folder owns it).
    ///
    /// This is the single authoritative seam for bucket placement.  All event paths that
    /// change a tab's favorite status or move it across spaces must call this after the move.
    pub fn apply_tab_residency(
        &mut self,
        space_id: &SpaceId,
        tab_id: &TabId,
        is_favorite: bool,
        is_in_folder: bool,
    ) {
        if is_favorite || is_in_folder {
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
    ) -> Option<FolderViewModel> {
        let space = self.spaces.get_mut(space_id)?;
        let folder = Folder {
            id: FolderId::generate(),
            name: name.to_string(),
            tab_ids: vec![],
            is_expanded: true,
            is_pinned,
            parent_folder_id: None,
        };
        let vm = FolderViewModel {
            id: folder.id.clone(),
            name: folder.name.clone(),
            tab_count: folder.tab_ids.len(),
            is_expanded: folder.is_expanded,
            is_pinned: folder.is_pinned,
            tab_ids: folder.tab_ids.clone(),
            parent_folder_id: folder.parent_folder_id.clone(),
        };
        if !space.root_order.iter().any(|item| item == &RootItem::Folder(folder.id.clone())) {
            space.root_order.push(RootItem::Folder(folder.id.clone()));
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

    pub fn delete_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            space.folders.retain(|f| f.id != *folder_id);
            space.root_order.retain(|item| item != &RootItem::Folder(folder_id.clone()));
        }
    }

    pub fn add_tab_to_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId, tab_id: TabId) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            // Ensure tab belongs to at most one folder within a space.
            for folder in space.folders.iter_mut() {
                if folder.id != *folder_id {
                    folder.tab_ids.retain(|t| t != &tab_id);
                }
            }
            space.root_order.retain(|item| item != &RootItem::Tab(tab_id.clone()));
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

    pub fn toggle_folder_expanded(&mut self, space_id: &SpaceId, folder_id: &FolderId) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                folder.is_expanded = !folder.is_expanded;
            }
        }
    }

    pub fn pin_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                folder.is_pinned = true;
            }
        }
    }

    pub fn unpin_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                folder.is_pinned = false;
            }
        }
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

    pub fn move_folder(&mut self, folder_id: &FolderId, new_parent: Option<FolderId>) {
        for space in self.spaces.values_mut() {
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                folder.parent_folder_id = new_parent.clone();
                if new_parent.is_some() {
                    space
                        .root_order
                        .retain(|item| item != &RootItem::Folder(folder_id.clone()));
                }
                return;
            }
        }
    }

    pub fn move_folder_in_space(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        new_parent: Option<FolderId>,
    ) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                folder.parent_folder_id = new_parent.clone();
                if new_parent.is_some() {
                    space
                        .root_order
                        .retain(|item| item != &RootItem::Folder(folder_id.clone()));
                }
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

    fn folder_to_view_model(&self, folder: &Folder) -> FolderViewModel {
        FolderViewModel {
            id: folder.id.clone(),
            name: folder.name.clone(),
            tab_count: folder.tab_ids.len(),
            is_expanded: folder.is_expanded,
            is_pinned: folder.is_pinned,
            tab_ids: folder.tab_ids.clone(),
            parent_folder_id: folder.parent_folder_id.clone(),
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
                space.icon = i.clone();
                applied.icon = Some(i.clone());
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
        self.spaces
            .values()
            .map(|s| (s.id.clone(), s.atc_rules.clone()))
            .collect()
    }
}
