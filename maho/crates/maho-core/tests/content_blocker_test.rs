use maho_core::content_blocker::{
    compile_engine_snapshot, is_local_filter_list_url, normalize_site_exception_key,
    ContentBlocker, FilterUpdateOutcome, CUSTOM_RULES_LIST_ID, MAX_LIST_BODY_BYTES,
};
use maho_types::content_blocking::{
    ContentBlockingError, ContentBlockingMode, FilterHealthStatus, FilterListUpdateResponse,
};

fn resp(list_id: &str, status: u16, body: Option<&str>) -> FilterListUpdateResponse {
    FilterListUpdateResponse {
        list_id: list_id.to_string(),
        status_code: status,
        body: body.map(str::to_string),
        etag: None,
        last_modified: None,
        sha256: None,
    }
}

#[test]
fn deferred_restore_blocks_the_first_request_and_rebuilds_invalid_cache() {
    let mut blocker = ContentBlocker::new();
    blocker.add_filter_list("lazy".into(), "Lazy".into(), "https://example.com/list".into()).unwrap();
    blocker.update_filter_list_content("lazy", "||ads.example.com^".to_string()).unwrap();
    blocker.rebuild_engine_sync();
    let hash = blocker.create_compile_snapshot().content_hash;
    let cache = blocker.serialize_engine();
    blocker.defer_engine_restore(Some(cache), hash.clone());
    assert!(blocker.should_block_request("https://ads.example.com/ad.js", "https://example.com", "script"));
    blocker.defer_engine_restore(Some(vec![0, 1, 2]), hash);
    assert!(blocker.should_block_request("https://ads.example.com/ad.js", "https://example.com", "script"));
}

#[test]
fn normalize_site_exception_key_handles_various_inputs() {
    assert_eq!(
        normalize_site_exception_key("https://www.example.co.uk:8443/path?q=1#hash"),
        "example.co.uk"
    );
    assert_eq!(
        normalize_site_exception_key("ads.example.co.uk"),
        "example.co.uk"
    );
    assert_eq!(
        normalize_site_exception_key("http://192.168.1.1:8080"),
        "192.168.1.1"
    );
    assert_eq!(normalize_site_exception_key("http://[::1]:3000"), "[::1]");
    assert_eq!(
        normalize_site_exception_key("http://localhost:5000"),
        "localhost"
    );
}

#[test]
fn mode_gating_behavior() {
    let mut cb = ContentBlocker::new();
    cb.update_filter_list_content("easylist", "||ads.example.com^\n".to_string())
        .unwrap();
    cb.rebuild_engine_sync();

    cb.set_mode(ContentBlockingMode::Native);
    assert!(cb.should_block_request(
        "https://ads.example.com/banner.js",
        "https://example.com",
        "script"
    ));

    cb.set_mode(ContentBlockingMode::Extension);
    assert!(!cb.should_block_request(
        "https://ads.example.com/banner.js",
        "https://example.com",
        "script"
    ));
    assert_eq!(cb.get_content_rules(), "[]");
    assert!(cb
        .get_cosmetic_resources("https://example.com")
        .hide_selectors
        .is_empty());

    cb.set_mode(ContentBlockingMode::Disabled);
    assert!(!cb.should_block_request(
        "https://ads.example.com/banner.js",
        "https://example.com",
        "script"
    ));
}

#[test]
fn unknown_mode_is_non_native_and_never_blocks() {
    let mut cb = ContentBlocker::new();
    cb.update_filter_list_content("easylist", "||ads.example.com^\n".to_string())
        .unwrap();
    cb.rebuild_engine_sync();
    cb.set_mode(ContentBlockingMode::Unknown);
    assert!(!cb.is_native());
    assert!(!cb.should_block_request("https://ads.example.com/x", "https://site.test", "script"));
    assert_eq!(cb.get_content_rules(), "[]");
}

