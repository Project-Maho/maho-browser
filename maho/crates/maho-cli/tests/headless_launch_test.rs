use std::process::Command;

fn cli_bin() -> String {
    env!("CARGO_BIN_EXE_maho").to_string()
}

#[test]
fn headless_launch_flag_appears_in_help() {
    let output = Command::new(cli_bin())
        .args(["headless", "--help"])
        .output()
        .expect("failed to run CLI");

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("--launch"),
        "expected --launch in help, got: {stdout}"
    );
    assert!(
        stdout.contains("--keep-alive"),
        "expected --keep-alive in help, got: {stdout}"
    );
}

#[test]
fn headless_launch_with_false_binary_fails() {
    let output = Command::new(cli_bin())
        .args([
            "headless",
            "--url",
            "https://example.com",
            "--extract",
            "title",
            "--launch",
        ])
        .env("MAHO_APP_PATH", "/usr/bin/false")
        .env("MAHO_SOCKET_TIMEOUT_MS", "1000")
        .env("HOME", "/tmp/maho-test-nonexistent")
        .output()
        .expect("failed to run CLI");

    assert!(!output.status.success());
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.contains("exited immediately") || stderr.contains("did not appear"),
        "expected spawn-failure message, got: {stderr}"
    );
}

#[test]
fn headless_no_launch_without_browser_errors() {
    let output = Command::new(cli_bin())
        .args([
            "headless",
            "--url",
            "https://example.com",
            "--extract",
            "title",
        ])
        .env("HOME", "/tmp/maho-test-nonexistent")
        .output()
        .expect("failed to run CLI");

    assert!(!output.status.success());
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.contains("not running") || stderr.contains("connect"),
        "expected connection error, got: {stderr}"
    );
}

#[test]
fn headless_launch_nonexistent_binary_errors() {
    let output = Command::new(cli_bin())
        .args([
            "headless",
            "--url",
            "https://example.com",
            "--extract",
            "title",
            "--launch",
        ])
        .env("MAHO_APP_PATH", "/nonexistent/path/Maho")
        .env("HOME", "/tmp/maho-test-nonexistent")
        .output()
        .expect("failed to run CLI");

    assert!(!output.status.success());
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.contains("Cannot find") || stderr.contains("not running"),
        "expected binary-not-found error, got: {stderr}"
    );
}
