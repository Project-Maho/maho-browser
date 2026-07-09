use maho_core::maho_core::MahoCore;

#[test]
fn test_filter_decision() {
    let mut core = MahoCore::new();
    core.add_filter_list(
        "test".to_string(),
        "Test".to_string(),
        "https://example.com/list.txt".to_string(),
    );
    core.update_filter_list_content("test", "||ads.example.com^".to_string());
    core.rebuild_content_rules();
    
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