#[test]
fn psl_exception_matching() {
    let mut cb = ContentBlocker::new();
    cb.update_filter_list_content("easylist", "||ads.example.co.uk^\n".to_string())
        .unwrap();
    cb.rebuild_engine_sync();

    cb.add_site_exception("https://www.example.co.uk:8443");
    assert!(!cb.should_block_request(
        "https://ads.example.co.uk/banner.js",
        "https://sub.example.co.uk",
        "script"
    ));
    assert!(cb.should_block_request(
        "https://ads.example.co.uk/banner.js",
        "https://example.net",
        "script"
    ));
}

#[test]
fn cosmetic_resources_expose_procedural_actions_and_exceptions() {
    let mut cb = ContentBlocker::new();
    cb.update_filter_list_content(
        "easylist",
        "example.com##div:has-text(Sponsored)\n##.ad-banner\nexample.com#@#.ad-banner\n"
            .to_string(),
    )
    .unwrap();
    cb.rebuild_engine_sync();
    cb.set_mode(ContentBlockingMode::Native);

    let resources = cb.get_cosmetic_resources("https://example.com/article");
    assert!(
        resources
            .procedural_actions
            .iter()
            .any(|action| action.contains("has-text") && action.contains("Sponsored")),
        "procedural :has-text rule must reach the caller, got {:?}",
        resources.procedural_actions
    );
    assert_eq!(resources.exceptions, vec![".ad-banner".to_string()]);

    let other = cb.get_cosmetic_resources("https://other.test/");
    assert!(other.procedural_actions.is_empty());
    assert!(other.exceptions.is_empty());

    let json = serde_json::to_value(&resources).expect("response serializes");
    assert!(json.get("proceduralActions").is_some());
    assert!(json.get("exceptions").is_some());
}

#[test]
fn hidden_class_id_selectors_match_generic_rules_and_honor_exceptions() {
    let mut cb = ContentBlocker::new();
    cb.update_filter_list_content(
        "easylist",
        "##.ad-banner\n###sponsor-box\nexample.com#@#.ad-banner\n".to_string(),
    )
    .unwrap();
    cb.rebuild_engine_sync();
    cb.set_mode(ContentBlockingMode::Native);

    let classes = vec!["ad-banner".to_string(), "article".to_string()];
    let ids = vec!["sponsor-box".to_string(), "main".to_string()];
    assert_eq!(
        cb.hidden_class_id_selectors(&classes, &ids, &[]),
        vec!["#sponsor-box".to_string(), ".ad-banner".to_string()]
    );

    // The URL's cosmetic exceptions remove the excepted generic rule.
    let exceptions = cb.get_cosmetic_resources("https://example.com/").exceptions;
    assert_eq!(
        cb.hidden_class_id_selectors(&classes, &ids, &exceptions),
        vec!["#sponsor-box".to_string()]
    );

    cb.set_mode(ContentBlockingMode::Extension);
    assert!(cb.hidden_class_id_selectors(&classes, &ids, &[]).is_empty());
}

#[test]
fn off_thread_compilation_and_generation_safety() {
    let mut cb = ContentBlocker::new();
    cb.update_filter_list_content("easylist", "||bad.com^\n".to_string())
        .unwrap();

    let gen1_snapshot = cb.create_compile_snapshot();
    let gen1_gen = gen1_snapshot.generation;

    cb.update_filter_list_content("easylist", "||other.com^\n".to_string())
        .unwrap();
    let gen2_gen = cb.generation();
    assert_ne!(gen1_gen, gen2_gen);

    let compiled_gen1 = compile_engine_snapshot(gen1_snapshot);
    assert!(!cb.install_compiled_engine(compiled_gen1));

    let gen2_snapshot = cb.create_compile_snapshot();
    let compiled_gen2 = compile_engine_snapshot(gen2_snapshot);
    assert!(cb.install_compiled_engine(compiled_gen2));
}

