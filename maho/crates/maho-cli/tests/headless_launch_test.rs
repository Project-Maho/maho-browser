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
    // A binary that exits immediately on every supported platform.
    #[cfg(windows)]
    let exits_immediately = std::env::var("SystemRoot")
        .map(|root| format!(r"{root}\System32\whoami.exe"))
        .unwrap_or_else(|_| r"C:\Windows\System32\whoami.exe".to_owned());
    #[cfg(not(windows))]
    let exits_immediately = "/usr/bin/false".to_owned();
    let home = std::env::temp_dir().join("maho-test-nonexistent");
    let output = Command::new(cli_bin())
        .args([
            "headless",
            "--url",
            "https://example.com",
            "--extract",
            "title",
            "--launch",
        ])
        .env("MAHO_APP_PATH", &exits_immediately)
        .env("MAHO_SOCKET_TIMEOUT_MS", "1000")
        .env("HOME", &home)
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

#[test]
fn headless_launch_rejects_unknown_extractor_before_spawning_a_browser() {
    // The binary path does not exist: if extractor validation ran after the launch
    // step, the CLI would report the missing binary instead (and with a real binary
    // it would leave the spawned headless browser running after exiting).
    let output = Command::new(cli_bin())
        .args([
            "headless",
            "--url",
            "https://example.com",
            "--extract",
            "title,bogus",
            "--launch",
        ])
        .env("MAHO_APP_PATH", "/nonexistent/path/Maho")
        .env("HOME", std::env::temp_dir().join("maho-test-nonexistent"))
        .output()
        .expect("failed to run CLI");

    assert!(!output.status.success());
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.contains("Unknown extractor: bogus"),
        "expected extractor validation before launch, got: {stderr}"
    );
    assert!(
        !stderr.contains("Launching browser"),
        "no browser may be launched for an invalid extractor, got: {stderr}"
    );
}
