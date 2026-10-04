use maho_core::content_blocker::{compile_engine_snapshot, ContentBlocker, FilterUpdateOutcome};
use maho_types::content_blocking::FilterListUpdateResponse;

const CHURN_ROUNDS: usize = 64;

fn candidate_response(body: String) -> FilterListUpdateResponse {
    FilterListUpdateResponse {
        list_id: "easylist".to_string(),
        status_code: 200,
        body: Some(body),
        etag: None,
        last_modified: None,
        sha256: None,
    }
}

#[test]
fn generation_churn_rejects_out_of_order_snapshots_and_retains_last_known_good() {
    // Given: a compiled last-known-good engine and a fixed amount of candidate churn.
    let mut blocker = ContentBlocker::new();
    blocker
        .update_filter_list_content("easylist", "||stable.example^\n".to_string())
        .expect("seed content must be valid");
    blocker.rebuild_engine_sync();
    let last_known_good_engine = blocker.serialize_engine();
    let last_known_good_hash = blocker.active_content_hash().map(str::to_string);
    let initial_generation = blocker.generation();

    let mut completed_snapshots = Vec::with_capacity(CHURN_ROUNDS);
    for round in 0..CHURN_ROUNDS {
        let candidate_body = format!("||candidate-{round}.invalid^\n");
        assert_eq!(
            blocker
                .apply_update_response(candidate_response(candidate_body))
                .expect("candidate response must be accepted"),
            FilterUpdateOutcome::CandidatePending
        );
        completed_snapshots.push(compile_engine_snapshot(blocker.select_compile_snapshot()));
        let next_generation = blocker.generation() + 1;
        blocker.set_generation(next_generation);
    }

    // When: completions arrive in reverse creation order after their generations are stale.
    for completed in completed_snapshots.into_iter().rev() {
        assert!(!blocker.install_compiled_engine(completed));
    }

    // Then: every stale completion preserves the byte-identical LKG state.
    assert_eq!(blocker.serialize_engine(), last_known_good_engine);
    assert_eq!(
        blocker.active_content_hash().map(str::to_string),
        last_known_good_hash
    );
    assert_eq!(
        blocker.filter_list_content("easylist"),
        Some("||stable.example^\n")
    );
    assert_eq!(
        blocker.generation(),
        initial_generation + u64::try_from(CHURN_ROUNDS).expect("round count fits u64")
    );

    // And: the final rebased candidate remains installable without retry timing.
    let final_candidate = format!("||candidate-{}.invalid^\n", CHURN_ROUNDS - 1);
    let final_snapshot = blocker.select_compile_snapshot();
    assert!(blocker.install_compiled_engine(compile_engine_snapshot(final_snapshot)));
    assert_eq!(
        blocker.filter_list_content("easylist"),
        Some(final_candidate.as_str())
    );
}
