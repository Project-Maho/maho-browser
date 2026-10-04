use std::process::Command;

fn maho_cmd() -> Command {
    let mut cmd = Command::new(env!("CARGO_BIN_EXE_maho"));
    cmd.env("MAHO_CLI_TEST", "1");
    cmd.stdin(std::process::Stdio::null());
    cmd
}

#[test]
fn help_contains_all_command_groups() {
    let output = maho_cmd()
        .arg("--help")
        .output()
        .expect("failed to run maho --help");

    let stdout = String::from_utf8_lossy(&output.stdout);

    // All visible top-level groups must appear in --help
    let expected_groups = [
        "auth",
        "billing",
        "tab",
        "history",
        "bookmarks",
        "page",
        "headless",
        "browser",
        "mcp",
        "version",
    ];
    for group in &expected_groups {
        assert!(
            stdout.contains(group),
            "--help output should contain '{group}', got:\n{stdout}"
        );
    }

    // Hidden deprecation shims must NOT appear in --help
    let hidden_commands = ["login", "signup", "logout", "whoami", "buy-credits"];
    for cmd in &hidden_commands {
        // Check they don't appear as standalone top-level entries.
        // They might appear in descriptions (e.g., "Use `maho auth login`"),
        // but they shouldn't be listed as subcommand names at the top level.
        // We check that the command is not listed with its own help line.
        let pattern = format!("  {cmd} ");
        assert!(
            !stdout.contains(&pattern),
            "hidden command '{cmd}' should not be visible in --help as '  {cmd} ...'"
        );
    }
}

#[test]
fn help_contains_global_flags() {
    let output = maho_cmd()
        .arg("--help")
        .output()
        .expect("failed to run maho --help");

    let stdout = String::from_utf8_lossy(&output.stdout);

    assert!(
        stdout.contains("--json"),
        "--help should show --json flag, got:\n{stdout}"
    );
    // `--isolated` was removed: it was declared and parsed but no handler
    // ever read it, so it advertised browser-profile isolation the CLI did
    // not perform. Help must not offer it back.
    assert!(
        !stdout.contains("--isolated"),
        "--help must not advertise the removed no-op --isolated flag, got:\n{stdout}"
    );
    assert!(
        stdout.contains("--socket-path"),
        "--help should show --socket-path flag, got:\n{stdout}"
    );
    assert!(
        stdout.contains("--relay-url"),
        "--help should show --relay-url flag, got:\n{stdout}"
    );
}

#[test]
fn auth_subcommand_has_nested_commands() {
    let output = maho_cmd()
        .args(["auth", "--help"])
        .output()
        .expect("failed to run maho auth --help");

    let stdout = String::from_utf8_lossy(&output.stdout);

    for sub in &["login", "signup", "logout", "whoami", "config-show"] {
        assert!(
            stdout.contains(sub),
            "maho auth --help should list '{sub}', got:\n{stdout}"
        );
    }
}

#[test]
fn deprecated_login_prints_warning_to_stderr() {
    // Use --email and --password to avoid interactive prompt; will fail at network
    // but we only care about the deprecation warning on stderr.
    let output = maho_cmd()
        .args(["login", "--email", "test@x.com", "--password", "pass123"])
        .output()
        .expect("failed to run maho login");

    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.contains("[deprecated]"),
        "deprecated login should print [deprecated] to stderr, got:\n{stderr}"
    );
    assert!(
        stderr.contains("maho auth login"),
        "deprecation warning should mention new path 'maho auth login', got:\n{stderr}"
    );
}

#[test]
fn deprecated_balance_prints_warning_to_stderr() {
    let tmp = tempfile::TempDir::new().unwrap();
    let output = maho_cmd()
        .env("MAHO_CONFIG_DIR", tmp.path())
        .args(["balance"])
        .output()
        .expect("failed to run maho balance");

    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.contains("[deprecated]"),
        "deprecated balance should print [deprecated] to stderr, got:\n{stderr}"
    );
    assert!(
        stderr.contains("maho billing balance"),
        "deprecation warning should mention new path 'maho billing balance', got:\n{stderr}"
    );
}

