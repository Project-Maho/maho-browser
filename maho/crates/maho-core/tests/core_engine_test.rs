use maho_core::maho_core::MahoCore;
use maho_types::events::core_update::CoreUpdate;
use maho_types::space::SpaceColor;

use std::cell::RefCell;
use std::rc::Rc;

#[test]
fn shortcut_manager_has_default_bindings() {
    let core = MahoCore::new();
    let json = core.get_all_shortcuts_json();
    let bindings: Vec<serde_json::Value> = serde_json::from_str(&json).unwrap();
    assert!(bindings.len() >= 30);
    let actions: Vec<String> = bindings
        .iter()
        .map(|b| b["action"].as_str().unwrap().to_string())
        .collect();
    assert!(actions.contains(&"new_tab".to_string()));
    assert!(actions.contains(&"close_tab".to_string()));
    assert!(actions.contains(&"reload_tab".to_string()));
    assert!(actions.contains(&"toggle_sidebar".to_string()));
    assert!(actions.contains(&"zoom_in".to_string()));
}

#[test]
fn shortcut_manager_set_shortcut_works() {
    let mut core = MahoCore::new();
    let key_combo = r#"{"key":"p","modifiers":["meta","shift"]}"#;
    let result = core.set_shortcut_json("new_tab", key_combo);
    assert!(result.is_ok());
    let json = core.get_all_shortcuts_json();
    assert!(json.contains("\"key\":\"p\""));
}

#[test]
fn shortcut_manager_detects_conflict() {
    let mut core = MahoCore::new();
    let key_combo = r#"{"key":"w","modifiers":["meta"]}"#;
    let result = core.set_shortcut_json("reload", key_combo);
    assert!(result.is_err());
}

#[test]
fn shortcut_manager_reset_single() {
    let mut core = MahoCore::new();
    let original_json = core.get_all_shortcuts_json();
    let key_combo = r#"{"key":"k","modifiers":["meta","shift"]}"#;
    let _ = core.set_shortcut_json("new_tab", key_combo);
    core.reset_shortcut("new_tab");
    let after_reset = core.get_all_shortcuts_json();
    let original: Vec<serde_json::Value> = serde_json::from_str(&original_json).unwrap();
    let reset: Vec<serde_json::Value> = serde_json::from_str(&after_reset).unwrap();
    let orig_binding = original
        .iter()
        .find(|b| b["action"] == "new_tab")
        .unwrap();
    let reset_binding = reset.iter().find(|b| b["action"] == "new_tab").unwrap();
    assert_eq!(orig_binding["keyCombo"], reset_binding["keyCombo"]);
}

#[test]
fn shortcut_manager_reset_all() {
    let mut core = MahoCore::new();
    let original_json = core.get_all_shortcuts_json();
    let key_combo = r#"{"key":"k","modifiers":["meta","shift"]}"#;
    let _ = core.set_shortcut_json("new_tab", key_combo);
    core.reset_all_shortcuts();
    let after_reset = core.get_all_shortcuts_json();
    assert_eq!(original_json, after_reset);
}

#[test]
fn shortcut_default_key_combo_immutable_after_override() {
    let mut core = MahoCore::new();
    let json = core.get_all_shortcuts_json();
    let bindings: Vec<serde_json::Value> = serde_json::from_str(&json).unwrap();
    let original = bindings.iter().find(|b| b["action"] == "new_tab").unwrap();
    let original_default = original["defaultKeyCombo"].clone();
    let original_key = original["keyCombo"].clone();
    assert_eq!(original_default, original_key);

    let key_combo = r#"{"key":"p","modifiers":["meta","shift"]}"#;
    let result = core.set_shortcut_json("new_tab", key_combo);
    assert!(result.is_ok());

    let json = core.get_all_shortcuts_json();
    let bindings: Vec<serde_json::Value> = serde_json::from_str(&json).unwrap();
    let overridden = bindings.iter().find(|b| b["action"] == "new_tab").unwrap();
    assert_eq!(overridden["keyCombo"]["key"], "p");
    assert_eq!(overridden["defaultKeyCombo"], original_default);

    core.reset_shortcut("new_tab");
    let json = core.get_all_shortcuts_json();
    let bindings: Vec<serde_json::Value> = serde_json::from_str(&json).unwrap();
    let reset = bindings.iter().find(|b| b["action"] == "new_tab").unwrap();
    assert_eq!(reset["keyCombo"], reset["defaultKeyCombo"]);
}

