use maho_core::command_bar::CommandBarEngine;
use maho_types::identifiers::SpaceId;
use maho_types::traits::shell_renderer::{
    SearchContext, SuggestionType, SuggestionViewModel, TabViewModel,
};

fn make_tab(id: &str, title: &str, url: &str) -> TabViewModel {
    TabViewModel {
        id: maho_types::identifiers::TabId::new(id),
        space_id: SpaceId::new("space-test"),
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
        role: maho_types::tab::TabRole::Normal,
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

fn pos_by_key(results: &[SuggestionViewModel], needle: &str) -> Option<usize> {
    results.iter().position(|r| r.key.contains(needle))
}

fn is_kind(r: &SuggestionViewModel, kind: &SuggestionType) -> bool {
    std::mem::discriminant(&r.kind) == std::mem::discriminant(kind)
}

fn pos_by_kind(results: &[SuggestionViewModel], kind: SuggestionType) -> Option<usize> {
    results.iter().position(|r| is_kind(r, &kind))
}

#[test]
fn calculator_result_ranks_first() {
    let engine = CommandBarEngine::new();
    let ctx = empty_ctx();
    let results = engine.search("2 + 2", None, &ctx);

    assert!(
        !results.is_empty(),
        "should have at least one result for '2 + 2'"
    );
    assert!(
        matches!(results[0].kind, SuggestionType::Calculator),
        "calculator result (score 3.0) must be ranked first; got {:?}",
        results[0].kind
    );
    assert!(
        results[0].title.contains('='),
        "calculator title must contain '='; got: {}",
        results[0].title
    );
}

#[test]
fn unit_conversion_ranks_above_navigation_when_both_present() {
    let mut engine = CommandBarEngine::new();
    engine.add_history_entry(
        "https://example.com/km".to_string(),
        "1 km to mile info page".to_string(),
        None,
    );
    let mut ctx = empty_ctx();
    ctx.tabs = vec![make_tab(
        "t-nav",
        "Navigate 1 km to mile",
        "https://navigate.example.com",
    )];

    let results = engine.search("1 km to mile", None, &ctx);

    let unit_pos = pos_by_kind(&results, SuggestionType::UnitConversion);
    let nav_pos = pos_by_kind(&results, SuggestionType::Navigation);

    assert!(
        unit_pos.is_some(),
        "unit conversion must be present for '1 km to mile'"
    );
    if let (Some(u), Some(n)) = (unit_pos, nav_pos) {
        assert!(
            u < n,
            "unit conversion (pos {u}, score 2.8) must rank above navigation (pos {n}, score 2.0)"
        );
    }
}

#[test]
fn prefix_match_ranks_above_substring_match() {
    let engine = CommandBarEngine::new();
    let mut ctx = empty_ctx();
    ctx.tabs = vec![
        make_tab(
            "prefix",
            "rust programming language",
            "https://prefix.example.com",
        ),
        make_tab(
            "substring",
            "the great rust programming",
            "https://sub.example.com",
        ),
    ];

    let results = engine.search("rust", None, &ctx);

    let prefix_pos = pos_by_key(&results, "tab:prefix");
    let sub_pos = pos_by_key(&results, "tab:substring");

    assert!(prefix_pos.is_some(), "prefix-match tab must appear");
    assert!(sub_pos.is_some(), "substring-match tab must appear");
    assert!(
        prefix_pos.unwrap() < sub_pos.unwrap(),
        "prefix match (pos {}, score 1.0) must rank above substring match (pos {}, score 0.8)",
        prefix_pos.unwrap(),
        sub_pos.unwrap()
    );
}

#[test]
fn exact_prefix_bookmark_ranks_above_fuzzy_history() {
    let mut engine = CommandBarEngine::new();
    engine.add_history_entry(
        "https://reference.site.net/guide".to_string(),
        "quick reference guide".to_string(),
        None,
    );

    let mut ctx = empty_ctx();
    ctx.bookmarks = vec![(
        "bm1".to_string(),
        "qr code tools".to_string(),
        "https://qrtools.example.com".to_string(),
    )];

    let results = engine.search("qr", None, &ctx);

    let bm_pos = pos_by_key(&results, "bookmark:bm1");
    let hist_pos = pos_by_key(&results, "history:");

    assert!(bm_pos.is_some(), "bookmark must appear");
    assert!(
        hist_pos.is_some(),
        "history entry must appear via full-scan (query < 3 chars)"
    );
    assert!(
        bm_pos.unwrap() < hist_pos.unwrap(),
        "prefix-match bookmark (pos {}, score 1.0) must rank above fuzzy history (pos {})",
        bm_pos.unwrap(),
        hist_pos.unwrap()
    );
}

#[test]
fn type_priority_tiebreak_tab_before_bookmark_before_archived() {
    let engine = CommandBarEngine::new();

    let mut ctx = empty_ctx();
    ctx.tabs = vec![make_tab(
        "t-tiebreak",
        "The pageinfo guide",
        "https://tab.example.com/pageinfo",
    )];
    ctx.bookmarks = vec![(
        "bm-tiebreak".to_string(),
        "The pageinfo wiki".to_string(),
        "https://bm.example.com/pageinfo".to_string(),
    )];
    ctx.archived_tabs = vec![make_tab(
        "archived-tiebreak",
        "The pageinfo archive",
        "https://archived.example.com/pageinfo",
    )];

    let results = engine.search("pageinfo", None, &ctx);

    let tab_pos = pos_by_key(&results, "tab:t-tiebreak").expect("tab must appear");
    let bm_pos = pos_by_key(&results, "bookmark:bm-tiebreak").expect("bookmark must appear");
    let archived_pos =
        pos_by_key(&results, "archived:archived-tiebreak").expect("archived tab must appear");

    assert!(
        tab_pos < bm_pos,
        "Tab (type-priority 0, pos {tab_pos}) must precede Bookmark (priority 1, pos {bm_pos})"
    );
    assert!(bm_pos < archived_pos, "Bookmark (type-priority 1, pos {bm_pos}) must precede ArchivedTab (priority 3, pos {archived_pos})");
}

#[test]
fn archived_tab_ranks_below_active_tab_same_score() {
    let engine = CommandBarEngine::new();
    let mut ctx = empty_ctx();
    ctx.tabs = vec![make_tab(
        "active-1",
        "archived test page equal",
        "https://active.example.com/archived",
    )];
    ctx.archived_tabs = vec![make_tab(
        "archived-1",
        "archived test page equal",
        "https://archived.example.com/archived",
    )];

    let results = engine.search("archived test", None, &ctx);

    let active_pos = pos_by_key(&results, "tab:active-1").expect("active tab must appear");
    let archived_pos = pos_by_key(&results, "archived:").expect("archived tab must appear");

    assert!(
        active_pos < archived_pos,
        "active tab (pos {active_pos}) must rank above archived tab (pos {archived_pos}, score penalized -0.2)"
    );
}

#[test]
fn closed_tab_ranks_below_active_tab_and_bookmark() {
    let engine = CommandBarEngine::new();

    let mut ctx = empty_ctx();
    ctx.tabs = vec![make_tab(
        "active-closed",
        "closedtest active tab",
        "https://active.example.com/closedtest",
    )];
    ctx.bookmarks = vec![(
        "bm-closedtest".to_string(),
        "closedtest bookmark".to_string(),
        "https://bm.example.com/closedtest".to_string(),
    )];
    ctx.closed_tabs = vec![make_tab(
        "closed-1",
        "closedtest recent tab",
        "https://closed.example.com/closedtest",
    )];

    let results = engine.search("closedtest", None, &ctx);

    let active_pos = pos_by_key(&results, "tab:active-closed").expect("active tab must appear");
    let closed_pos = pos_by_key(&results, "closed:").expect("closed tab must appear");
    let bm_pos = pos_by_key(&results, "bookmark:bm-closedtest").expect("bookmark must appear");

    assert!(active_pos < closed_pos, "active tab (type-priority 0, pos {active_pos}) must rank above closed tab (type-priority 2, pos {closed_pos})");
    assert!(bm_pos < closed_pos, "bookmark (type-priority 1, pos {bm_pos}) must rank above closed tab (type-priority 2, pos {closed_pos}) due to penalty -0.1");
}

#[test]
fn address_bar_mode_lifts_navigation_above_tab() {
    let engine = CommandBarEngine::new();
    let mut ctx = empty_ctx();
    ctx.tabs = vec![make_tab(
        "t-addr",
        "rustaceans community site",
        "https://rustaceans.example.com",
    )];

    let results = engine.search("rustaceans.example.com", Some("addressBar"), &ctx);

    let nav_pos = pos_by_kind(&results, SuggestionType::Navigation)
        .expect("navigation must be generated for URL-like query");
    if let Some(t) = pos_by_key(&results, "tab:t-addr") {
        assert!(
            nav_pos < t,
            "navigation (pos {nav_pos}, 2.0+0.5=2.5) must rank above tab (pos {t}, ~1.0) in addressBar mode"
        );
    }
}

#[test]
fn normal_mode_lifts_action_above_history() {
    let mut engine = CommandBarEngine::new();
    engine.add_history_entry(
        "https://history.example.com/action-page".to_string(),
        "action page history entry".to_string(),
        None,
    );
    engine.add_action(
        "open_action".to_string(),
        "action page action".to_string(),
        "Navigation".to_string(),
    );

    let ctx = empty_ctx();
    let results = engine.search("action page", Some("normal"), &ctx);

    let action_pos = pos_by_key(&results, "action:open_action").expect("action must appear");
    let hist_pos = pos_by_key(&results, "history:").expect("history must appear");

    assert!(
        action_pos < hist_pos,
        "action (pos {action_pos}, +0.5 in 'normal' mode) must rank above history (pos {hist_pos}, no boost)"
    );
}

#[test]
fn new_tab_mode_places_navigation_first_for_url_query() {
    let mut engine = CommandBarEngine::new();
    engine.add_history_entry(
        "https://navigate.example.com/page".to_string(),
        "navigate example page".to_string(),
        None,
    );

    let ctx = empty_ctx();
    let results = engine.search("navigate.example.com", Some("newTab"), &ctx);

    assert!(
        !results.is_empty(),
        "must have results for URL query in newTab mode"
    );
    assert!(
        matches!(results[0].kind, SuggestionType::Navigation),
        "first result must be Navigation in newTab mode for URL query (score 2.0+0.5=2.5); got {:?}",
        results[0].kind
    );
}

#[test]
fn frequency_boost_lifts_high_usage_result() {
    let mut engine = CommandBarEngine::new();
    engine.load_usage_frequencies(vec![
        ("tab:high-freq".to_string(), 50),
        ("tab:low-freq".to_string(), 0),
    ]);

    let mut ctx = empty_ctx();
    ctx.tabs = vec![
        make_tab(
            "high-freq",
            "frequency boost test page",
            "https://hi.example.com/freq",
        ),
        make_tab(
            "low-freq",
            "frequency boost test page",
            "https://lo.example.com/freq",
        ),
    ];

    let results = engine.search("frequency boost test", None, &ctx);

    let hi_pos = pos_by_key(&results, "tab:high-freq").expect("high-freq tab must appear");
    let lo_pos = pos_by_key(&results, "tab:low-freq").expect("low-freq tab must appear");

    assert!(
        hi_pos < lo_pos,
        "high-usage tab (pos {hi_pos}, freq=50, boost≈0.39) must rank above low-usage tab (pos {lo_pos}, freq=0)"
    );
}

#[test]
fn action_only_mode_returns_only_actions() {
    let mut engine = CommandBarEngine::new();
    engine.add_history_entry(
        "https://history.example.com/new".to_string(),
        "New Tab history".to_string(),
        None,
    );
    engine.add_action(
        "new_tab_action".to_string(),
        "New Tab".to_string(),
        "Navigation".to_string(),
    );
    engine.add_action(
        "new_window_action".to_string(),
        "New Window".to_string(),
        "Navigation".to_string(),
    );

    let ctx = empty_ctx();
    let results = engine.search("> new", None, &ctx);

    assert!(
        !results.is_empty(),
        "action-only mode must return actions matching 'new'"
    );
    for r in &results {
        assert!(
            matches!(r.kind, SuggestionType::Action),
            "action-only mode must return only Actions; found {:?} (key: {})",
            r.kind,
            r.key
        );
    }
}

#[test]
fn action_only_mode_respects_frequency_boost_ordering() {
    let mut engine = CommandBarEngine::new();
    engine.add_action(
        "action_high".to_string(),
        "New Tab".to_string(),
        "Navigation".to_string(),
    );
    engine.add_action(
        "action_low".to_string(),
        "New Tab Settings".to_string(),
        "Navigation".to_string(),
    );
    engine.load_usage_frequencies(vec![
        ("action:action_high".to_string(), 20),
        ("action:action_low".to_string(), 0),
    ]);

    let ctx = empty_ctx();
    let results = engine.search("> New Tab", None, &ctx);

    let high_pos = pos_by_key(&results, "action:action_high").expect("action_high must appear");
    let low_pos = pos_by_key(&results, "action:action_low").expect("action_low must appear");

    assert!(
        high_pos < low_pos,
        "higher-usage action (pos {high_pos}, freq=20) must rank above lower-usage action (pos {low_pos}, freq=0)"
    );
}

#[test]
fn incognito_mode_excludes_history_and_bookmarks_from_ranking() {
    let mut engine = CommandBarEngine::new();
    engine.add_history_entry(
        "https://history.example.com/incognito".to_string(),
        "incognito test history".to_string(),
        None,
    );

    let mut ctx = empty_ctx();
    ctx.is_incognito = true;
    ctx.tabs = vec![make_tab(
        "incognito-tab",
        "incognito test active tab",
        "https://tab.example.com/incognito",
    )];
    ctx.bookmarks = vec![(
        "incognito-bm".to_string(),
        "incognito test bookmark".to_string(),
        "https://bm.example.com/incognito".to_string(),
    )];

    let results = engine.search("incognito test", None, &ctx);

    for r in &results {
        assert!(
            !matches!(r.kind, SuggestionType::History),
            "history must not appear in incognito mode (key: {})",
            r.key
        );
        assert!(
            !matches!(r.kind, SuggestionType::Bookmark),
            "bookmark must not appear in incognito mode (key: {})",
            r.key
        );
    }
    assert!(
        pos_by_key(&results, "tab:incognito-tab").is_some(),
        "active tab must still appear in incognito mode"
    );
}

#[test]
fn golden_mixed_sources_tab_prefix_beats_bookmark_substring() {
    let mut engine = CommandBarEngine::new();
    engine.add_history_entry(
        "https://history.example.com/rustref".to_string(),
        "r u s t reference".to_string(),
        None,
    );

    let mut ctx = empty_ctx();
    ctx.tabs = vec![make_tab(
        "rust-tab",
        "rust programming",
        "https://tab.example.com/rust",
    )];
    ctx.bookmarks = vec![(
        "rust-bm".to_string(),
        "awesome rust resources".to_string(),
        "https://bm.example.com/rust".to_string(),
    )];

    let results = engine.search("rust", None, &ctx);

    assert!(!results.is_empty(), "results must not be empty for 'rust'");

    let tab_pos = pos_by_key(&results, "tab:rust-tab").expect("tab must appear");
    let bm_pos = pos_by_key(&results, "bookmark:rust-bm").expect("bookmark must appear");

    assert!(
        tab_pos < bm_pos,
        "golden order: tab (prefix match, pos {tab_pos}) must precede bookmark (substring match, pos {bm_pos})"
    );
}

#[test]
fn golden_address_bar_navigation_first_then_tab() {
    let mut engine = CommandBarEngine::new();
    engine.add_history_entry(
        "https://docs.example.com/path".to_string(),
        "docs.example.com documentation".to_string(),
        None,
    );

    let mut ctx = empty_ctx();
    ctx.tabs = vec![make_tab(
        "docs-tab",
        "docs.example.com guide",
        "https://docs.example.com/guide",
    )];

    let results = engine.search("docs.example.com", Some("addressBar"), &ctx);

    assert!(!results.is_empty(), "must have results");

    let nav_pos = pos_by_kind(&results, SuggestionType::Navigation)
        .expect("navigation must be present for URL query in addressBar mode");
    let tab_pos = pos_by_key(&results, "tab:docs-tab").expect("tab must appear");

    assert!(
        nav_pos < tab_pos,
        "golden addressBar order: navigation (pos {nav_pos}, score 2.5) must precede tab (pos {tab_pos}, score ~1.0)"
    );
}

#[test]
fn ranking_is_stable_across_repeated_identical_queries() {
    let mut engine = CommandBarEngine::new();
    for i in 0..10 {
        engine.add_history_entry(
            format!("https://stable{}.example.com", i),
            format!("stable test page number {}", i),
            None,
        );
    }

    let mut ctx = empty_ctx();
    for i in 0..5 {
        ctx.tabs.push(make_tab(
            &format!("stable-tab-{}", i),
            &format!("stable test tab {}", i),
            &format!("https://stable-tab{}.example.com", i),
        ));
    }

    let run1 = engine.search("stable test", None, &ctx);
    let run2 = engine.search("stable test", None, &ctx);
    let run3 = engine.search("stable test", None, &ctx);

    let keys1: Vec<&str> = run1.iter().map(|r| r.key.as_str()).collect();
    let keys2: Vec<&str> = run2.iter().map(|r| r.key.as_str()).collect();
    let keys3: Vec<&str> = run3.iter().map(|r| r.key.as_str()).collect();

    assert_eq!(
        keys1, keys2,
        "first and second run must produce identical result order"
    );
    assert_eq!(
        keys2, keys3,
        "second and third run must produce identical result order"
    );
}

#[test]
fn closed_tab_limit_does_not_crowd_out_other_types() {
    let engine = CommandBarEngine::new();
    let mut ctx = empty_ctx();
    for i in 0..5 {
        ctx.closed_tabs.push(make_tab(
            &format!("closed-{}", i),
            &format!("closed limit test page {}", i),
            &format!("https://closed{}.example.com/limit", i),
        ));
    }
    ctx.tabs.push(make_tab(
        "active-limit",
        "closed limit test active",
        "https://active.example.com/limit",
    ));

    let results = engine.search("closed limit test", None, &ctx);

    let closed_count = results
        .iter()
        .filter(|r| matches!(r.kind, SuggestionType::ClosedTab))
        .count();
    assert!(
        closed_count <= 3,
        "at most 3 closed-tab results may appear; got {closed_count}"
    );
    assert!(
        pos_by_key(&results, "tab:active-limit").is_some(),
        "active tab must appear alongside closed tabs"
    );
}

#[test]
fn search_suggestion_first_for_non_url_query() {
    let engine = CommandBarEngine::new();
    let ctx = empty_ctx();
    for query in &["naver", "ㅇㅇ", "hello world"] {
        let results = engine.search(query, None, &ctx);
        assert!(
            !results.is_empty(),
            "results should not be empty for '{}'",
            query
        );
        assert!(
            matches!(results[0].kind, SuggestionType::Search),
            "first result should be Search for '{}'; got {:?}",
            query,
            results[0].kind
        );
    }
}

#[test]
fn search_suggestion_title_uses_em_dash_format() {
    let engine = CommandBarEngine::new();
    let ctx = empty_ctx();
    let query = "hello";
    let results = engine.search(query, None, &ctx);
    let search_res = results
        .iter()
        .find(|r| matches!(r.kind, SuggestionType::Search))
        .expect("Search suggestion must exist");
    assert_eq!(search_res.title, "hello — Search with Google");
    assert!(search_res.subtitle.is_none());
}

#[test]
fn search_suggestion_still_emitted_for_url_query() {
    let engine = CommandBarEngine::new();
    let ctx = empty_ctx();
    let query = "naver.com";
    let results = engine.search(query, None, &ctx);
    let search_pos = pos_by_kind(&results, SuggestionType::Search);
    assert!(
        search_pos.is_some(),
        "Search suggestion should be present for URL query"
    );
}

#[test]
fn empty_query_yields_no_action_suggestions() {
    let mut engine = CommandBarEngine::new();
    engine.add_action(
        "mute_tab".to_string(),
        "Mute Tab".to_string(),
        "Tab Actions".to_string(),
    );

    let ctx = empty_ctx();
    let results = engine.search("", None, &ctx);

    assert!(
        pos_by_kind(&results, SuggestionType::Action).is_none(),
        "empty query must not surface any Action suggestions; got keys: {:?}",
        results.iter().map(|r| r.key.as_str()).collect::<Vec<_>>()
    );
}

#[test]
fn empty_query_returns_recent_tabs_as_switch_to_tab() {
    let engine = CommandBarEngine::new();
    let mut ctx = empty_ctx();
    ctx.recent_tabs = vec![
        make_tab("recent-1", "First", "https://first.test/"),
        make_tab("recent-2", "Second", "https://second.test/"),
        make_tab("recent-3", "Third", "https://third.test/"),
    ];

    let results = engine.search("", None, &ctx);

    assert_eq!(results.len(), 3);
    assert!(results.iter().all(|r| is_kind(r, &SuggestionType::Tab)));
    assert_eq!(
        results.iter().map(|r| r.key.as_str()).collect::<Vec<_>>(),
        vec!["tab:recent-1", "tab:recent-2", "tab:recent-3"]
    );
    assert!(results.iter().all(|r| r.is_suspended));
    assert_eq!(results[0].tab_core_id.as_deref(), Some("recent-1"));
    assert_eq!(
        results[0].execution_payload.as_deref(),
        Some("https://first.test/")
    );
    assert!(results[0].relevance_score > results[1].relevance_score);
    assert!(results[1].relevance_score > results[2].relevance_score);
}

#[test]
fn empty_query_with_no_recent_tabs_returns_empty() {
    let engine = CommandBarEngine::new();
    let ctx = empty_ctx();
    let results = engine.search("", None, &ctx);
    assert!(results.is_empty());
}

#[test]
fn empty_query_dedups_recent_tabs_against_live_tabs() {
    let engine = CommandBarEngine::new();
    let mut ctx = empty_ctx();
    ctx.tabs = vec![
        make_tab("recent-1", "Live One", "https://live-one.test/"),
        make_tab("live-2", "Live Two", "https://second.test/"),
    ];
    ctx.recent_tabs = vec![
        make_tab("recent-1", "First", "https://first.test/"),
        make_tab("recent-2", "Second", "https://second.test"),
        make_tab("recent-3", "Third", "https://third.test/"),
    ];

    let results = engine.search("", None, &ctx);
    let keys: Vec<&str> = results.iter().map(|r| r.key.as_str()).collect();
    assert_eq!(keys, vec!["tab:recent-3"]);
}

#[test]
fn text_match_surfaces_action_inline() {
    let mut engine = CommandBarEngine::new();
    engine.add_action(
        "mute_tab".to_string(),
        "Mute Tab".to_string(),
        "Tab Actions".to_string(),
    );

    let ctx = empty_ctx();
    let results = engine.search("mute", None, &ctx);

    assert!(
        pos_by_key(&results, "action:mute_tab").is_some(),
        "text match 'mute' must surface the mute_tab action inline (score > 0.3 threshold); got keys: {:?}",
        results.iter().map(|r| r.key.as_str()).collect::<Vec<_>>()
    );
}

#[test]
fn incognito_search_builds_only_ephemeral_sources() {
    let mut engine = CommandBarEngine::new();
    engine.add_history_entry(
        "https://history.test/".to_string(),
        "Maho History".to_string(),
        None,
    );
    engine.add_action(
        "mute_tab".to_string(),
        "Mute Tab".to_string(),
        "Tabs".to_string(),
    );

    let mut ctx = empty_ctx();
    ctx.is_incognito = true;
    ctx.tabs = vec![
        make_tab("tab-a", "Maho Alpha", "https://example.test/a"),
        make_tab("tab-b", "비공개 탭", "https://example.test/b"),
    ];
    ctx.spaces = vec![maho_types::traits::shell_renderer::SpaceViewModel {
        id: SpaceId::new("space-normal-v1"),
        name: "Normal".to_string(),
        color: maho_types::space::SpaceColor {
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
    }];
    ctx.folders = vec![maho_types::traits::shell_renderer::FolderViewModel {
        id: maho_types::identifiers::FolderId::new("folder-normal-v1"),
        name: "Normal Folder".to_string(),
        tab_count: 1,
        is_expanded: true,
        tab_ids: vec![maho_types::identifiers::TabId::new("tab-b")],
        is_pinned: false,
        parent_folder_id: None,
        provider_type: None,
        config_json: None,
    }];
    ctx.archived_tabs = vec![make_tab(
        "tab-archived-v1",
        "Archived Maho",
        "https://archived.test/",
    )];
    ctx.bookmarks = vec![(
        "bookmark-v1".to_string(),
        "Maho Bookmark".to_string(),
        "https://bookmark.test/".to_string(),
    )];
    ctx.closed_tabs = vec![make_tab(
        "tab-closed-v1",
        "Closed Maho",
        "https://closed.test/",
    )];
    ctx.extensions = vec![("extension-v1".to_string(), "Maho Extension".to_string())];

    let results = engine.search("maho", None, &ctx);

    for r in &results {
        assert!(
            !matches!(r.kind, SuggestionType::History),
            "History should be forbidden"
        );
        assert!(
            !matches!(r.kind, SuggestionType::Bookmark),
            "Bookmark should be forbidden"
        );
        assert!(
            !matches!(r.kind, SuggestionType::Folder),
            "Folder should be forbidden"
        );
        assert!(
            !matches!(r.kind, SuggestionType::ArchivedTab),
            "ArchivedTab should be forbidden"
        );
        assert!(
            !matches!(r.kind, SuggestionType::ClosedTab),
            "ClosedTab should be forbidden"
        );

        if matches!(r.kind, SuggestionType::Action) {
            assert!(
                r.key != "action:mute_tab",
                "Forbidden action mute_tab returned"
            );
            assert!(
                r.key != "action:open_space:space-normal-v1",
                "Forbidden space action returned"
            );
            assert!(
                r.key != "extension:extension-v1",
                "Forbidden extension action returned"
            );
        }
    }
}

#[test]
fn incognito_action_catalog_is_exact_ordered_existing_fields() {
    let engine = CommandBarEngine::new();
    let mut ctx = empty_ctx();
    ctx.is_incognito = true;

    let results = engine.search(">", None, &ctx);

    let expected_actions = vec![
        "action:close_tab",
        "action:reload_tab",
        "action:hard_reload",
        "action:copy_url",
        "action:toggle_sidebar",
        "action:toggle_split_view",
        "action:zoom_in",
        "action:zoom_out",
        "action:reset_zoom",
        "action:find_in_page",
        "action:view_source",
        "action:toggle_dev_tools",
        "action:print_page",
    ];

    let actual_keys: Vec<String> = results.iter().map(|r| r.key.clone()).collect();
    assert_eq!(
        actual_keys, expected_actions,
        "Incognito action catalog must match exact ordered ephemeral catalog"
    );
}

#[test]
fn normal_search_fixture_matches_preedit_sha() {
    use std::fs;
    use std::path::PathBuf;

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
    let t_now = maho_types::common::DateTime::from_iso("2024-01-01T00:00:00Z");

    let tab_a = TabViewModel {
        id: maho_types::identifiers::TabId::new("tab-a"),
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
        role: maho_types::tab::TabRole::Normal,
        is_private: false,
    };

    let tab_b = TabViewModel {
        id: maho_types::identifiers::TabId::new("tab-b"),
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
        role: maho_types::tab::TabRole::Normal,
        is_private: false,
    };

    let space_vm = maho_types::traits::shell_renderer::SpaceViewModel {
        id: space_normal.clone(),
        name: "Normal".to_string(),
        color: maho_types::space::SpaceColor {
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

    let folder_vm = maho_types::traits::shell_renderer::FolderViewModel {
        id: maho_types::identifiers::FolderId::new("folder-normal-v1"),
        name: "Normal Folder".to_string(),
        tab_count: 1,
        is_expanded: true,
        tab_ids: vec![maho_types::identifiers::TabId::new("tab-b")],
        is_pinned: false,
        parent_folder_id: None,
        provider_type: None,
        config_json: None,
    };

    let mut tab_archived = tab_a.clone();
    tab_archived.id = maho_types::identifiers::TabId::new("tab-archived-v1");
    tab_archived.title = "Archived Maho".to_string();
    tab_archived.url = "https://archived.test/".to_string();
    tab_archived.lifecycle_state = "archived".to_string();

    let mut tab_closed = tab_a.clone();
    tab_closed.id = maho_types::identifiers::TabId::new("tab-closed-v1");
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

    #[derive(serde::Serialize)]
    struct GoldenSuggestion {
        query: String,
        index: usize,
        kind: SuggestionType,
        key: String,
        title: String,
        subtitle: Option<String>,
        execution_payload: Option<String>,
        relevance_score_f64_bits: u64,
        #[serde(serialize_with = "serialize_match_ranges")]
        match_ranges: Option<Vec<(usize, usize)>>,
        shortcut: Option<String>,
        icon_sha256: Option<String>,
    }

    fn serialize_match_ranges<S>(
        ranges: &Option<Vec<(usize, usize)>>,
        serializer: S,
    ) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        match ranges {
            Some(vec) => {
                use serde::ser::SerializeSeq;
                let mut seq = serializer.serialize_seq(Some(vec.len()))?;
                for &(start, end) in vec {
                    seq.serialize_element(&vec![start, end])?;
                }
                seq.end()
            }
            None => serializer.serialize_none(),
        }
    }

    #[derive(serde::Serialize)]
    struct QueryResult {
        query: String,
        results: Vec<GoldenSuggestion>,
    }

    #[derive(serde::Serialize)]
    struct GoldenOutput {
        schema: String,
        results: Vec<QueryResult>,
    }

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

    let current_output = GoldenOutput {
        schema: "normal-command-golden-v1".to_string(),
        results: query_results,
    };

    let mut serialized_bytes = serde_json::to_vec_pretty(&current_output).unwrap();
    serialized_bytes.push(b'\n');

    let mut golden_path = None;
    let evidence_dir = PathBuf::from("../../../.omo/evidence/arc-parity-incognito-desktop");
    if evidence_dir.exists() {
        if let Ok(entries) = fs::read_dir(evidence_dir) {
            let mut paths = Vec::new();
            for entry in entries.flatten() {
                let path = entry.path().join("task-2-tooling/command-normal-v1.json");
                if path.exists() {
                    paths.push(path);
                }
            }
            // sort by directory name or path to pick the latest
            paths.sort();
            golden_path = paths.last().cloned();
        }
    }

    let golden_path = golden_path.expect("Failed to locate command-normal-v1.json golden file");
    let golden_bytes = fs::read(golden_path).expect("Failed to read golden file");
    assert_eq!(
        serialized_bytes, golden_bytes,
        "Serialized output does not match pre-edit golden file"
    );
}