// Failing-first test #1: an HTTP 200 candidate body is not visible in active raw
// content / metadata / rules before a compiled engine for it installs.
#[test]
fn http_200_candidate_is_invisible_until_install() {
    let mut cb = ContentBlocker::new();
    cb.update_filter_list_content("easylist", "||good.example^\n".to_string())
        .unwrap();
    cb.rebuild_engine_sync();
    let good_hash = cb.active_content_hash().expect("active hash").to_string();
    let good_rules = cb.get_content_rules();

    let outcome = cb
        .apply_update_response(resp("easylist", 200, Some("||candidate.example^\n")))
        .unwrap();
    assert_eq!(outcome, FilterUpdateOutcome::CandidatePending);
    assert!(cb.has_pending_candidate("easylist"));

    assert_eq!(
        cb.filter_list_content("easylist"),
        Some("||good.example^\n")
    );
    assert_eq!(cb.active_content_hash(), Some(good_hash.as_str()));
    assert_eq!(cb.get_content_rules(), good_rules);
    assert!(!cb.should_block_request("https://candidate.example/x", "https://site.test", "script"));
    assert!(cb.should_block_request("https://good.example/x", "https://site.test", "script"));

    let snapshot = cb
        .create_candidate_compile_snapshot("easylist")
        .expect("candidate snapshot");
    let compiled = compile_engine_snapshot(snapshot);
    assert!(cb.install_compiled_engine(compiled));

    assert!(!cb.has_pending_candidate("easylist"));
    assert_eq!(
        cb.filter_list_content("easylist"),
        Some("||candidate.example^\n")
    );
    assert!(cb.should_block_request("https://candidate.example/x", "https://site.test", "script"));
    assert!(!cb.should_block_request("https://good.example/x", "https://site.test", "script"));
}

// Failing-first test #2: a stale or failed candidate never replaces the
// last-known-good engine; the serialized engine + content + hash stay identical.
#[test]
fn stale_or_failed_candidate_retains_byte_identical_last_known_good() {
    let mut cb = ContentBlocker::new();
    cb.update_filter_list_content("easylist", "||good.example^\n".to_string())
        .unwrap();
    cb.rebuild_engine_sync();
    let good_serialized = cb.serialize_engine();
    let good_hash = cb.active_content_hash().expect("active hash").to_string();

    assert_eq!(
        cb.apply_update_response(resp("easylist", 200, Some("||candidate.example^\n")))
            .unwrap(),
        FilterUpdateOutcome::CandidatePending
    );

    // Failed compile path: candidate is scheduled but never installed.
    assert_eq!(
        cb.serialize_engine(),
        good_serialized,
        "scheduling must not touch the active engine"
    );
    assert_eq!(
        cb.filter_list_content("easylist"),
        Some("||good.example^\n")
    );
    assert_eq!(cb.active_content_hash(), Some(good_hash.as_str()));
    assert!(cb.discard_pending_candidate("easylist"));

    // Stale-generation path: build a candidate, then advance the generation so
    // the compiled result is stale and cannot install.
    assert_eq!(
        cb.apply_update_response(resp("easylist", 200, Some("||candidate2.example^\n")))
            .unwrap(),
        FilterUpdateOutcome::CandidatePending
    );
    let stale_snapshot = cb
        .create_candidate_compile_snapshot("easylist")
        .expect("candidate snapshot");
    cb.add_site_exception("https://unrelated.example"); // bumps generation
    let compiled_stale = compile_engine_snapshot(stale_snapshot);
    assert!(!cb.install_compiled_engine(compiled_stale));

    assert_eq!(
        cb.serialize_engine(),
        good_serialized,
        "stale candidate must not replace the engine"
    );
    assert_eq!(
        cb.filter_list_content("easylist"),
        Some("||good.example^\n")
    );
    assert_eq!(cb.active_content_hash(), Some(good_hash.as_str()));
    assert!(cb.should_block_request("https://good.example/x", "https://site.test", "script"));
    assert!(!cb.should_block_request(
        "https://candidate2.example/x",
        "https://site.test",
        "script"
    ));
}