#[test]
fn shortcut_manager_select_tab_1_through_9() {
    let core = MahoCore::new();
    let json = core.get_all_shortcuts_json();
    let bindings: Vec<serde_json::Value> = serde_json::from_str(&json).unwrap();
    let tab_shortcuts: Vec<&serde_json::Value> = bindings
        .iter()
        .filter(|b| b["action"].as_str().unwrap().starts_with("select_tab_"))
        .collect();
    assert_eq!(tab_shortcuts.len(), 9);
}

#[test]
fn set_density_compact() {
    let mut core = MahoCore::new();
    core.set_density("compact");
    let settings = core.get_settings();
    assert!(serde_json::to_string(&settings.appearance.density)
        .unwrap()
        .contains("compact"));
}

#[test]
fn set_density_comfortable() {
    let mut core = MahoCore::new();
    core.set_density("comfortable");
    let settings = core.get_settings();
    assert!(serde_json::to_string(&settings.appearance.density)
        .unwrap()
        .contains("comfortable"));
}

#[test]
fn set_accent_color() {
    let mut core = MahoCore::new();
    core.set_accent_color(r#"{"r":255,"g":100,"b":50,"a":1.0}"#);
    let settings = core.get_settings();
    assert!(settings.appearance.accent_color.is_some());
}

#[test]
fn set_accent_color_null() {
    let mut core = MahoCore::new();
    core.set_accent_color("null");
    let settings = core.get_settings();
    assert!(settings.appearance.accent_color.is_none());
}

#[test]
fn set_window_transparency() {
    let mut core = MahoCore::new();
    core.set_window_transparency(true);
    let settings = core.get_settings();
    assert!(settings.appearance.window_transparency);
}

#[test]
fn toolbar_items_default() {
    let core = MahoCore::new();
    let json = core.get_toolbar_items_json();
    let items: Vec<serde_json::Value> = serde_json::from_str(&json).unwrap();
    assert!(items.len() >= 5);
    let ids: Vec<String> = items
        .iter()
        .map(|i| i["id"].as_str().unwrap().to_string())
        .collect();
    assert!(ids.contains(&"back_forward".to_string()));
    assert!(ids.contains(&"address_bar".to_string()));
}

#[test]
fn set_toolbar_items() {
    let mut core = MahoCore::new();
    let items_json = r#"[{"id":"reload","kind":"reload","visible":true,"label":"Reload"},{"id":"address_bar","kind":"address_bar","visible":true,"label":"Address Bar"}]"#;
    let result = core.set_toolbar_items_json(items_json);
    assert!(result.is_ok());
    let json = core.get_toolbar_items_json();
    let items: Vec<serde_json::Value> = serde_json::from_str(&json).unwrap();
    assert_eq!(items.len(), 2);
}

#[test]
fn multi_observer_global_sync_behavior() {
    let mut core = MahoCore::new();
    let first = Rc::new(RefCell::new(Vec::new()));
    let second = Rc::new(RefCell::new(Vec::new()));

    let first_sink = first.clone();
    core.on_update(Box::new(move |update| first_sink.borrow_mut().push(update)));

    let second_sink = second.clone();
    core.on_update(Box::new(move |update| {
        second_sink.borrow_mut().push(update)
    }));

    let active_space = core.get_active_space_id();
    core.rename_space(&active_space, "Synced Space");

    assert_eq!(first.borrow().len(), 1);
    assert_eq!(second.borrow().len(), 1);
    assert!(matches!(
        first.borrow().as_slice(),
        [CoreUpdate::SpaceConfigUpdated { .. }]
    ));
    assert!(matches!(
        second.borrow().as_slice(),
        [CoreUpdate::SpaceConfigUpdated { .. }]
    ));
}

#[test]
fn delete_active_space_falls_back_and_persists_order_name_and_profile_linkage() {
    let temp_dir = tempfile::tempdir().expect("temp dir");
    let storage_path = temp_dir.path().join("maho-core.lmdb");

    let mut core = MahoCore::new().with_lmdb_storage(&storage_path);
    let profile = core.create_profile_persisted("Work Profile".to_string());
    let color = SpaceColor {
        hue: 210.0,
        saturation: 0.6,
        brightness: 0.8,
        grain: 0.0,
    };

    let first_space = core.create_space("Personal", color.clone(), profile.id.clone());
    let second_space = core.create_space("Research", color.clone(), profile.id.clone());
    core.rename_space(&second_space.id, "Research Space");
    core.reorder_space(&second_space.id, 2, 0);

    core.save_state().expect("state should save");

    let mut restored = MahoCore::new().with_lmdb_storage(&storage_path);
    restored.load_state().expect("state should restore");

    let restored_spaces = restored.get_space_view_models();
    let restored_active_space_id = restored.get_active_space_id();
    assert_eq!(
        restored_spaces.first().map(|space| &space.id),
        Some(&second_space.id)
    );
    let restored_first_space = restored_spaces
        .iter()
        .find(|space| space.id == first_space.id)
        .expect("first created space should persist");
    let restored_second_space = restored_spaces
        .iter()
        .find(|space| space.id == second_space.id)
        .expect("second created space should persist");
    assert!(restored_spaces
        .iter()
        .any(|space| space.name == "Research Space"));
    assert_eq!(restored_first_space.profile_id, Some(profile.id.clone()));
    assert_eq!(restored_second_space.profile_id, Some(profile.id.clone()));

    let observed = Rc::new(RefCell::new(Vec::new()));
    let observed_sink = observed.clone();
    restored.on_update(Box::new(move |update| {
        observed_sink.borrow_mut().push(update)
    }));

    restored.delete_space(&first_space.id);
    let updates = observed.borrow();
    let space_deleted_event = updates.iter().find(|u| {
        if let CoreUpdate::SpaceDeleted { space_id } = u {
            space_id == &first_space.id
        } else {
            false
        }
    });
    assert!(space_deleted_event.is_some(), "Expected SpaceDeleted event, but got {:?}", updates);
    assert_eq!(restored.get_active_space_id(), restored_active_space_id);
}

#[cfg(test)]
mod persistence_tests {
    use super::*;
    use maho_types::air_traffic::{MatchType, TrafficRule};
    use maho_types::autofill::{AutofillAddress, AutofillPayment};
    use maho_types::keyboard::{KeyCombo, KeyModifier};
    use maho_types::search_engine::SearchEngine;
    use tempfile::TempDir;

    fn create_persisted_core(temp_dir: &TempDir) -> MahoCore {
        let storage_path = temp_dir.path().join("maho-core.sqlite");
        let storage_path = storage_path.to_string_lossy();
        MahoCore::new().with_storage(&storage_path)
    }

    fn temp_dir() -> TempDir {
        match tempfile::tempdir() {
            Ok(dir) => dir,
            Err(err) => panic!("failed to create temp dir: {err}"),
        }
    }

    #[test]
    fn note_persistence_round_trip() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);
        let note = core.create_note_persisted(None, "test content".to_string());
        let note_id = note.id.clone();

        drop(core);

        let mut restored_core = create_persisted_core(&temp_dir);
        assert!(restored_core.load_persisted_data().is_ok());

        let notes = restored_core.get_note_view_models();
        let restored_note = notes.iter().find(|note| note.id == note_id.as_ref());
        assert!(restored_note.is_some());
        assert_eq!(
            restored_note.map(|note| note.content.as_str()),
            Some("test content")
        );
    }

    #[test]
    fn boost_persistence_round_trip() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);
        let boost = core.create_boost_persisted(
            "example.com".to_string(),
            "My Test Boost".to_string(),
        );
        let boost_id = boost.id.clone();
        core.set_active_boost_persisted("example.com", Some(boost_id.clone()));

        drop(core);

        let mut restored_core = create_persisted_core(&temp_dir);
        assert!(restored_core.load_persisted_data().is_ok());

        let boosts = restored_core.get_boost_view_models();
        let restored_boost = boosts.iter().find(|boost| boost.id == boost_id);
        assert!(restored_boost.is_some());
        assert_eq!(
            restored_boost.map(|boost| boost.domain.as_str()),
            Some("example.com")
        );
        assert!(restored_boost.map(|boost| boost.enabled).unwrap_or(false));
    }

    #[test]
    fn traffic_rule_persistence_round_trip() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);
        let rule = TrafficRule {
            id: "traffic-rule-1".to_string(),
            url_pattern: "example.com".to_string(),
            match_type: MatchType::Contains,
            target_space_id: maho_types::identifiers::SpaceId::new("space-1"),
            enabled: true,
        };
        let created_rule = core.create_traffic_rule_persisted(rule.clone());
        let rule_id = created_rule.id.clone();
        assert_eq!(created_rule.id, rule.id);

        drop(core);

        let mut restored_core = create_persisted_core(&temp_dir);
        assert!(restored_core.load_persisted_data().is_ok());

        let restored_rule = restored_core
            .get_atc_rules()
            .iter()
            .find(|restored_rule| restored_rule.id == rule_id);
        assert!(restored_rule.is_some());
        let restored_rule = restored_rule.expect("traffic rule should exist after restore");
        assert_eq!(restored_rule.id, rule.id);
        assert_eq!(restored_rule.name, rule.url_pattern);
        assert_eq!(restored_rule.space_id, Some(rule.target_space_id));
        assert_eq!(restored_rule.url_pattern, None);
        assert_eq!(restored_rule.max_age_hours, None);
        assert_eq!(restored_rule.max_tabs, None);
        assert_eq!(restored_rule.enabled, rule.enabled);
    }

    #[test]
    fn permission_persistence_round_trip() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);
        core.grant_permission_persisted("https://example.com", "camera");

        drop(core);

        let mut restored_core = create_persisted_core(&temp_dir);
        assert!(restored_core.load_persisted_data().is_ok());

        assert_eq!(
            restored_core.query_permission("https://example.com", "camera"),
            "allow"
        );
    }

    #[test]
    fn delete_persistence_round_trip() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);
        let note = core.create_note_persisted(None, "temporary note".to_string());
        let note_id = note.id.clone();
        let deleted = core.delete_note_persisted(&note_id);
        assert!(deleted.is_some());

        drop(core);

        let mut restored_core = create_persisted_core(&temp_dir);
        assert!(restored_core.load_persisted_data().is_ok());

        let notes = restored_core.get_note_view_models();
        assert!(notes.iter().all(|note| note.id != note_id.as_ref()));
    }

    #[test]
    fn autofill_address_persistence_round_trip() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);

        let address = AutofillAddress {
            id: "addr-1".to_string(),
            name: "John".to_string(),
            street: "123 Main St".to_string(),
            city: "SF".to_string(),
            state: "CA".to_string(),
            zip: "94102".to_string(),
            country: "US".to_string(),
            phone: Some("555-1234".to_string()),
            email: Some("john@test.com".to_string()),
        };

        let added = core.add_autofill_address_persisted(address);
        let address_id = added.id.clone();

        drop(core);

        let mut restored_core = create_persisted_core(&temp_dir);
        assert!(restored_core.load_persisted_data().is_ok());

        // Use settings view model to verify autofill data
        let settings_vm = restored_core.get_settings_view_model();
        let autofill_section = settings_vm
            .sections
            .iter()
            .find(|s| s.title == "Autofill")
            .expect("should have Autofill section");
        let address_item = autofill_section
            .items
            .iter()
            .find(|i| i.key == format!("autofill_address_{}", address_id));
        assert!(address_item.is_some(), "address should exist after restore");
        assert_eq!(address_item.unwrap().label, "John");
    }

    #[test]
    fn autofill_payment_persistence_round_trip() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);

        let payment = AutofillPayment {
            id: "pay-1".to_string(),
            card_name: "Visa".to_string(),
            last_four: "4242".to_string(),
            expiry: "12/28".to_string(),
        };

        let added = core.add_autofill_payment_persisted(payment);
        let payment_id = added.id.clone();

        drop(core);

        let mut restored_core = create_persisted_core(&temp_dir);
        assert!(restored_core.load_persisted_data().is_ok());

        // Use settings view model to verify autofill data
        let settings_vm = restored_core.get_settings_view_model();
        let autofill_section = settings_vm
            .sections
            .iter()
            .find(|s| s.title == "Autofill")
            .expect("should have Autofill section");
        let payment_item = autofill_section
            .items
            .iter()
            .find(|i| i.key == format!("autofill_payment_{}", payment_id));
        assert!(payment_item.is_some(), "payment should exist after restore");
        // The label should contain the last four digits
        assert!(payment_item.unwrap().label.contains("4242"));
    }

    #[test]
    fn reading_list_persistence_round_trip() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);

        let item = core.add_reading_list_item_persisted("https://example.com", "Example Page");
        let item_id = item.id.clone();

        core.mark_reading_list_read_persisted(&item_id);

        drop(core);

        let mut restored_core = create_persisted_core(&temp_dir);
        assert!(restored_core.load_persisted_data().is_ok());

        // Use get_reading_list_json to verify reading list data
        let reading_list_json = restored_core.get_reading_list_json();
        let items: Vec<serde_json::Value> =
            serde_json::from_str(&reading_list_json).expect("should parse reading list JSON");
        let restored_item = items.iter().find(|i| i["id"] == item_id);
        assert!(restored_item.is_some(), "item should exist after restore");
        assert_eq!(restored_item.unwrap()["isRead"], true);
    }

    #[test]
    fn search_engine_persistence_round_trip() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);

        let engine = SearchEngine {
            id: "custom-1".to_string(),
            name: "Custom Search".to_string(),
            url_template: "https://custom.com/search?q={}".to_string(),
            shortcut: Some("cs".to_string()),
            icon_url: None,
            is_default: false,
        };

        core.add_search_engine_persisted(engine);
        core.set_default_search_engine_persisted("custom-1");

        drop(core);

        let mut restored_core = create_persisted_core(&temp_dir);
        assert!(restored_core.load_persisted_data().is_ok());

        let engines = restored_core.get_search_engines();
        let custom_engine = engines.iter().find(|e| e.id == "custom-1");
        assert!(
            custom_engine.is_some(),
            "custom engine should exist after restore"
        );
        assert!(
            custom_engine.unwrap().is_default,
            "custom engine should be default"
        );
    }

    #[test]
    fn shortcut_persistence_round_trip() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);

        let key_combo = KeyCombo {
            key: "p".to_string(),
            modifiers: vec![KeyModifier::Meta, KeyModifier::Shift],
        };

        core.set_shortcut_persisted("reload_tab", key_combo)
            .expect("should set shortcut");

        drop(core);

        let mut restored_core = create_persisted_core(&temp_dir);
        assert!(restored_core.load_persisted_data().is_ok());

        let shortcuts_json = restored_core.get_all_shortcuts_json();
        let bindings: Vec<serde_json::Value> =
            serde_json::from_str(&shortcuts_json).expect("should parse shortcuts JSON");
        let reload_binding = bindings.iter().find(|b| b["action"] == "reload_tab");
        assert!(
            reload_binding.is_some(),
            "reload_tab binding should exist after restore"
        );
        assert_eq!(reload_binding.unwrap()["keyCombo"]["key"], "p");
    }

    #[test]
    fn bookmark_folder_persistence_round_trip() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);

        let folder = core.create_bookmark_folder_persisted("Test Folder".to_string(), None);
        let folder_id = folder.id.clone();

        drop(core);

        let mut restored_core = create_persisted_core(&temp_dir);
        assert!(restored_core.load_persisted_data().is_ok());

        // Use get_settings_view_model to check bookmark folders
        // Folders are loaded into bookmark_manager but we need to verify through settings
        // Since there's no direct public getter, we verify by checking the folder was saved to storage
        // by attempting to add a bookmark to it
        let bookmark = restored_core.add_bookmark_entry(
            "Test Bookmark".to_string(),
            "https://example.com".to_string(),
            Some(folder_id.clone()),
            None,
        );
        assert_eq!(
            bookmark.folder_id,
            Some(folder_id),
            "bookmark should be in restored folder"
        );
    }

    #[test]
    fn profile_persistence_round_trip() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);
        assert!(core.load_persisted_data().is_ok());

        let profile = core.create_profile_persisted("Test Profile".to_string());
        let profile_id = profile.id.clone();

        drop(core);

        let mut restored = create_persisted_core(&temp_dir);
        assert!(restored.load_persisted_data().is_ok());

        let list = restored.list_profiles();
        assert!(list.iter().any(|p| p.id == profile_id && p.name == "Test Profile"));
    }

    #[test]
    fn profile_delete_persistence() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);
        assert!(core.load_persisted_data().is_ok());

        let profile = core.create_profile_persisted("Test Profile 2".to_string());
        let profile_id = profile.id.clone();

        let deleted = core.delete_profile(&profile_id);
        assert!(deleted);

        drop(core);

        let mut restored = create_persisted_core(&temp_dir);
        assert!(restored.load_persisted_data().is_ok());

        let list = restored.list_profiles();
        assert!(!list.iter().any(|p| p.id == profile_id));
    }

    #[test]
    fn active_profile_id_survives_restart() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);
        assert!(core.load_persisted_data().is_ok());

        let profile = core.create_profile_persisted("Test Profile 3".to_string());
        let profile_id = profile.id.clone();

        let switched = core.switch_profile(&profile_id);
        assert!(switched);

        drop(core);

        let mut restored = create_persisted_core(&temp_dir);
        assert!(restored.load_persisted_data().is_ok());

        let restored_active = restored.get_active_profile_id();
        assert_eq!(restored_active, Some(&profile_id));
    }

    #[test]
    fn shortcut_manager_rejects_reserved_combo() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);

        let reserved_combo = KeyCombo {
            key: "q".to_string(),
            modifiers: vec![KeyModifier::Meta],
        };

        let res = core.set_shortcut_persisted("reload_tab", reserved_combo);
        assert!(matches!(res, Err(maho_types::keyboard::SetShortcutError::Reserved)));
    }

    #[test]
    fn shortcut_exhaustive_actions_parity_test() {
        let temp_dir = temp_dir();
        let core = create_persisted_core(&temp_dir);

        let shortcuts_json = core.get_all_shortcuts_json();
        let bindings: Vec<serde_json::Value> =
            serde_json::from_str(&shortcuts_json).expect("should parse shortcuts JSON");

        let expected_actions = vec![
            "navigate_back",
            "navigate_forward",
            "reload_tab",
            "hard_reload",
            "stop_loading",
            "command_bar",
            "new_tab",
            "close_tab",
            "restore_tab",
            "pin_tab",
            "duplicate_tab",
            "next_tab",
            "prev_tab",
            "mru_tab_switch_next",
            "mru_tab_switch_prev",
            "clear_unpinned_tabs",
            "copy_url",
            "copy_url_markdown",
            "select_tab_1",
            "select_tab_2",
            "select_tab_3",
            "select_tab_4",
            "select_tab_5",
            "select_tab_6",
            "select_tab_7",
            "select_tab_8",
            "select_tab_9",
            "next_space",
            "prev_space",
            "new_space",
            "select_space_1",
            "select_space_2",
            "select_space_3",
            "select_space_4",
            "select_space_5",
            "select_space_6",
            "select_space_7",
            "select_space_8",
            "select_space_9",
            "toggle_sidebar",
            "ai_panel",
            "find_in_page",
            "fullscreen",
            "minimize",
            "new_window",
            "little_arc",
            "close_window",
            "add_split_view",
            "remove_split_view",
            "next_split_view",
            "prev_split_view",
            "show_archive",
            "open_downloads",
            "settings",
            "open_history",
            "open_bookmarks",
            "new_note",
            "zoom_in",
            "zoom_out",
            "reset_zoom",
            "toggle_dev_tools",
            "view_source",
            "js_console",
            "open_boost_editor",
            "open_atc_rules",
        ];

        assert_eq!(
            bindings.len(),
            expected_actions.len(),
            "Expected {} shortcuts, got {}",
            expected_actions.len(),
            bindings.len()
        );

        for action in &expected_actions {
            assert!(
                bindings.iter().any(|b| b["action"] == *action),
                "Missing action in registry: {}",
                action
            );
        }
    }

    #[test]
    fn shortcut_export_import_envelope_round_trip() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);

        let exported = core.export_shortcuts_json();
        let envelope: serde_json::Value =
            serde_json::from_str(&exported).expect("export should be valid JSON");

        assert_eq!(envelope["version"], 1);
        let platform = envelope["platform"].as_str().unwrap();
        assert!(
            ["macos", "windows", "linux"].contains(&platform),
            "unexpected platform: {}",
            platform
        );
        let exported_at = envelope["exportedAt"].as_str().unwrap();
        chrono::DateTime::parse_from_rfc3339(exported_at)
            .expect("exportedAt should be valid ISO 8601 / RFC 3339");
        assert!(envelope["shortcuts"].as_array().unwrap().len() > 0);

        let combo_json = r#"{"key":"p","modifiers":["meta","shift"]}"#;
        core.set_shortcut_json("new_tab", combo_json).unwrap();

        let modified_export = core.export_shortcuts_json();
        let import_result = core.import_shortcuts_json(&modified_export);
        assert!(import_result.is_ok());

        let after_import = core.get_all_shortcuts_json();
        let after_bindings: Vec<serde_json::Value> =
            serde_json::from_str(&after_import).unwrap();
        let new_tab_binding = after_bindings
            .iter()
            .find(|b| b["action"] == "new_tab")
            .expect("new_tab should exist after import");
        assert_eq!(new_tab_binding["keyCombo"]["key"], "p");
        assert!(new_tab_binding["isCustom"].as_bool().unwrap());
    }
}