#[test]
fn tab_info_exits_with_error_when_no_browser() {
    let output = maho_cmd()
        .args([
            "--socket-path",
            "/tmp/nonexistent-maho-cli-test.sock",
            "tab",
            "info",
        ])
        .output()
        .expect("failed to run maho tab info");

    assert_eq!(
        output.status.code(),
        Some(1),
        "tab info should exit with code 1 when no browser running"
    );

    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.contains("Browser not running") || stderr.contains("socket connect failed"),
        "should indicate browser not running, got:\n{stderr}"
    );
}

#[test]
fn headless_exits_with_error_when_no_browser() {
    let output = maho_cmd()
        .args([
            "--socket-path",
            "/tmp/nonexistent-maho-cli-test.sock",
            "headless",
            "--url",
            "https://example.com",
            "--extract",
            "title",
        ])
        .output()
        .expect("failed to run maho headless");

    assert_eq!(
        output.status.code(),
        Some(1),
        "headless should exit with code 1 when no browser running"
    );

    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.contains("no running browser") || stderr.contains("Browser not running"),
        "headless should indicate no browser, got:\n{stderr}"
    );
}

#[cfg(unix)]
#[test]
fn mcp_errors_when_sibling_binary_missing() {
    use std::os::unix::fs::PermissionsExt;

    // `maho mcp` exec-replaces the process with the sibling `maho-browser-mcp`
    // binary resolved via current_exe(). Copying the CLI into an isolated dir
    // with no sibling makes the missing-binary error path deterministic
    // (independent of whatever sits next to the test binary in target/debug).
    let dir = tempfile::tempdir().unwrap();
    let maho_copy = dir.path().join("maho");
    std::fs::copy(env!("CARGO_BIN_EXE_maho"), &maho_copy).unwrap();
    let mut perms = std::fs::metadata(&maho_copy).unwrap().permissions();
    perms.set_mode(0o755);
    std::fs::set_permissions(&maho_copy, perms).unwrap();

    let output = Command::new(&maho_copy)
        .arg("mcp")
        .stdin(std::process::Stdio::null())
        .output()
        .expect("failed to run maho mcp");

    assert!(
        !output.status.success(),
        "mcp should fail when the sibling maho-browser-mcp binary is missing"
    );
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.contains("maho-browser-mcp"),
        "mcp should report the missing sibling binary, got:\n{stderr}"
    );
}

#[test]
fn version_command_prints_version() {
    let output = maho_cmd()
        .args(["version"])
        .output()
        .expect("failed to run maho version");

    assert!(output.status.success(), "maho version should succeed");
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("maho"),
        "maho version should contain 'maho', got:\n{stdout}"
    );
}

#[test]
fn help_output_snapshot() {
    // Plain string snapshot test — no insta dep needed.
    // Verifies the about line and command grouping structure.
    let output = maho_cmd()
        .arg("--help")
        .output()
        .expect("failed to run maho --help");

    let stdout = String::from_utf8_lossy(&output.stdout);

    // About line from the binary
    assert!(
        stdout.contains("Maho CLI"),
        "help should contain 'Maho CLI', got:\n{stdout}"
    );
    assert!(
        stdout.contains("browser control from your terminal"),
        "help should contain updated about text, got:\n{stdout}"
    );

    // Verify command ordering (all groups present in order)
    let auth_pos = stdout.find("auth").expect("auth not found");
    let billing_pos = stdout.find("billing").expect("billing not found");

    assert!(
        auth_pos < billing_pos,
        "auth should appear before billing in help"
    );
}

#[test]
fn billing_buy_credits_preserves_behavior() {
    let output = maho_cmd()
        .args(["buy-credits", "--amount", "10"])
        .output()
        .expect("failed to run maho buy-credits");

    let stdout = String::from_utf8_lossy(&output.stdout);
    let stderr = String::from_utf8_lossy(&output.stderr);

    // Should print deprecation warning
    assert!(
        stderr.contains("[deprecated]"),
        "deprecated buy-credits should warn, got stderr:\n{stderr}"
    );

    assert!(
        !output.status.success(),
        "deprecated unauthenticated buy-credits should fail closed"
    );
    assert!(
        !stdout.contains("93f5ccbc-cd1c-4c4a-94a0-5ff4ad8d2c35"),
        "buy-credits must not produce an unattributable URL, got stdout:\n{stdout}"
    );
}
