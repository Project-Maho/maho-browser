use maho_core::content_blocker::{
    compile_engine_snapshot, ContentBlocker, FilterUpdateOutcome, MAX_ENABLED_FILTER_RULES,
};
use maho_types::content_blocking::ContentBlockingError;
use maho_types::content_blocking::FilterListUpdateResponse;

fn response(list_id: &str, body: &str) -> FilterListUpdateResponse {
    FilterListUpdateResponse {
        list_id: list_id.to_string(),
        status_code: 200,
        body: Some(body.to_string()),
        etag: None,
        last_modified: None,
        sha256: None,
    }
}

fn blocker_with_active_lists() -> ContentBlocker {
    let mut blocker = ContentBlocker::new();
    blocker
        .update_filter_list_content("easylist", "||old-ads.example^\n".to_string())
        .unwrap();
    blocker
        .update_filter_list_content("easyprivacy", "||old-tracker.example^\n".to_string())
        .unwrap();
    blocker.rebuild_engine_sync();
    blocker
}

#[test]
fn snapshot_promotes_all_current_generation_candidates() {
    let mut blocker = blocker_with_active_lists();
    assert_eq!(
        blocker
            .apply_update_response(response("easylist", "||new-ads.example^\n"))
            .unwrap(),
        FilterUpdateOutcome::CandidatePending
    );
    assert_eq!(
        blocker
            .apply_update_response(response("easyprivacy", "||new-tracker.example^\n"))
            .unwrap(),
        FilterUpdateOutcome::CandidatePending
    );

    let snapshot = blocker.select_compile_snapshot();
    assert_eq!(snapshot.candidate_ids, ["easylist", "easyprivacy"]);
    assert!(snapshot
        .raw_contents
        .iter()
        .any(|body| body.contains("new-ads.example")));
    assert!(snapshot
        .raw_contents
        .iter()
        .any(|body| body.contains("new-tracker.example")));

    assert!(blocker.install_compiled_engine(compile_engine_snapshot(snapshot)));
    assert_eq!(
        blocker.filter_list_content("easylist"),
        Some("||new-ads.example^\n")
    );
    assert_eq!(
        blocker.filter_list_content("easyprivacy"),
        Some("||new-tracker.example^\n")
    );
}

#[test]
fn aggregate_snapshot_identity_ignores_candidate_insertion_order() {
    let mut first = ContentBlocker::new();
    let mut second = ContentBlocker::new();
    first
        .apply_update_response(response("easylist", "||first-a.example^"))
        .unwrap();
    first
        .apply_update_response(response("easyprivacy", "||first-b.example^"))
        .unwrap();
    second
        .apply_update_response(response("easyprivacy", "||first-b.example^"))
        .unwrap();
    second
        .apply_update_response(response("easylist", "||first-a.example^"))
        .unwrap();

    let first_snapshot = first.select_compile_snapshot();
    let second_snapshot = second.select_compile_snapshot();
    assert_eq!(first_snapshot.candidate_ids, second_snapshot.candidate_ids);
    assert_eq!(first_snapshot.raw_contents, second_snapshot.raw_contents);
    assert_eq!(first_snapshot.content_hash, second_snapshot.content_hash);
}

#[test]
fn installing_frozen_set_preserves_later_candidate() {
    let mut blocker = blocker_with_active_lists();
    blocker
        .apply_update_response(response("easylist", "||new-ads.example^\n"))
        .unwrap();
    let frozen_snapshot = blocker.select_compile_snapshot();
    blocker
        .apply_update_response(response("easyprivacy", "||new-tracker.example^\n"))
        .unwrap();

    assert!(blocker.install_compiled_engine(compile_engine_snapshot(frozen_snapshot)));
    assert!(!blocker.has_pending_candidate("easylist"));
    assert!(blocker.has_pending_candidate("easyprivacy"));
    assert_eq!(
        blocker.filter_list_content("easyprivacy"),
        Some("||old-tracker.example^\n")
    );
}

