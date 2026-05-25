use maho_core::command_bar::CommandBarEngine;
use maho_types::identifiers::SpaceId;
use maho_types::traits::shell_renderer::{SearchContext, SuggestionType, SuggestionViewModel, TabViewModel};

fn make_tab(id: &str, title: &str, url: &str) -> TabViewModel {
    TabViewModel {
        id: maho_types::identifiers::TabId::new(id),
        space_id: SpaceId::new("space-test"),
        title: title.to_string(),
        custom_title: None,
        url: url.to_string(),
        favicon: None,
        is_loading: false,
        is_pinned: false,
        is_favorite: false,
        favorite_order: None,
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

    assert!(!results.is_empty(), "should have at least one result for '2 + 2'");
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
    ctx.tabs = vec![make_tab("t-nav", "Navigate 1 km to mile", "https://navigate.example.com")];

    let results = engine.search("1 km to mile", None, &ctx);

    let unit_pos = pos_by_kind(&results, SuggestionType::UnitConversion);
    let nav_pos = pos_by_kind(&results, SuggestionType::Navigation);

    assert!(unit_pos.is_some(), "unit conversion must be present for '1 km to mile'");
    if let (Some(u), Some(n)) = (unit_pos, nav_pos) {
        assert!(u < n, "unit conversion (pos {u}, score 2.8) must rank above navigation (pos {n}, score 2.0)");
    }
}

#[test]
fn prefix_match_ranks_above_substring_match() {
    let engine = CommandBarEngine::new();
    let mut ctx = empty_ctx();
    ctx.tabs = vec![
        make_tab("prefix", "rust programming language", "https://prefix.example.com"),
        make_tab("substring", "the great rust programming", "https://sub.example.com"),
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
    assert!(hist_pos.is_some(), "history entry must appear via full-scan (query < 3 chars)");
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
    let archived_pos = pos_by_key(&results, "archived:archived-tiebreak").expect("archived tab must appear");

    assert!(tab_pos < bm_pos, "Tab (type-priority 0, pos {tab_pos}) must precede Bookmark (priority 1, pos {bm_pos})");
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

    assert!(!results.is_empty(), "must have results for URL query in newTab mode");
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
        make_tab("high-freq", "frequency boost test page", "https://hi.example.com/freq"),
        make_tab("low-freq", "frequency boost test page", "https://lo.example.com/freq"),
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
    engine.add_action("new_tab_action".to_string(), "New Tab".to_string(), "Navigation".to_string());
    engine.add_action("new_window_action".to_string(), "New Window".to_string(), "Navigation".to_string());

    let ctx = empty_ctx();
    let results = engine.search("> new", None, &ctx);

    assert!(!results.is_empty(), "action-only mode must return actions matching 'new'");
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
    engine.add_action("action_high".to_string(), "New Tab".to_string(), "Navigation".to_string());
    engine.add_action("action_low".to_string(), "New Tab Settings".to_string(), "Navigation".to_string());
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
        assert!(!matches!(r.kind, SuggestionType::History), "history must not appear in incognito mode (key: {})", r.key);
        assert!(!matches!(r.kind, SuggestionType::Bookmark), "bookmark must not appear in incognito mode (key: {})", r.key);
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
    ctx.tabs = vec![make_tab("rust-tab", "rust programming", "https://tab.example.com/rust")];
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

    assert_eq!(keys1, keys2, "first and second run must produce identical result order");
    assert_eq!(keys2, keys3, "second and third run must produce identical result order");
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

    let closed_count = results.iter().filter(|r| matches!(r.kind, SuggestionType::ClosedTab)).count();
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
        assert!(!results.is_empty(), "results should not be empty for '{}'", query);
        assert!(
            matches!(results[0].kind, SuggestionType::Search),
            "first result should be Search for '{}'; got {:?}",
            query, results[0].kind
        );
    }
}

#[test]
fn search_suggestion_title_uses_em_dash_format() {
    let engine = CommandBarEngine::new();
    let ctx = empty_ctx();
    let query = "hello";
    let results = engine.search(query, None, &ctx);
    let search_res = results.iter().find(|r| matches!(r.kind, SuggestionType::Search)).expect("Search suggestion must exist");
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
    assert!(search_pos.is_some(), "Search suggestion should be present for URL query");
}