// Failing-first test #3: identical content does not schedule a rebuild; a 304
// only refreshes health/validators.
#[test]
fn identical_content_and_304_do_not_schedule_rebuild() {
    let mut cb = ContentBlocker::new();
    cb.update_filter_list_content("easylist", "||good.example^\n".to_string())
        .unwrap();
    cb.rebuild_engine_sync();

    let mut identical = resp("easylist", 200, Some("||good.example^\n"));
    identical.etag = Some("\"v1\"".to_string());
    assert_eq!(
        cb.apply_update_response(identical).unwrap(),
        FilterUpdateOutcome::Unchanged
    );
    assert!(!cb.has_pending_candidate("easylist"));

    assert_eq!(
        cb.apply_update_response(resp("easylist", 304, None))
            .unwrap(),
        FilterUpdateOutcome::NotModified
    );
    assert!(!cb.has_pending_candidate("easylist"));
    assert_eq!(cb.health().health_status, FilterHealthStatus::Ok);
}

// A 304 can only confirm content we already hold. With no stored body the
// validators are stale, so the update must fail and drop them, forcing the
// next fetch to be unconditional instead of reporting a healthy empty list.
#[test]
fn not_modified_without_stored_body_fails_and_drops_validators() {
    let mut cb = ContentBlocker::new();
    cb.hydrate_lists(vec![(
        maho_core::content_blocker::default_seeded_filter_lists()
            .into_iter()
            .find(|list| list.id == "easylist")
            .map(|mut list| {
                list.etag = Some("\"stale\"".to_string());
                list.last_modified = Some("Mon, 01 Jan 2026 00:00:00 GMT".to_string());
                list
            })
            .unwrap(),
        String::new(),
    )]);

    assert_eq!(
        cb.apply_update_response(resp("easylist", 304, None)).unwrap(),
        FilterUpdateOutcome::Failed { status_code: 304 }
    );

    let list = cb
        .get_filter_lists()
        .iter()
        .find(|list| list.id == "easylist")
        .unwrap();
    assert_eq!(list.failure_count, 1);
    assert_eq!(list.last_status, Some(304));
    assert_eq!(list.etag, None);
    assert_eq!(list.last_modified, None);
    assert_eq!(list.last_success_timestamp, None);
    assert_ne!(cb.health().health_status, FilterHealthStatus::Ok);
}

#[test]
fn unknown_update_list_ids_return_typed_errors_without_mutation() {
    let mut cb = ContentBlocker::new();
    cb.update_filter_list_content("easylist", "||good.example^\n".to_string())
        .unwrap();
    cb.rebuild_engine_sync();
    let before_state = cb.get_state_dto();
    let before_health = cb.health().clone();

    for (status, body) in [(200, Some("||unknown.example^\n")), (304, None)] {
        let result = cb.apply_update_response(resp("unknown-list", status, body));
        assert!(matches!(
            result,
            Err(ContentBlockingError::InvalidUpdateBody { reason })
                if reason == "unknown filter list id: unknown-list"
        ));
        assert!(!cb.has_pending_candidate("unknown-list"));
        assert_eq!(cb.get_state_dto(), before_state);
        assert_eq!(cb.health(), &before_health);
    }
}

#[test]
fn candidate_boundary_rejects_hostile_and_zero_usable_rule_bodies() {
    let rejected_bodies = [
        "\u{feff}<!doctype html><html><body>gateway error</body></html>",
        "<!-- gateway error -->",
        "[Adblock Plus 2.0]\n! header only\n# comment only\n",
    ];

    for body in rejected_bodies {
        let mut blocker = ContentBlocker::new();

        let result = blocker.apply_update_response(resp("easylist", 200, Some(body)));

        assert!(
            matches!(result, Err(ContentBlockingError::InvalidUpdateBody { .. })),
            "candidate body must be rejected: {body:?}"
        );
        assert!(
            !blocker.has_pending_candidate("easylist"),
            "rejected candidate must not become pending: {body:?}"
        );
    }
}

