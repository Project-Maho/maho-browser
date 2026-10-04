use maho_core::maho_core::MahoCore;
use maho_types::common::Url;
use maho_types::events::core_update::CoreUpdate;
use maho_types::events::shell_event::ShellEvent;
use maho_types::space::SpaceColor;
use maho_types::tab::TabRole;

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
    let orig_binding = original.iter().find(|b| b["action"] == "new_tab").unwrap();
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
    let profile = core
        .create_profile_persisted("Work Profile".to_string())
        .unwrap();
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
    assert!(
        space_deleted_event.is_some(),
        "Expected SpaceDeleted event, but got {:?}",
        updates
    );
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
        let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");
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
        let boost =
            core.create_boost_persisted("example.com".to_string(), "My Test Boost".to_string());
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
    fn traffic_rule_toggle_persistence_round_trip() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);
        let rule = TrafficRule {
            id: "traffic-rule-toggle".to_string(),
            url_pattern: "example.com".to_string(),
            match_type: MatchType::Contains,
            target_space_id: maho_types::identifiers::SpaceId::new("space-1"),
            enabled: true,
        };
        let created_rule = core.create_traffic_rule_persisted(rule.clone());
        let rule_id = created_rule.id.clone();

        core.toggle_traffic_rule_persisted(&rule_id, false);

        drop(core);

        let mut restored_core = create_persisted_core(&temp_dir);
        assert!(restored_core.load_persisted_data().is_ok());

        let restored_rule = restored_core
            .get_atc_rules()
            .iter()
            .find(|restored_rule| restored_rule.id == rule_id);
        assert!(restored_rule.is_some());
        let restored_rule = restored_rule.expect("traffic rule should exist after restore");
        assert_eq!(restored_rule.enabled, false);
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
    fn atc_settings_persistence_round_trip() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);

        // Verify default values
        assert_eq!(core.get_open_external_links_in_maho_mini(), false);

        // Set values and trigger events
        core.set_open_external_links_in_maho_mini_persisted(true);
        core.handle_event(ShellEvent::SetDefaultLinkBehavior {
            behavior: maho_types::air_traffic::DefaultLinkBehavior::SpecificSpace {
                space_id: maho_types::identifiers::SpaceId::new("space-abc"),
            },
        });

        drop(core);

        let mut restored_core = create_persisted_core(&temp_dir);
        assert!(restored_core.load_persisted_data().is_ok());

        assert_eq!(restored_core.get_open_external_links_in_maho_mini(), true);
        match restored_core.get_default_link_behavior() {
            maho_types::air_traffic::DefaultLinkBehavior::SpecificSpace { space_id } => {
                assert_eq!(space_id.as_ref(), "space-abc");
            }
            _ => panic!("Expected SpecificSpace behavior"),
        }
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
            address_line2: None,
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
            card_network: None,
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
    fn add_bookmark_reports_the_stored_id() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);

        let id = core
            .add_bookmark_returning_id("https://example.com/docs", "Docs", None)
            .expect("bookmark should be stored");
        assert!(!id.is_empty(), "stored bookmark must report an id");

        // Agent tools answer with the id they just received, so it has to be
        // the id the store actually holds.
        let found = core.search_bookmarks("Docs");
        assert!(
            found.iter().any(|row| row.0 == id),
            "stored id {id} should be searchable, got {found:?}"
        );
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

        let profile = core
            .create_profile_persisted("Test Profile".to_string())
            .unwrap();
        let profile_id = profile.id.clone();

        drop(core);

        let mut restored = create_persisted_core(&temp_dir);
        assert!(restored.load_persisted_data().is_ok());

        let list = restored.list_profiles();
        assert!(list
            .iter()
            .any(|p| p.id == profile_id && p.name == "Test Profile"));
    }

    #[test]
    fn profile_metadata_update_targets_non_active_profile_and_survives_restart() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);
        assert!(core.load_persisted_data().is_ok());

        let active = core
            .create_profile_persisted("Profile A".to_string())
            .unwrap();
        let active_id = active.id.clone();
        let target = core
            .create_profile_persisted("Profile B".to_string())
            .unwrap();
        let target_id = target.id.clone();
        assert!(core.switch_profile(&active_id));
        let updates = Rc::new(RefCell::new(Vec::new()));
        let captured = Rc::clone(&updates);
        core.on_update(Box::new(move |update| captured.borrow_mut().push(update)));

        let updated = core
            .update_profile_persisted(
                &target_id,
                Some("  Profile B Updated  ".to_string()),
                Some("#112233".to_string()),
                Some("/tmp/profile-b".to_string()),
                Some(None),
            )
            .expect("target profile exists");

        assert_eq!(core.get_active_profile_id(), Some(&active_id));
        assert_eq!(updated.name, "Profile B Updated");
        assert_eq!(updated.avatar_color, "#112233");
        assert_eq!(updated.download_path, "/tmp/profile-b");
        assert_eq!(updated.archive_timeout_hours, None);
        // The persisted helper emits exactly one target ProfileUpdated and
        // never a misleading active-profile update, matching the lib-level
        // contract update_profile_persisted_emits_exactly_one_target_metadata_update.
        assert!(matches!(
            updates.borrow().as_slice(),
            [CoreUpdate::ProfileUpdated { profile }]
                if profile.id == target_id
                    && profile.id != active_id
                    && profile.name == "Profile B Updated"
        ));

        drop(core);

        let mut restored = create_persisted_core(&temp_dir);
        assert!(restored.load_persisted_data().is_ok());
        assert_eq!(restored.get_active_profile_id(), Some(&active_id));
        let restored_target = restored
            .list_profiles()
            .iter()
            .find(|profile| profile.id == target_id)
            .expect("target profile survives restart");
        assert_eq!(restored_target.name, "Profile B Updated");
        assert_eq!(restored_target.avatar_color, "#112233");
        assert_eq!(restored_target.download_path, "/tmp/profile-b");
        assert_eq!(restored_target.archive_timeout_hours, None);
    }

    #[test]
    fn update_profile_event_emits_target_update_without_active_switch() {
        let mut core = MahoCore::new();
        let active_id = core
            .get_active_profile_id()
            .expect("default profile is active")
            .clone();
        let target = core
            .create_profile_persisted("Profile B".to_string())
            .unwrap();
        let updates = core.handle_event(ShellEvent::UpdateProfile {
            profile_id: target.id.clone(),
            name: Some("Profile B Updated".to_string()),
            avatar_color: None,
            download_path: None,
            archive_timeout_hours: None,
        });

        assert_eq!(core.get_active_profile_id(), Some(&active_id));
        assert!(matches!(
            updates.as_slice(),
            [CoreUpdate::ProfileUpdated { profile }]
                if profile.id == target.id && profile.name == "Profile B Updated"
        ));
    }

    #[test]
    fn profile_delete_outcomes_cover_protected_not_found_in_use_and_final() {
        use maho_types::identifiers::ProfileId;
        use maho_types::profile::{ProfileConfig, ProfileDeleteOutcome};

        let mut core = MahoCore::new();
        let default_id = core
            .get_active_profile_id()
            .expect("default profile is active")
            .clone();
        assert_eq!(
            core.delete_profile_detailed(&default_id),
            ProfileDeleteOutcome::Protected
        );
        assert_eq!(
            core.delete_profile_detailed(&ProfileId::new("malformed/not-a-real-profile")),
            ProfileDeleteOutcome::NotFound
        );

        let in_use = core.create_profile_persisted("In Use".to_string()).unwrap();
        core.create_space(
            "Profile Space",
            SpaceColor {
                hue: 120.0,
                saturation: 0.5,
                brightness: 0.5,
                grain: 0.0,
            },
            in_use.id.clone(),
        );
        assert_eq!(
            core.delete_profile_detailed(&in_use.id),
            ProfileDeleteOutcome::InUse
        );

        let final_profile = ProfileConfig::new("Final".to_string());
        let final_id = final_profile.id.clone();
        let mut manager = maho_core::profile_manager::ProfileManager::new();
        manager.restore_profiles(vec![final_profile], Some(final_id.clone()));
        assert_eq!(
            manager.delete_profile(&final_id),
            ProfileDeleteOutcome::FinalProfile
        );
    }

    #[test]
    fn profile_delete_persistence() {
        let temp_dir = temp_dir();
        let mut core = create_persisted_core(&temp_dir);
        assert!(core.load_persisted_data().is_ok());

        let profile = core
            .create_profile_persisted("Test Profile 2".to_string())
            .unwrap();
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

        let profile = core
            .create_profile_persisted("Test Profile 3".to_string())
            .unwrap();
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
        assert!(matches!(
            res,
            Err(maho_types::keyboard::SetShortcutError::Reserved)
        ));
    }

    #[test]
    fn shortcut_exhaustive_actions_parity_test() {
        let temp_dir = temp_dir();
        let core = create_persisted_core(&temp_dir);

        let shortcuts_json = core.get_all_shortcuts_json();
        let bindings: Vec<serde_json::Value> =
            serde_json::from_str(&shortcuts_json).expect("should parse shortcuts JSON");

        #[allow(unused_mut)]
        let mut expected_actions = vec![
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
            "toggle_spaces_overlay",
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
            "maho_mini",
            "close_window",
            "add_split_view",
            "add_split_view_arc",
            "increase_split_view",
            "decrease_split_view",
            "remove_split_view",
            "next_split_view",
            "prev_split_view",
            "swap_split_view",
            "toggle_split_view",
            "toggle_split_orientation",
            "show_archive",
            "open_downloads",
            "settings",
            "open_history",
            // `new_note` is intentionally absent: the 2026-08-20 registry audit
            // removed it as a ghost binding (Ctrl+N with no browser surface,
            // swallowing the key on macOS). See Rule 4 in
            // `maho-chromium/docs/shortcut-registry-contract.md`; it returns
            // only together with a dispatch handler when Notes ships.
            "zoom_in",
            "zoom_out",
            "reset_zoom",
            "capture_screenshot",
            "toggle_dev_tools",
            "view_source",
            "js_console",
            "open_boost_editor",
            "open_atc_rules",
        ];
        // Alt+D address-bar alias exists only off macOS.
        #[cfg(not(target_os = "macos"))]
        expected_actions.push("command_bar_alt");

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
        let after_bindings: Vec<serde_json::Value> = serde_json::from_str(&after_import).unwrap();
        let new_tab_binding = after_bindings
            .iter()
            .find(|b| b["action"] == "new_tab")
            .expect("new_tab should exist after import");
        assert_eq!(new_tab_binding["keyCombo"]["key"], "p");
        assert!(new_tab_binding["isCustom"].as_bool().unwrap());
    }
}