#[test]
fn unrelated_generation_change_rebases_candidate_for_follow_up_install() {
    let mut blocker = blocker_with_active_lists();
    blocker
        .apply_update_response(response("easylist", "||new-ads.example^\n"))
        .unwrap();
    blocker.add_site_exception("https://generation-bump.example");

    let snapshot = blocker.select_compile_snapshot();
    assert_eq!(snapshot.candidate_ids, ["easylist"]);
    assert!(snapshot
        .raw_contents
        .iter()
        .any(|body| body.contains("new-ads.example")));
    assert!(blocker.install_compiled_engine(compile_engine_snapshot(snapshot)));
    assert!(!blocker.has_pending_candidate("easylist"));
    assert_eq!(
        blocker.filter_list_content("easylist"),
        Some("||new-ads.example^\n")
    );
}

#[test]
fn removing_list_discards_its_pending_candidate() {
    let mut blocker = blocker_with_active_lists();
    blocker
        .apply_update_response(response("easylist", "||obsolete.example^\n"))
        .unwrap();
    blocker
        .apply_update_response(response("easyprivacy", "||survivor.example^\n"))
        .unwrap();

    assert!(blocker.remove_filter_list("easylist"));
    assert!(!blocker.has_pending_candidate("easylist"));
    let snapshot = blocker.select_compile_snapshot();
    assert_eq!(snapshot.candidate_ids, ["easyprivacy"]);
    assert!(blocker.install_compiled_engine(compile_engine_snapshot(snapshot)));
    assert_eq!(
        blocker.filter_list_content("easyprivacy"),
        Some("||survivor.example^\n")
    );
}

#[test]
fn direct_content_replace_discards_same_list_candidate() {
    let mut blocker = blocker_with_active_lists();
    blocker
        .apply_update_response(response("easylist", "||obsolete.example^\n"))
        .unwrap();
    blocker
        .apply_update_response(response("easyprivacy", "||survivor.example^\n"))
        .unwrap();

    blocker
        .update_filter_list_content("easylist", "||explicit.example^\n".to_string())
        .unwrap();
    assert!(!blocker.has_pending_candidate("easylist"));
    let snapshot = blocker.select_compile_snapshot();
    assert_eq!(snapshot.candidate_ids, ["easyprivacy"]);
    assert!(snapshot
        .raw_contents
        .iter()
        .any(|body| body.contains("explicit.example")));
    assert!(blocker.install_compiled_engine(compile_engine_snapshot(snapshot)));
    assert_eq!(
        blocker.filter_list_content("easylist"),
        Some("||explicit.example^\n")
    );
    assert_eq!(
        blocker.filter_list_content("easyprivacy"),
        Some("||survivor.example^\n")
    );
}

#[test]
fn disabling_list_rebases_candidate_for_future_reenable() {
    let mut blocker = blocker_with_active_lists();
    blocker
        .apply_update_response(response("easylist", "||new-ads.example^\n"))
        .unwrap();

    assert!(blocker.toggle_filter_list("easylist", false));
    let snapshot = blocker.select_compile_snapshot();
    assert_eq!(snapshot.candidate_ids, ["easylist"]);
    assert!(blocker.install_compiled_engine(compile_engine_snapshot(snapshot)));
    assert_eq!(
        blocker.filter_list_content("easylist"),
        Some("||new-ads.example^\n")
    );

    assert!(blocker.toggle_filter_list("easylist", true));
    let reenabled = blocker.select_compile_snapshot();
    assert!(reenabled
        .raw_contents
        .iter()
        .any(|body| body.contains("new-ads.example")));
}

#[test]
fn stale_candidate_hash_preserves_byte_identical_last_known_good() {
    let mut blocker = blocker_with_active_lists();
    let last_known_good = blocker.serialize_engine();
    let last_known_good_hash = blocker.active_content_hash().map(str::to_string);
    blocker
        .apply_update_response(response("easylist", "||first.example^"))
        .unwrap();
    let stale_snapshot = blocker.select_compile_snapshot();
    blocker
        .apply_update_response(response("easylist", "||second.example^"))
        .unwrap();

    assert!(!blocker.install_compiled_engine(compile_engine_snapshot(stale_snapshot)));
    assert_eq!(blocker.serialize_engine(), last_known_good);
    assert_eq!(
        blocker.active_content_hash().map(str::to_string),
        last_known_good_hash
    );
    assert!(blocker.has_pending_candidate("easylist"));
}

