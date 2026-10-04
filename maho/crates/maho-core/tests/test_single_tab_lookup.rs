use maho_core::maho_core::MahoCore;
use maho_types::common::Url;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::TabId;

fn setup_core_with_tabs(count: usize) -> (MahoCore, Vec<TabId>) {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();
    let mut tab_ids = Vec::new();
    for i in 0..count {
        let before: std::collections::HashSet<TabId> = core
            .get_tab_view_models()
            .iter()
            .map(|t| t.id.clone())
            .collect();
        core.handle_event(ShellEvent::CreateTab {
            space_id: space_id.clone(),
            url: Some(Url::new(format!("https://example.com/{}", i))),
            parent_id: None,
            tab_id: None,
            window_id: None,
            is_private: false,
        });
        let after = core.get_tab_view_models();
        let new_tab = after.iter().find(|t| !before.contains(&t.id)).unwrap();
        tab_ids.push(new_tab.id.clone());
    }
    (core, tab_ids)
}

#[test]
fn test_get_tab_snapshot_by_id_found() {
    let (core, tab_ids) = setup_core_with_tabs(5);
    let tab_id = &tab_ids[2];
    let json_opt = core.get_tab_snapshot_json(tab_id);
    assert!(json_opt.is_some());
    let json_str = json_opt.unwrap();
    let parsed: serde_json::Value = serde_json::from_str(&json_str).unwrap();
    assert_eq!(parsed["id"].as_str().unwrap(), tab_id.as_ref());
    assert_eq!(
        parsed["url"].as_str().unwrap(),
        &format!("https://example.com/2")
    );
    assert_eq!(parsed["title"].as_str().unwrap(), "New Tab");
    assert!(parsed["isPinned"].is_boolean());
    assert!(parsed["scrollPosition"]["x"].is_number());
    assert!(parsed["scrollPosition"]["y"].is_number());
}

#[test]
fn test_get_tab_snapshot_by_id_not_found() {
    let (core, _) = setup_core_with_tabs(5);
    let json_opt = core.get_tab_snapshot_json(&TabId::new("nonexistent"));
    assert!(json_opt.is_none());
}