#[test]
fn pin_unpin_routed_through_transition_seam() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();

    let create_updates = core.handle_event(ShellEvent::CreateTab {
        space_id: space_id.clone(),
        url: Some(Url::new("https://example.com")),
        parent_id: None,
        tab_id: None,
        window_id: None,
        is_private: false,
    });
    let tab_id = create_updates
        .iter()
        .find_map(|u| {
            if let CoreUpdate::TabCreated { tab } = u {
                Some(tab.id.clone())
            } else {
                None
            }
        })
        .expect("CreateTab must produce TabCreated");

    let vm_before = core
        .get_tab_view_models()
        .into_iter()
        .find(|t| t.id == tab_id)
        .expect("tab must exist after create");
    assert_eq!(vm_before.role, TabRole::Normal);
    assert!(!vm_before.is_pinned);

    let pin_updates = core.handle_event(ShellEvent::PinTab {
        tab_id: tab_id.clone(),
    });

    assert!(
        pin_updates.iter().any(|u| matches!(
            u,
            CoreUpdate::TabRoleChanged { tab_id: tid, new_role: TabRole::Pinned, .. }
                if *tid == tab_id
        )),
        "PinTab must emit TabRoleChanged{{new_role: Pinned}}; got: {pin_updates:?}"
    );

    let vm_pinned = core
        .get_tab_view_models()
        .into_iter()
        .find(|t| t.id == tab_id)
        .expect("tab must exist after pin");
    assert_eq!(vm_pinned.role, TabRole::Pinned);
    assert!(vm_pinned.is_pinned);

    let unpin_updates = core.handle_event(ShellEvent::UnpinTab {
        tab_id: tab_id.clone(),
    });

    assert!(
        unpin_updates.iter().any(|u| matches!(
            u,
            CoreUpdate::TabRoleChanged { tab_id: tid, new_role: TabRole::Normal, .. }
                if *tid == tab_id
        )),
        "UnpinTab must emit TabRoleChanged{{new_role: Normal}}; got: {unpin_updates:?}"
    );

    let vm_unpinned = core
        .get_tab_view_models()
        .into_iter()
        .find(|t| t.id == tab_id)
        .expect("tab must exist after unpin");
    assert_eq!(vm_unpinned.role, TabRole::Normal);
    assert!(!vm_unpinned.is_pinned);
}
