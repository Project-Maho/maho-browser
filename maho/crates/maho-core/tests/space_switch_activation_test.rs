use maho_core::maho_core::MahoCore;
use maho_types::common::Url;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::{ProfileId, TabId};
use maho_types::space::SpaceColor;
use maho_types::tab::TabRole;

fn make_color(hue: f64) -> SpaceColor {
    SpaceColor {
        hue,
        saturation: 0.8,
        brightness: 0.9,
        grain: 0.0,
    }
}

#[test]
fn test_activation_target_with_last_active() {
    let mut core = MahoCore::new();
    let space_id = core.get_active_space_id();

    // Create a tab and activate it
    let before: std::collections::HashSet<TabId> = core
        .get_tab_view_models()
        .iter()
        .map(|t| t.id.clone())
        .collect();
    core.handle_event(ShellEvent::CreateTab {
        space_id: space_id.clone(),
        url: Some(Url::new("https://example.com/1")),
        parent_id: None,
        tab_id: None,
        window_id: None,
        is_private: false,
    });
    let after = core.get_tab_view_models();
    let tab_id = after
        .iter()
        .find(|t| !before.contains(&t.id))
        .unwrap()
        .id
        .clone();

    // Activate the tab
    core.handle_event(ShellEvent::ActivateTab {
        tab_id: tab_id.clone(),
    });

    // Verify get_activation_target_for_space returns the active tab
    assert_eq!(
        core.get_activation_target_for_space(&space_id),
        Some(tab_id)
    );
}

#[test]
fn test_activation_target_no_history_returns_first() {
    let mut core = MahoCore::new();
    let profile_id = ProfileId::new("test-profile");
    let space = core.create_space("Test Space", make_color(120.0), profile_id);
    let space_id = space.id.clone();

    // Find the seeded Google tab ID
    let google_tab_id = core
        .get_tab_view_models()
        .into_iter()
        .find(|t| t.space_id == space_id && t.url == "https://www.google.com")
        .map(|t| t.id)
        .unwrap();

    // There is no last active tab set because we haven't sent any ActivateTab/CreateTab event.
    // So get_activation_target_for_space should fall back to the first non-archived tab in the space (Google).
    let target_tab = core.get_activation_target_for_space(&space_id);
    assert_eq!(target_tab, Some(google_tab_id));
}

#[test]
fn test_activation_target_empty_space_returns_none() {
    let mut core = MahoCore::new();
    let profile_id = ProfileId::new("test-profile-2");
    let space = core.create_space("Empty Space", make_color(180.0), profile_id);
    let space_id = space.id.clone();

    // Find all tabs belonging to this space
    let tabs_in_space: Vec<TabId> = core
        .get_tab_view_models()
        .into_iter()
        .filter(|t| t.space_id == space_id)
        .map(|t| t.id)
        .collect();

    // Delete all tabs in the space to make it truly empty
    for tab_id in tabs_in_space {
        // Change role to Normal first so closing it actually archives/deletes it.
        core.handle_event(ShellEvent::ChangeTabRole {
            tab_id: tab_id.clone(),
            new_role: TabRole::Normal,
        });
        core.handle_event(ShellEvent::CloseTab {
            tab_id,
            expected_space_id: Some(space_id.clone()),
        });
    }

    // Verify it returns None
    assert_eq!(core.get_activation_target_for_space(&space_id), None);
}