#[test]
fn candidate_boundary_normalizes_bom_and_verifies_optional_sha256() {
    let mut blocker = ContentBlocker::new();
    let body = "\u{feff}||bom-normalized.example^\n";

    assert_eq!(
        blocker.apply_update_response(resp("easylist", 200, Some(body))),
        Ok(FilterUpdateOutcome::CandidatePending)
    );
    let compiled = compile_engine_snapshot(
        blocker
            .create_candidate_compile_snapshot("easylist")
            .expect("candidate snapshot"),
    );
    assert!(blocker.install_compiled_engine(compiled));
    assert_eq!(
        blocker.filter_list_content("easylist"),
        Some("||bom-normalized.example^\n")
    );
    assert_eq!(
        blocker
            .get_filter_lists()
            .iter()
            .find(|list| list.id == "easylist")
            .and_then(|list| list.sha256.as_deref()),
        Some("e7b73d1c77e9745381a466c6e4e0122ab7bb19258369b228e4da60766ac7642e")
    );

    let mut mismatched = resp("easylist", 200, Some("||sha-mismatch.example^\n"));
    mismatched.sha256 = Some("deadbeef".to_string());
    assert!(matches!(
        blocker.apply_update_response(mismatched),
        Err(ContentBlockingError::InvalidUpdateBody { .. })
    ));
}

// Failing-first test #8: invalid / duplicate / insecure / oversized list input
// returns a typed ContentBlockingError and does not mutate active state.
#[test]
fn invalid_list_inputs_return_typed_errors_without_mutation() {
    let mut cb = ContentBlocker::new();
    let before = cb.get_filter_lists().len();

    let insecure = cb
        .add_filter_list(
            "custom".into(),
            "Custom".into(),
            "http://insecure.example/l.txt".into(),
        )
        .unwrap_err();
    assert!(matches!(
        insecure,
        ContentBlockingError::InsecureFilterListUrl { .. }
    ));

    let not_a_url = cb
        .add_filter_list("custom".into(), "Custom".into(), "not a url".into())
        .unwrap_err();
    assert!(matches!(
        not_a_url,
        ContentBlockingError::InsecureFilterListUrl { .. }
    ));

    let duplicate = cb
        .add_filter_list(
            "easylist".into(),
            "Dup".into(),
            "https://ok.example/l.txt".into(),
        )
        .unwrap_err();
    assert!(matches!(
        duplicate,
        ContentBlockingError::DuplicateFilterListId { .. }
    ));

    assert_eq!(
        cb.get_filter_lists().len(),
        before,
        "rejected adds must not mutate the list set"
    );

    let oversized = "a".repeat(MAX_LIST_BODY_BYTES + 1);
    let content_err = cb
        .update_filter_list_content("easylist", oversized.clone())
        .unwrap_err();
    assert!(matches!(
        content_err,
        ContentBlockingError::FilterBodyTooLarge { .. }
    ));

    let response_err = cb
        .apply_update_response(resp("easylist", 200, Some(&oversized)))
        .unwrap_err();
    assert!(matches!(
        response_err,
        ContentBlockingError::FilterBodyTooLarge { .. }
    ));
    assert!(
        !cb.has_pending_candidate("easylist"),
        "oversized body must not become a candidate"
    );
}

// A 500 failure records a warning in health and retains last-known-good state.
#[test]
fn http_failure_records_health_and_retains_state() {
    let mut cb = ContentBlocker::new();
    cb.update_filter_list_content("easylist", "||good.example^\n".to_string())
        .unwrap();
    cb.rebuild_engine_sync();

    assert_eq!(
        cb.apply_update_response(resp("easylist", 500, None))
            .unwrap(),
        FilterUpdateOutcome::Failed { status_code: 500 }
    );
    assert_eq!(cb.health().health_status, FilterHealthStatus::Warning);
    assert_eq!(
        cb.filter_list_content("easylist"),
        Some("||good.example^\n")
    );
}

