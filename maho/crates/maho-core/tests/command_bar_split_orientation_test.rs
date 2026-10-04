use maho_core::command_bar::CommandBarEngine;
use maho_types::traits::shell_renderer::{SearchContext, SuggestionType};

fn context(is_incognito: bool) -> SearchContext {
    SearchContext {
        tabs: Vec::new(),
        spaces: Vec::new(),
        folders: Vec::new(),
        archived_tabs: Vec::new(),
        bookmarks: Vec::new(),
        bookmark_favicons: Vec::new(),
        closed_tabs: Vec::new(),
        extensions: Vec::new(),
        is_incognito,
        recent_tabs: Vec::new(),
    }
}

#[test]
fn split_orientation_actions_are_discoverable_with_exact_labels() {
    let engine = CommandBarEngine::new();
    let cases = [
        (
            ">split orientation",
            "action:toggle_split_orientation",
            "Toggle Split Orientation",
        ),
        (
            ">side by side split",
            "action:split_side_by_side",
            "Split Tab: Side-by-Side",
        ),
        (
            ">top bottom split",
            "action:split_top_bottom",
            "Split Tab: Top/Bottom",
        ),
    ];

    for (query, expected_key, expected_title) in cases {
        let actions: Vec<_> = engine
            .search(query, None, &context(false))
            .into_iter()
            .filter(|suggestion| matches!(suggestion.kind, SuggestionType::Action))
            .collect();
        assert_eq!(actions.len(), 1, "query {query:?} should be unambiguous");
        assert_eq!(actions[0].key, expected_key);
        assert_eq!(actions[0].title, expected_title);
    }
}

#[test]
fn split_orientation_actions_are_not_added_to_incognito_ephemeral_catalog() {
    let engine = CommandBarEngine::new();
    let actions: Vec<_> = engine
        .search(">split orientation", None, &context(true))
        .into_iter()
        .filter(|suggestion| matches!(suggestion.kind, SuggestionType::Action))
        .collect();

    assert!(actions
        .iter()
        .all(|suggestion| !suggestion.key.contains("split_orientation")));
}
