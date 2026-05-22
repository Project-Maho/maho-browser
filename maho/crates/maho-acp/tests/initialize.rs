//! Integration test: spawns real `opencode acp`, runs initialize round-trip.
//!
//! Gated behind `#[ignore]` — run manually with:
//!   cargo test -p maho-acp --test initialize -- --ignored

use maho_acp::AcpClient;

#[tokio::test]
#[ignore]
async fn initialize_round_trip() {
    // Check if opencode is available
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

    let result = client.initialize().await.expect("initialize failed");

    assert_eq!(result.protocol_version, 1, "protocol_version must be 1");

    println!("protocol_version: {}", result.protocol_version);
    println!("agentCapabilities: {}", result.agent_capabilities);
    println!(
        "authMethods: {:?}",
        result
            .auth_methods
            .iter()
            .map(|m| format!("{}({})", m.name, m.id))
            .collect::<Vec<_>>()
    );

    client.shutdown().await.expect("shutdown failed");
}