#[test]
fn stale_generation_preserves_byte_identical_last_known_good() {
    let mut blocker = blocker_with_active_lists();
    let last_known_good = blocker.serialize_engine();
    let last_known_good_hash = blocker.active_content_hash().map(str::to_string);
    blocker
        .apply_update_response(response("easylist", "||new-ads.example^"))
        .unwrap();
    blocker
        .apply_update_response(response("easyprivacy", "||new-tracker.example^"))
        .unwrap();
    let stale_snapshot = blocker.select_compile_snapshot();
    blocker.add_site_exception("https://generation-bump.example");

    assert!(!blocker.install_compiled_engine(compile_engine_snapshot(stale_snapshot)));
    assert_eq!(blocker.serialize_engine(), last_known_good);
    assert_eq!(
        blocker.active_content_hash().map(str::to_string),
        last_known_good_hash
    );
    assert_eq!(
        blocker.filter_list_content("easylist"),
        Some("||old-ads.example^\n")
    );
    assert_eq!(
        blocker.filter_list_content("easyprivacy"),
        Some("||old-tracker.example^\n")
    );

    let follow_up_snapshot = blocker.select_compile_snapshot();
    assert_eq!(
        follow_up_snapshot.candidate_ids,
        ["easylist", "easyprivacy"]
    );
    assert!(blocker.install_compiled_engine(compile_engine_snapshot(follow_up_snapshot)));
    assert_eq!(
        blocker.filter_list_content("easylist"),
        Some("||new-ads.example^")
    );
    assert_eq!(
        blocker.filter_list_content("easyprivacy"),
        Some("||new-tracker.example^")
    );
}

#[test]
fn enabled_candidate_rules_accept_exact_aggregate_cap_and_install() {
    let mut blocker = ContentBlocker::new();
    let list_body = "||cap.example^\n".repeat(MAX_ENABLED_FILTER_RULES / 2);

    assert_eq!(
        blocker.apply_update_response(response("easylist", &list_body)),
        Ok(FilterUpdateOutcome::CandidatePending)
    );
    assert_eq!(
        blocker.apply_update_response(response("easyprivacy", &list_body)),
        Ok(FilterUpdateOutcome::CandidatePending)
    );

    let snapshot = blocker.select_compile_snapshot();
    assert!(blocker.install_compiled_engine(compile_engine_snapshot(snapshot)));
    assert_eq!(blocker.get_rule_count(), MAX_ENABLED_FILTER_RULES);
}

#[test]
fn enabled_candidate_rules_reject_one_rule_over_aggregate_cap() {
    let mut blocker = ContentBlocker::new();
    let first_body = "||cap-first.example^\n".repeat(MAX_ENABLED_FILTER_RULES / 2);
    let second_body = "||cap-second.example^\n".repeat(MAX_ENABLED_FILTER_RULES / 2 + 1);

    assert_eq!(
        blocker.apply_update_response(response("easylist", &first_body)),
        Ok(FilterUpdateOutcome::CandidatePending)
    );
    assert!(matches!(
        blocker.apply_update_response(response("easyprivacy", &second_body)),
        Err(ContentBlockingError::InvalidUpdateBody { .. })
    ));
    assert!(blocker.has_pending_candidate("easylist"));
    assert!(!blocker.has_pending_candidate("easyprivacy"));
}

// The default seeded set enables EasyList and EasyPrivacy together; they were
// measured at ~81.7k and ~56.2k rules on 2026-09-27. A 100k aggregate cap made
// that pair impossible: whichever list lost the fetch race was rejected on
// every attempt and stayed permanently at zero rules, which left profiles
// that lost EasyList with no ad blocking at all. Both default-sized lists must
// therefore be accepted in the same generation.
#[test]
fn default_seeded_list_pair_fits_the_aggregate_rule_budget() {
    const DEFAULT_EASYLIST_RULES: usize = 82_000;
    const DEFAULT_EASYPRIVACY_RULES: usize = 56_000;

    let mut blocker = ContentBlocker::new();
    let easylist = "||default-ads.example^\n".repeat(DEFAULT_EASYLIST_RULES);
    let easyprivacy = "||default-tracker.example^\n".repeat(DEFAULT_EASYPRIVACY_RULES);

    assert_eq!(
        blocker.apply_update_response(response("easylist", &easylist)),
        Ok(FilterUpdateOutcome::CandidatePending)
    );
    assert_eq!(
        blocker.apply_update_response(response("easyprivacy", &easyprivacy)),
        Ok(FilterUpdateOutcome::CandidatePending)
    );
    assert!(blocker.has_pending_candidate("easylist"));
    assert!(blocker.has_pending_candidate("easyprivacy"));
}
