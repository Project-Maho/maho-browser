#![cfg(unix)]
//! Drives `OmoBackend` against the omo runtime shipped inside Maho.app, so session
//! lifecycle and error propagation are proven against the real RPC server rather than a
//! stand-in. Point `MAHO_OMO_BUNDLE_DIR` at `<Maho.app>/Contents`, or `MAHO_OMO_BUN` plus
//! `MAHO_OMO_RPC_ENTRY` at an equivalent runtime. The tests are `#[ignore]`d; opt in with
//! `cargo test -- --ignored` and they fail when no runtime is present.

use std::path::PathBuf;
use std::time::Duration;

use maho_agent::omo::backend::OmoBackend;
use maho_agent::omo::config::{CustomProvider, OmoLaunchConfig};
use maho_agent::AgentRuntime;
use maho_types::chat::{ChatContent, ChatMessage};
use tokio::io::{AsyncReadExt, AsyncWriteExt};

fn bundle() -> Option<(PathBuf, PathBuf)> {
    let contents = PathBuf::from(
        std::env::var("MAHO_OMO_BUNDLE_DIR")
            .unwrap_or_else(|_| "/Applications/Maho.app/Contents".to_string()),
    );
    let bun = std::env::var("MAHO_OMO_BUN")
        .map(PathBuf::from)
        .unwrap_or_else(|_| contents.join("MacOS").join("bun"));
    let entry = std::env::var("MAHO_OMO_RPC_ENTRY")
        .map(PathBuf::from)
        .unwrap_or_else(|_| contents.join("Resources/omo/dist/rpc-entry.js"));
    (bun.exists() && entry.exists()).then_some((bun, entry))
}

async fn fixed_provider(status: &'static str, body: &'static str) -> String {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let address = listener.local_addr().unwrap();
    tokio::spawn(async move {
        while let Ok((mut stream, _)) = listener.accept().await {
            tokio::spawn(async move {
                let mut buffer = vec![0u8; 64 * 1024];
                let _ = stream.read(&mut buffer).await;
                let response = format!(
                    "HTTP/1.1 {status}\r\ncontent-type: application/json\r\ncontent-length: {}\r\nconnection: close\r\n\r\n{body}",
                    body.len()
                );
                let _ = stream.write_all(response.as_bytes()).await;
            });
        }
    });
    format!("http://{address}/v1")
}

fn backend(scratch: &std::path::Path, base_url: String, env: &[(&str, &str)]) -> OmoBackend {
    let (bun, entry) = bundle().expect("bundle checked by caller");
    let mut builder = OmoLaunchConfig::builder(scratch)
        .runtime_binary(bun)
        .rpc_entry(entry)
        .socket_path(scratch.join("rpc.sock"))
        .custom_provider(CustomProvider {
            name: "fixture".to_string(),
            base_url,
            api_key: "fixture-key".to_string(),
            models: vec!["fixture-model".to_string()],
        })
        .provider("fixture")
        .model_id("fixture-model");
    for (key, value) in env {
        builder = builder.env_var(*key, *value);
    }
    let config = builder.build().expect("config builds");
    OmoBackend::new(config, scratch.join("maho-browser-mcp"), scratch)
}

async fn turn(backend: &OmoBackend, session: &str) -> Result<ChatMessage, maho_agent::AgentError> {
    tokio::time::timeout(
        Duration::from_secs(90),
        backend.run_turn(session, ChatMessage::user(ChatContent::text("hi")), None, None),
    )
    .await
    .expect("turn must reach a terminal event")
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "live: needs the bundled omo runtime (MAHO_OMO_BUNDLE_DIR or MAHO_OMO_BUN + MAHO_OMO_RPC_ENTRY); opt in with cargo test -- --ignored"]
async fn provider_http_error_reaches_the_caller_with_its_text() {
    assert!(bundle().is_some(), "bundled omo runtime not found");
    let scratch = tempfile::tempdir().unwrap();
    let provider = fixed_provider(
        "429 Too Many Requests",
        r#"{"error":{"code":429,"message":"Individual quota reached. Please upgrade your subscription."}}"#,
    )
    .await;
    let backend = backend(scratch.path(), provider, &[]);

    let error = turn(&backend, "live-429").await.expect_err("a 429 turn must fail");
    let text = error.to_string();
    eprintln!("LIVE_PROVIDER_ERROR={text}");
    assert!(text.contains("429"), "{text}");
    assert!(text.contains("Individual quota reached"), "{text}");
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "live: needs the bundled omo runtime (MAHO_OMO_BUNDLE_DIR or MAHO_OMO_BUN + MAHO_OMO_RPC_ENTRY); opt in with cargo test -- --ignored"]
async fn idle_evicted_session_is_reopened_without_unknown_session() {
    assert!(bundle().is_some(), "bundled omo runtime not found");
    let scratch = tempfile::tempdir().unwrap();
    // Any provider answer works here: the assertion is about the session handle, and
    // a provider error proves the prompt was accepted by a live session. A 400 is not
    // retried by omo, so each turn settles promptly.
    let provider = fixed_provider("400 Bad Request", r#"{"error":{"message":"fixture"}}"#).await;
    let backend = backend(
        scratch.path(),
        provider,
        &[("SENPI_RPC_SESSION_IDLE_EVICTION_MS", "1500")],
    );
    let mut events = backend.subscribe_rpc_events().await.expect("subscribe before turn");

    let first = turn(&backend, "live-evict").await.expect_err("fixture provider fails");
    eprintln!("LIVE_TURN_1={first}");
    assert!(!first.to_string().contains("unknown_session"), "{first}");

    tokio::time::timeout(Duration::from_secs(30), async {
        loop {
            match events.recv().await.expect("runtime event stream must stay open") {
                maho_agent::omo::proto::RpcEvent::SessionClosed { reason, .. }
                    if reason.as_deref() == Some("idle_evicted") => break,
                maho_agent::omo::proto::RpcEvent::SessionParked { .. } => break,
                _ => {}
            }
        }
    }).await.expect("runtime must announce idle eviction");

    let second = turn(&backend, "live-evict").await.expect_err("fixture provider fails");
    eprintln!("LIVE_TURN_2={second}");
    assert!(
        !second.to_string().contains("unknown_session"),
        "an idle-evicted session must be reopened, got: {second}"
    );
    assert!(second.to_string().contains("fixture"), "{second}");
}
