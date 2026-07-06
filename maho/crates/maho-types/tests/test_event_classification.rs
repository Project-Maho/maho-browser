use maho_types::ShellEventKind;

#[test]
fn test_event_kind_classification() {
    assert_eq!(ShellEventKind::classify("create_tab"), ShellEventKind::Structural);
    assert_eq!(ShellEventKind::classify("close_tab"), ShellEventKind::Structural);
    assert_eq!(ShellEventKind::classify("reorder_tab"), ShellEventKind::Structural);
    assert_eq!(ShellEventKind::classify("move_tab_to_folder"), ShellEventKind::Structural);
    assert_eq!(ShellEventKind::classify("pin_tab"), ShellEventKind::Structural);
    assert_eq!(ShellEventKind::classify("tab_title_updated"), ShellEventKind::Cosmetic);
    assert_eq!(ShellEventKind::classify("tab_url_updated"), ShellEventKind::Cosmetic);
    assert_eq!(ShellEventKind::classify("tab_favicon_updated"), ShellEventKind::Cosmetic);
    assert_eq!(ShellEventKind::classify("tab_loading_state_changed"), ShellEventKind::Cosmetic);
    // Unknown events default to Structural (safe fallback)
    assert_eq!(ShellEventKind::classify("unknown_event"), ShellEventKind::Structural);
}
