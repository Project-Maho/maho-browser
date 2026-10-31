#![cfg(unix)]
//! Drives `session_control` against the real senpi RPC host launched with Maho's argv: a
//! first connection runs a slow turn on Maho's session transcript while a second process
//! reads its status, steers it and stops it. `#[ignore]`d like the other live omo tests.

use std::path::PathBuf;
use std::time::Duration;

use maho_agent::omo::config::{CustomProvider, OmoLaunchConfig};
use maho_agent::omo::session_control::{self, SessionControlError, SessionVerb};
use tokio::io::{AsyncReadExt, AsyncWriteExt};

fn runtime() -> (PathBuf, PathBuf) {
    let home = PathBuf::from(std::env::var("HOME").expect("HOME"));
    let bun = std::env::var("MAHO_OMO_BUN")
        .map(PathBuf::from)
        .unwrap_or_else(|_| home.join(".bun/bin/bun"));
    let entry = std::env::var("MAHO_OMO_RPC_ENTRY").map(PathBuf::from).unwrap_or_else(|_| {
        home.join(".bun/install/global/node_modules/@code-yeongyu/senpi/dist/rpc-entry.js")
    });
    assert!(bun.exists() && entry.exists(), "live session test needs bun and senpi");
    (bun, entry)
}

async fn slow_provider() -> String {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let address = listener.local_addr().unwrap();
    tokio::spawn(async move {
        while let Ok((mut stream, _)) = listener.accept().await {
            tokio::spawn(async move {
                let mut buffer = vec![0u8; 256 * 1024];
                let _ = stream.read(&mut buffer).await;
                let chunk = |delta: serde_json::Value, finish: Option<&str>| {
                    format!(
                        "data: {}\n\n",
                        serde_json::json!({"id": "x", "object": "chat.completion.chunk", "created": 0,
                            "model": "fake-model",
                            "choices": [{"index": 0, "delta": delta, "finish_reason": finish}]})
                    )
                };
                let head = "HTTP/1.1 200 OK\r\ncontent-type: text/event-stream\r\nconnection: close\r\n\r\n";
                let _ = stream.write_all(head.as_bytes()).await;
                let _ = stream
                    .write_all(chunk(serde_json::json!({"role": "assistant", "content": "working"}), None).as_bytes())
                    .await;
                tokio::time::sleep(Duration::from_secs(20)).await;
                let _ = stream.write_all(chunk(serde_json::json!({}), Some("stop")).as_bytes()).await;
                let _ = stream.write_all(b"data: [DONE]\n\n").await;
            });
        }
    });
    format!("http://{address}/v1")
}

async fn until_streaming(config: &OmoLaunchConfig, id: &str, want: bool) {
    let deadline = tokio::time::Instant::now() + Duration::from_secs(30);
    loop {
        let state = session_control::control(config, id, SessionVerb::Status, None)
            .await
            .expect("status");
        if state.is_streaming() == Some(want) {
            return;
        }
        assert!(tokio::time::Instant::now() < deadline, "isStreaming never became {want}: {:?}", state.payload);
        tokio::time::sleep(Duration::from_millis(250)).await;
    }
}

#[tokio::test(flavor = "multi_thread")]
#[ignore = "needs bun and senpi; run with --ignored"]
async fn second_process_reads_steers_and_stops_a_running_turn() {
    let (bun, entry) = runtime();
    let scratch = tempfile::tempdir().unwrap();
    let agent_dir = scratch.path().join("agent");
    let base_url = slow_provider().await;
    let config = OmoLaunchConfig::builder(&agent_dir)
        .runtime_binary(&bun)
        .rpc_entry(&entry)
        .socket_path(scratch.path().join("rpc.sock"))
        .custom_provider(CustomProvider {
            name: "fake".to_string(),
            base_url,
            api_key: "k".to_string(),
            models: vec!["fake-model".to_string()],
        })
        .provider("fake")
        .model_id("fake-model")
        .build()
        .unwrap();

    assert!(matches!(
        session_control::control(&config, "task-live", SessionVerb::Status, None).await,
        Err(SessionControlError::SessionNotFound(_))
    ));
    let transcript = session_control::session_file(&config, "task-live");
    std::fs::create_dir_all(transcript.parent().unwrap()).unwrap();
    std::fs::write(&transcript, "").unwrap();
    assert!(matches!(
        session_control::control(&config, "task-live", SessionVerb::Status, None).await,
        Err(SessionControlError::NotRunning(_))
    ));
    std::fs::remove_file(&transcript).unwrap();

    let backend = maho_agent::omo::backend::OmoBackend::new(
        config.clone(),
        scratch.path().join("maho-browser-mcp"),
        scratch.path(),
    );
    let turn = tokio::spawn(async move {
        use maho_agent::AgentRuntime;
        let message = maho_types::chat::ChatMessage::user(maho_types::chat::ChatContent::text("go"));
        let result = backend.run_turn("task-live", message, None, None).await;
        (backend, result)
    });

    let socket_deadline = tokio::time::Instant::now() + Duration::from_secs(30);
    while !session_control::session_file(&config, "task-live").exists() || !config.socket_path().exists() {
        assert!(tokio::time::Instant::now() < socket_deadline, "backend never opened the session");
        tokio::time::sleep(Duration::from_millis(100)).await;
    }
    until_streaming(&config, "task-live", true).await;

    let steer = session_control::control(&config, "task-live", SessionVerb::Steer, Some("steer-msg"))
        .await
        .expect("steer");
    assert_eq!(steer.payload["disposition"], "queued");
    assert_eq!(steer.is_streaming(), Some(true));

    let stop = session_control::control(&config, "task-live", SessionVerb::Stop, None)
        .await
        .expect("stop");
    assert_eq!(stop.payload["aborted"], true);
    until_streaming(&config, "task-live", false).await;

    assert!(matches!(
        session_control::control(&config, "no-such-task", SessionVerb::Steer, Some("x")).await,
        Err(SessionControlError::SessionNotFound(_))
    ));

    let (_backend, _result) = tokio::time::timeout(Duration::from_secs(60), turn)
        .await
        .expect("aborted turn settles")
        .expect("turn task");
}
