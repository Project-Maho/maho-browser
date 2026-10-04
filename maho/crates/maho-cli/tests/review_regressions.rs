use maho_cli::{browser::parse_duration_to_seconds, config::CliConfig};

#[test]
fn duration_rejects_non_ascii_suffix_without_panicking() {
    assert!(parse_duration_to_seconds("7日").is_err());
}

#[test]
fn duration_rejects_overflow_without_panicking() {
    assert!(parse_duration_to_seconds("9223372036854775807d").is_err());
}

#[test]
fn config_display_accepts_utf8_tokens() {
    for token in ["aéé", "ééééé", "abc"] {
        let config = CliConfig {
            access_token: Some(token.to_owned()),
            refresh_token: Some(token.to_owned()),
            ..Default::default()
        };
        let display = config.redacted_display();
        assert!(
            !display.contains(token),
            "must not expose the complete token"
        );
    }
}

#[cfg(unix)]
#[test]
fn batch_reports_broken_output_pipe() {
    use std::process::{Command, Stdio};
    let dir = tempfile::tempdir().unwrap();
    let input = dir.path().join("urls.txt");
    std::fs::write(&input, "https://example.test\n").unwrap();
    let (reader, writer) = std::os::unix::net::UnixStream::pair().unwrap();
    drop(reader);
    let output_fd: std::os::fd::OwnedFd = writer.into();
    let child = Command::new(env!("CARGO_BIN_EXE_maho"))
        .args([
            "--socket-path",
            dir.path().join("missing.sock").to_str().unwrap(),
            "headless",
            "--batch",
            input.to_str().unwrap(),
        ])
        .stdout(Stdio::from(output_fd))
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();
    let output = child.wait_with_output().unwrap();
    assert!(
        !output.status.success(),
        "lost output must fail the command: {:?}",
        output
    );
}