// select_compile_snapshot prefers a pending candidate so the off-thread compile
// produces a promotable engine; with none pending it returns the active config.
#[test]
fn select_compile_snapshot_prefers_pending_candidate_then_active() {
    let mut cb = ContentBlocker::new();
    cb.update_filter_list_content("easylist", "||good.example^\n".to_string())
        .unwrap();
    cb.rebuild_engine_sync();
    let active_hash = cb.create_compile_snapshot().content_hash;

    assert_eq!(cb.select_compile_snapshot().content_hash, active_hash);

    assert_eq!(
        cb.apply_update_response(resp("easylist", 200, Some("||candidate.example^\n")))
            .unwrap(),
        FilterUpdateOutcome::CandidatePending
    );
    let selected = cb.select_compile_snapshot();
    assert_ne!(
        selected.content_hash, active_hash,
        "a pending candidate must change the compile input"
    );
    assert!(selected
        .raw_contents
        .iter()
        .any(|c| c.contains("candidate.example")));

    let compiled = compile_engine_snapshot(selected);
    assert!(cb.install_compiled_engine(compiled));
    assert!(!cb.has_pending_candidate("easylist"));
    let post = cb.select_compile_snapshot();
    assert!(post
        .raw_contents
        .iter()
        .any(|c| c.contains("candidate.example")));
    assert!(!post.raw_contents.iter().any(|c| c.contains("good.example")));
}

fn blocker_with_list(id: &str, body: &str) -> ContentBlocker {
    let mut blocker = ContentBlocker::new();
    blocker
        .add_filter_list(
            id.into(),
            id.into(),
            format!("https://example.com/{id}.txt"),
        )
        .unwrap();
    blocker
        .update_filter_list_content(id, body.to_string())
        .unwrap();
    blocker.rebuild_engine_sync();
    blocker
}

#[test]
fn test_request_reports_matching_filter() {
    let blocker = blocker_with_list(
        "ads",
        "! comment\n||127.0.0.1/ads/\n@@||127.0.0.1/ads/ok.js\n",
    );
    let hit = blocker.test_request(
        "http://127.0.0.1/ads/banner.js",
        "https://example.com/",
        "script",
    );
    assert!(
        hit.valid_request && hit.native_mode && hit.blocked,
        "{hit:?}"
    );
    assert_eq!(hit.matched_filter.as_deref(), Some("||127.0.0.1/ads/"));
    assert_eq!(hit.list_id.as_deref(), Some("ads"));

    let allowed = blocker.test_request(
        "http://127.0.0.1/ads/ok.js",
        "https://example.com/",
        "script",
    );
    assert!(!allowed.blocked, "{allowed:?}");
    assert_eq!(
        allowed.exception.as_deref(),
        Some("@@||127.0.0.1/ads/ok.js")
    );
    assert_eq!(allowed.list_id.as_deref(), Some("ads"));

    let miss = blocker.test_request(
        "https://cdn.example.org/app.js",
        "https://example.com/",
        "script",
    );
    assert!(!miss.blocked && miss.matched_filter.is_none() && miss.list_id.is_none());

    let invalid = blocker.test_request("not a url", "https://example.com/", "script");
    assert!(!invalid.valid_request && !invalid.blocked);

    let mut excepted = blocker_with_list("ads", "||127.0.0.1/ads/\n");
    excepted.add_site_exception("example.com");
    let skipped = excepted.test_request(
        "http://127.0.0.1/ads/banner.js",
        "https://example.com/",
        "script",
    );
    assert!(skipped.site_excepted && !skipped.blocked);
}

