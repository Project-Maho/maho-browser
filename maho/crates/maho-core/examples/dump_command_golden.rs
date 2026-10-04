use maho_core::command_bar::CommandBarEngine;
use maho_types::common::DateTime;
use maho_types::identifiers::{FolderId, SpaceId, TabId};
use maho_types::space::SpaceColor;
use maho_types::tab::TabRole;
use maho_types::traits::shell_renderer::{
    FolderViewModel, SearchContext, SpaceViewModel, SuggestionType, TabViewModel,
};
use serde::Serialize;
use std::env;
use std::fs::File;
use std::io::Write;

#[derive(Serialize)]
struct GoldenOutput {
    schema: String,
    results: Vec<QueryResult>,
}

#[derive(Serialize)]
struct QueryResult {
    query: String,
    results: Vec<GoldenSuggestion>,
}

#[derive(Serialize)]
struct GoldenSuggestion {
    query: String,
    index: usize,
    kind: SuggestionType,
    key: String,
    title: String,
    subtitle: Option<String>,
    execution_payload: Option<String>,
    relevance_score_f64_bits: u64,
    match_ranges: Option<Vec<(usize, usize)>>,
    shortcut: Option<String>,
    icon_sha256: Option<String>,
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let mut fixture = None;
    let mut output_path = None;

    let mut i = 1;
    while i < args.len() {
        if args[i] == "--fixture" {
            fixture = Some(args[i + 1].clone());
            i += 2;
        } else if args[i] == "--output" {
            output_path = Some(args[i + 1].clone());
            i += 2;
        } else {
            i += 1;
        }
    }

    let fixture = fixture.expect("Missing --fixture");
    let output_path = output_path.expect("Missing --output");

    if fixture != "normal-v1" {
        panic!("Unsupported fixture: {}", fixture);
    }

    let mut engine = CommandBarEngine::new();
    engine.add_history_entry(
        "https://history.test/".to_string(),
        "Maho History".to_string(),
        None,
    );
    engine.add_action(
        "close_tab".to_string(),
        "Close Tab".to_string(),
        "Tabs".to_string(),
    );
    engine.add_action(
        "toggle_sidebar".to_string(),
        "Toggle Sidebar".to_string(),
        "View".to_string(),
    );

    let space_normal = SpaceId::new("space-normal-v1");
    let t_now = DateTime::from_iso("2024-01-01T00:00:00Z");

    let tab_a = TabViewModel {
        id: TabId::new("tab-a"),
        space_id: space_normal.clone(),
        title: "Maho Alpha".to_string(),
        custom_title: None,
        custom_icon: None,
        pinned_url: None,
        url: "https://example.test/a".to_string(),
        favicon: None,
        is_loading: false,
        is_pinned: false,
        is_favorite: false,
        favorite_order: None,
        is_muted: false,
        is_playing_audio: false,
        lifecycle_state: "active".to_string(),
        children: vec![],
        created_at: t_now.clone(),
        last_active_at: t_now.clone(),
        role: TabRole::Normal,
        is_private: false,
    };

    let tab_b = TabViewModel {
        id: TabId::new("tab-b"),
        space_id: space_normal.clone(),
        title: "비공개 탭".to_string(),
        custom_title: None,
        custom_icon: None,
        pinned_url: None,
        url: "https://example.test/b".to_string(),
        favicon: None,
        is_loading: false,
        is_pinned: false,
        is_favorite: false,
        favorite_order: None,
        is_muted: false,
        is_playing_audio: false,
        lifecycle_state: "background".to_string(),
        children: vec![],
        created_at: t_now.clone(),
        last_active_at: t_now.clone(),
        role: TabRole::Normal,
        is_private: false,
    };

    let space_vm = SpaceViewModel {
        id: space_normal.clone(),
        name: "Normal".to_string(),
        color: SpaceColor {
            hue: 220.0,
            saturation: 0.4,
            brightness: 0.8,
            grain: 0.0,
        },
        theme: None,
        tab_count: 2,
        is_active: true,
        icon: None,
        profile_id: None,
        profile_name: None,
        order_index: None,
    };

    let folder_vm = FolderViewModel {
        id: FolderId::new("folder-normal-v1"),
        name: "Normal Folder".to_string(),
        tab_count: 1,
        is_expanded: true,
        tab_ids: vec![TabId::new("tab-b")],
        is_pinned: false,
        parent_folder_id: None,
        provider_type: None,
        config_json: None,
    };

    let mut tab_archived = tab_a.clone();
    tab_archived.id = TabId::new("tab-archived-v1");
    tab_archived.title = "Archived Maho".to_string();
    tab_archived.url = "https://archived.test/".to_string();
    tab_archived.lifecycle_state = "archived".to_string();

    let mut tab_closed = tab_a.clone();
    tab_closed.id = TabId::new("tab-closed-v1");
    tab_closed.title = "Closed Maho".to_string();
    tab_closed.url = "https://closed.test/".to_string();
    tab_closed.lifecycle_state = "closed".to_string();

    let context = SearchContext {
        tabs: vec![tab_a, tab_b],
        spaces: vec![space_vm],
        folders: vec![folder_vm],
        archived_tabs: vec![tab_archived],
        bookmarks: vec![(
            "bookmark-v1".to_string(),
            "Maho Bookmark".to_string(),
            "https://bookmark.test/".to_string(),
        )],
        bookmark_favicons: vec![],
        closed_tabs: vec![tab_closed],
        extensions: vec![("extension-v1".to_string(), "Maho Extension".to_string())],
        is_incognito: false,
        recent_tabs: vec![],
    };

    let queries = vec!["", "maho", "https://example.test/a", "close", "비공개 탭"];
    let mut query_results = Vec::new();

    for q in queries {
        let raw_results = engine.search(q, None, &context);
        let golden_suggs: Vec<GoldenSuggestion> = raw_results
            .into_iter()
            .enumerate()
            .map(|(idx, sugg)| GoldenSuggestion {
                query: q.to_string(),
                index: idx,
                kind: sugg.kind,
                key: sugg.key,
                title: sugg.title,
                subtitle: sugg.subtitle,
                execution_payload: sugg.execution_payload,
                relevance_score_f64_bits: sugg.relevance_score.to_bits(),
                match_ranges: sugg.match_ranges,
                shortcut: sugg.shortcut,
                icon_sha256: None,
            })
            .collect();
        query_results.push(QueryResult {
            query: q.to_string(),
            results: golden_suggs,
        });
    }

    let output = GoldenOutput {
        schema: "normal-command-golden-v1".to_string(),
        results: query_results,
    };

    let json_bytes = serde_json::to_vec_pretty(&output).unwrap();
    let mut file = File::create(output_path).unwrap();
    file.write_all(&json_bytes).unwrap();
    file.write_all(b"\n").unwrap();
}
