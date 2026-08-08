use maho_core::content_blocker::compile_engine_snapshot;
use maho_core::maho_core::MahoCore;
use maho_types::content_blocking::FilterListUpdateResponse;

#[test]
fn test_filter_decision() {
    let mut core = MahoCore::new();
    let update = FilterListUpdateResponse {
        list_id: "easylist".to_string(),
        status_code: 200,
        body: Some("||ads.example.com^".to_string()),
        etag: None,
        last_modified: None,
        sha256: None,
    };
    assert!(core
        .apply_filter_list_update_response(update)
        .expect("candidate should be accepted"));
    assert!(!core.should_block_request(
        "https://ads.example.com/banner.js",
        "https://example.com/",
        "script"
    ));
    let snapshot = core.create_content_blocker_compile_snapshot();
    assert!(core.install_content_blocker_compiled_engine(compile_engine_snapshot(snapshot)));

    assert!(core.should_block_request(
        "https://ads.example.com/banner.js",
        "https://example.com/",
        "script"
    ));
    assert!(!core.should_block_request(
        "https://safe.example.com/page.html",
        "https://example.com/",
        "document"
    ));
}