#[test]
fn custom_rules_compile_and_block() {
    let mut blocker = ContentBlocker::new();
    let generation = blocker.generation();
    blocker
        .set_custom_rules("||tracker.example.net^\n")
        .unwrap();
    assert!(blocker.generation() > generation);
    assert_eq!(blocker.custom_rules(), "||tracker.example.net^\n");
    let list = blocker
        .get_filter_lists()
        .iter()
        .find(|list| list.id == CUSTOM_RULES_LIST_ID)
        .expect("custom pseudo-list");
    assert!(list.enabled && list.rule_count == 1 && is_local_filter_list_url(&list.url));
    blocker.rebuild_engine_sync();
    assert!(blocker.should_block_request(
        "https://tracker.example.net/p.gif",
        "https://example.com",
        "image"
    ));
    let explained = blocker.test_request(
        "https://tracker.example.net/p.gif",
        "https://example.com",
        "image",
    );
    assert_eq!(explained.list_id.as_deref(), Some(CUSTOM_RULES_LIST_ID));

    // A rejected save keeps the previous rules (atomic).
    let before = blocker.generation();
    assert!(blocker.set_custom_rules("<html>not rules</html>").is_err());
    assert_eq!(blocker.custom_rules(), "||tracker.example.net^\n");
    assert_eq!(blocker.generation(), before);

    // Custom rules are never updated from the network.
    let err = blocker
        .apply_update_response(resp(CUSTOM_RULES_LIST_ID, 200, Some("||evil.example^")))
        .unwrap_err();
    assert!(matches!(
        err,
        ContentBlockingError::InvalidUpdateBody { .. }
    ));
    assert_eq!(blocker.custom_rules(), "||tracker.example.net^\n");

    // Blank text removes the pseudo-list.
    blocker.set_custom_rules("   ").unwrap();
    assert!(blocker
        .get_filter_lists()
        .iter()
        .all(|list| list.id != CUSTOM_RULES_LIST_ID));
    blocker.rebuild_engine_sync();
    assert!(!blocker.should_block_request(
        "https://tracker.example.net/p.gif",
        "https://example.com",
        "image"
    ));
}

#[test]
fn import_local_list_validates() {
    let mut blocker = ContentBlocker::new();
    let lists_before = blocker.get_filter_lists().len();
    let generation = blocker.generation();

    for bad in [
        &b""[..],
        b"<!doctype html><html></html>",
        b"! only a comment\n",
        &[0xff, 0xfe, 0x00][..],
    ] {
        assert!(blocker.import_local_list("bad", bad).is_err(), "{bad:?}");
    }
    let oversize = vec![b'a'; MAX_LIST_BODY_BYTES + 1];
    assert!(matches!(
        blocker.import_local_list("big", &oversize),
        Err(ContentBlockingError::FilterBodyTooLarge { .. })
    ));
    assert_eq!(blocker.get_filter_lists().len(), lists_before);
    assert_eq!(blocker.generation(), generation);

    let id = blocker
        .import_local_list("  My list  ", b"\xef\xbb\xbf||local-ads.example^\n")
        .unwrap();
    assert!(id.starts_with("local-"));
    let list = blocker
        .get_filter_lists()
        .iter()
        .find(|list| list.id == id)
        .unwrap();
    assert_eq!(list.name, "My list");
    assert!(is_local_filter_list_url(&list.url) && list.enabled && list.rule_count == 1);
    blocker.rebuild_engine_sync();
    assert!(blocker.should_block_request(
        "https://local-ads.example/a.js",
        "https://example.com",
        "script"
    ));

    // Same bytes twice is a duplicate, not a second list.
    assert!(matches!(
        blocker.import_local_list("again", b"||local-ads.example^\n"),
        Err(ContentBlockingError::DuplicateFilterListId { .. })
    ));
    assert!(blocker
        .apply_update_response(resp(&id, 200, Some("||x.example^")))
        .is_err());
}
