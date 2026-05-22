use std::time::Duration;

use maho_acp::AcpClient;

#[tokio::test]
#[ignore]
async fn drop_client_mid_turn_reaps_child() {
    let which = std::process::Command::new("which")
        .arg("opencode")
        .output();

    match which {
        Ok(output) if output.status.success() => {}
        _ => {
            eprintln!("SKIP: `opencode` not found on PATH");
            return;
        }
    }

    let client = AcpClient::start("opencode")
        .await
        .expect("failed to start opencode acp");

    let init = client.initialize().await.expect("initialize failed");
    assert_eq!(init.protocol_version, 1);

    let session = client
        .new_session(std::env::current_dir().unwrap(), vec![])
        .await
        .expect("session/new failed");

    let _handle = client
        .submit_prompt(&session.session_id, "Say hello".to_string())
        .await
        .expect("submit_prompt failed");

    // Drop the client mid-turn — this should close stdin and reap the child
    // within the grace window (1.5s + 1s SIGTERM + SIGKILL).
    drop(client);

    // Give the shutdown sequence time to complete.
    tokio::time::sleep(Duration::from_secs(4)).await;

    // Verify no orphan `opencode acp` child remains for this test.
    let pgrep = std::process::Command::new("pgrep")
        .args(["-f", "opencode acp"])
        .output();

    if let Ok(output) = pgrep {
        let stdout = String::from_utf8_lossy(&output.stdout);
        assert!(
            stdout.trim().is_empty(),
            "orphan opencode acp process detected: {}",
            stdout
        );
    }
}
