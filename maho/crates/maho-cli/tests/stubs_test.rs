fn run_cli(args: &[&str]) -> std::process::Output {
    let bin = env!("CARGO_BIN_EXE_maho");
    std::process::Command::new(bin)
        .args(args)
        .output()
        .expect("failed to run CLI binary")
}

#[test]
fn test_tab_annotate_stub() {
    let output = run_cli(&["tab", "annotate", "--title", "title", "--tab", "1"]);
    assert_eq!(output.status.code(), Some(2));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("Command 'tab annotate' is deprecated"));
}

#[test]
fn test_tab_write_stub() {
    let output = run_cli(&["tab", "write", "--format", "pdf", "--tab", "1"]);
    assert_eq!(output.status.code(), Some(2));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("Command 'tab write' is deprecated"));
}

/// `history import` is no longer registered at all (its handler was a pure
/// hard-exit stub for a server tool that does not exist), so clap rejects it
/// as an unknown subcommand — same exit code, no longer advertised in help.
#[test]
fn test_history_import_is_unregistered() {
    let output = run_cli(&["history", "import", "foo.ndjson"]);
    assert_eq!(output.status.code(), Some(2));
}

#[test]
fn test_history_search_referrer_domain_stub() {
    let output = run_cli(&["history", "search", "--referrer-domain", "example.com"]);
    assert_eq!(output.status.code(), Some(2));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("--referrer-domain option is not supported"));
}

#[test]
fn test_page_search_max_results_stub() {
    let output = run_cli(&["page", "search", "--query", "query", "--max-results", "10"]);
    assert_eq!(output.status.code(), Some(2));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("--max-results option is not supported"));
}

#[test]
fn test_page_screenshot_full_page_stub() {
    let output = run_cli(&["page", "screenshot", "--full-page"]);
    assert_eq!(output.status.code(), Some(2));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("--full-page option is not supported"));
}

#[test]
fn test_headless_wait_stub() {
    let output = run_cli(&[
        "headless",
        "--url",
        "https://example.com",
        "--wait",
        "timeout:5s",
        "--extract",
        "title",
    ]);
    assert_eq!(output.status.code(), Some(2));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("--wait option is not supported"));
}
