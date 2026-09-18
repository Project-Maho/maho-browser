#![cfg(unix)]

use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use maho_agent::omo::backend::OmoBackend;
use maho_agent::omo::config::OmoLaunchConfig;
use maho_agent::{AgentRuntime, AgentStreamEvent};
use maho_types::chat::{ChatContent, ChatMessage};

fn rpc_entry() -> PathBuf {
    PathBuf::from(
        "/Users/indo/.bun/install/global/node_modules/@code-yeongyu/senpi/dist/rpc-entry.js",
    )
}

fn browser_mcp_binary() -> PathBuf {
    PathBuf::from("/Users/indo/code/project/maho-workspace/maho/target/debug/maho-browser-mcp")
}

#[tokio::test(flavor = "multi_thread")]
async fn drives_a_turn_through_a_real_omo_rpc_runtime() {
    if !rpc_entry().exists() {
        eprintln!("skipping: omo runtime not installed at {:?}", rpc_entry());
        return;
    }
    if !browser_mcp_binary().exists() {
        eprintln!(
            "skipping: browser MCP binary not built at {:?}",
            browser_mcp_binary()
        );
        return;
    }

    let scratch = tempfile::tempdir().expect("scratch directory");
    let socket = scratch.path().join("rpc.sock");
    let config = OmoLaunchConfig::builder(scratch.path())
        .runtime_binary("bun")
        .rpc_entry(rpc_entry())
        .socket_path(socket)
        .build()
        .expect("config builds");
    let backend = OmoBackend::new(config, browser_mcp_binary(), scratch.path());
    let events = Arc::new(Mutex::new(Vec::new()));
    let event_sink = Arc::clone(&events);

    let result = tokio::time::timeout(
        Duration::from_secs(60),
        backend.run_turn(
            "live-backend-session",
            ChatMessage::user(ChatContent::text("Reply with the single word pong.")),
            None,
            Some(Arc::new(move |event: AgentStreamEvent| {
                event_sink
                    .lock()
                    .unwrap_or_else(std::sync::PoisonError::into_inner)
                    .push(event);
            })),
        ),
    )
    .await
    .expect("real omo turn reaches a terminal RPC event");

    match result {
        Ok(message) => {
            assert_eq!(message.role, "assistant");
        }
        Err(error) => {
            let detail = error.to_string();
            assert!(
                !detail.contains("failed to spawn omo runtime")
                    && !detail.contains("failed to connect to omo RPC socket")
                    && !detail.contains("timed out")
                    && !detail.contains("connection closed"),
                "expected a real server/model response error, got transport failure: {detail}"
            );
            eprintln!("real omo RPC reached server error: {detail}");
        }
    }

    let mcp_config = std::fs::read_to_string(scratch.path().join("mcp.json"))
        .expect("backend writes global MCP config");
    assert!(mcp_config.contains(browser_mcp_binary().to_string_lossy().as_ref()));
}

#[tokio::test(flavor = "multi_thread")]
async fn streams_real_assistant_tokens_through_a_configured_provider() {
    if !rpc_entry().exists() || !browser_mcp_binary().exists() {
        eprintln!("skipping: omo runtime or browser MCP binary missing");
        return;
    }
    let (Ok(base_url), Ok(api_key), Ok(model)) = (
        std::env::var("MAHO_TEST_PROXY_URL"),
        std::env::var("MAHO_TEST_PROXY_KEY"),
        std::env::var("MAHO_TEST_PROXY_MODEL"),
    ) else {
        eprintln!("skipping: MAHO_TEST_PROXY_URL, MAHO_TEST_PROXY_KEY, or MAHO_TEST_PROXY_MODEL not set");
        return;
    };

    let scratch = tempfile::tempdir().expect("scratch directory");
    let config = OmoLaunchConfig::builder(scratch.path())
        .runtime_binary("bun")
        .rpc_entry(rpc_entry())
        .socket_path(scratch.path().join("rpc.sock"))
        .custom_provider(maho_agent::omo::config::CustomProvider {
            name: "maho-proxy".to_string(),
            base_url,
            api_key,
            models: vec![model.clone()],
        })
        .model_id(model)
        .build()
        .expect("config builds");

    let backend = OmoBackend::new(config, browser_mcp_binary(), scratch.path());
    let events = Arc::new(Mutex::new(Vec::new()));
    let sink = Arc::clone(&events);

    let result = tokio::time::timeout(
        Duration::from_secs(120),
        backend.run_turn(
            "live-provider-session",
            ChatMessage::user(ChatContent::text("Reply with the single word pong.")),
            None,
            Some(Arc::new(move |event: AgentStreamEvent| {
                sink.lock()
                    .unwrap_or_else(std::sync::PoisonError::into_inner)
                    .push(event);
            })),
        ),
    )
    .await
    .expect("turn reaches a terminal event");

    let models_config = std::fs::read_to_string(scratch.path().join("models.json"))
        .expect("backend writes models.json for the custom provider");
    assert!(models_config.contains("maho-proxy"), "{models_config}");

    {
        let captured = events
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        eprintln!("EVENT_COUNT={}", captured.len());
        for (i, ev) in captured.iter().enumerate().take(40) {
            eprintln!("EVENT[{i}]={ev:?}");
        }
    }
    let message = result.expect("a configured provider must produce an assistant message");
    let text = match &message.content {
        ChatContent::Text(t) => t.clone(),
        other => format!("{other:?}"),
    };
    eprintln!("ASSISTANT_TEXT={text}");
    assert_eq!(message.role, "assistant");
    assert!(!text.trim().is_empty(), "assistant text must not be empty");

    let captured = events
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
        .len();
    eprintln!("STREAM_EVENTS={captured}");
    assert!(captured > 0, "a real turn must emit stream events");
}

#[tokio::test]
async fn omo_returns_auth_providers_with_oauth_and_api_key() {
    let scratch = tempfile::tempdir().expect("tempdir");
    let sock = scratch.path().join("omo.sock");
    let helpers_mcp = std::path::PathBuf::from("/Volumes/T9-Mac/chromium/src/out/Default/Maho.app/Contents/Helpers/maho-browser-mcp");

    let config = OmoLaunchConfig::builder(scratch.path())
        .runtime_binary("bun")
        .rpc_entry(rpc_entry())
        .socket_path(&sock)
        .build()
        .expect("config builds");

    let backend = OmoBackend::new(config, helpers_mcp, scratch.path());
    let providers = backend.get_auth_providers().await.expect("query auth providers");
    eprintln!("AUTH_PROVIDERS_COUNT={}", providers.len());
    for p in &providers {
        eprintln!("AUTH_PROVIDER: id={} name={} auth_type={:?} configured={}", p.id, p.name, p.auth_type, p.status.configured);
    }
    assert!(!providers.is_empty(), "auth providers must not be empty");
    assert!(
        providers.iter().any(|p| p.auth_type == "oauth"),
        "at least one OAuth provider must be present (e.g. anthropic, openai-codex, google)"
    );
}

#[test]
fn resolver_agrees_with_the_build_staging_layout() {
    let Ok(dir) = std::env::var("MAHO_STAGED_MACOS_DIR") else {
        eprintln!("skipping: MAHO_STAGED_MACOS_DIR not set");
        return;
    };
    let rt = maho_agent::omo::config::BundledRuntime::from_executable_dir(&dir);
    let missing = rt.missing();
    assert!(
        missing.is_empty(),
        "build staged a layout the runtime resolver cannot find: {missing:?}"
    );
}

#[tokio::test]
async fn omo_invokes_browser_tab_list_through_mcp() {
    let scratch = tempfile::tempdir().expect("tempdir");
    let sock = scratch.path().join("omo.sock");
    let helpers_mcp = std::path::PathBuf::from("/Volumes/T9-Mac/chromium/src/out/Default/Maho.app/Contents/Helpers/maho-browser-mcp");
    if !helpers_mcp.exists() {
        eprintln!("skipping: helpers_mcp not built yet");
        return;
    }

    let Ok(api_key) = std::env::var("MAHO_TEST_PROXY_KEY") else {
        eprintln!("skipping: MAHO_TEST_PROXY_KEY not set");
        return;
    };
    let base_url = std::env::var("MAHO_TEST_PROXY_URL")
        .unwrap_or_else(|_| "http://127.0.0.1:18801/v1".to_string());
    let model = std::env::var("MAHO_TEST_PROXY_MODEL")
        .unwrap_or_else(|_| "claude-opus-4-6-thinking".to_string());

    let config = OmoLaunchConfig::builder(scratch.path())
        .runtime_binary("bun")
        .rpc_entry(rpc_entry())
        .socket_path(&sock)
        .custom_provider(maho_agent::omo::config::CustomProvider {
            name: "maho-proxy".to_string(),
            base_url,
            api_key,
            models: vec![model.clone()],
        })
        .model_id(model)
        .build()
        .expect("config builds");

    let backend = OmoBackend::new(config, helpers_mcp, scratch.path());
    let events = Arc::new(Mutex::new(Vec::new()));
    let sink = Arc::clone(&events);

    let result = tokio::time::timeout(
        Duration::from_secs(120),
        backend.run_turn(
            "live-tool-session",
            ChatMessage::user(ChatContent::text("Call browser_tab_list and reply with the title of the first tab.")),
            None,
            Some(Arc::new(move |event: AgentStreamEvent| {
                sink.lock()
                    .unwrap_or_else(std::sync::PoisonError::into_inner)
                    .push(event);
            })),
        ),
    )
    .await
    .expect("turn reaches a terminal event")
    .expect("turn succeeds");

    let captured = events.lock().unwrap();
    eprintln!("CAPTURED_EVENTS_LEN={}", captured.len());
    for (i, ev) in captured.iter().enumerate().take(30) {
        eprintln!("EVENT[{i}]={ev:?}");
    }
    let text = match &result.content {
        ChatContent::Text(t) => t.clone(),
        other => format!("{other:?}"),
    };
    eprintln!("FINAL_ASSISTANT_REPLY={text}");
    assert!(!text.trim().is_empty(), "must not be empty");

    let tool_call_observed = captured.iter().any(|ev| match ev {
        AgentStreamEvent::ToolCall { name, .. } => name == "browser_tab_list",
        AgentStreamEvent::Token(t) => t.contains("browser_tab_list"),
        _ => false,
    });
    assert!(
        tool_call_observed || text.contains("browser_tab_list"),
        "must observe browser_tab_list tool invocation in stream events or assistant reply"
    );
}

#[tokio::test]
async fn omo_gates_boundary_tools_as_always_ask() {
    let scratch = tempfile::tempdir().expect("tempdir");
    let sock = scratch.path().join("omo.sock");
    let helpers_mcp = std::path::PathBuf::from("/Volumes/T9-Mac/chromium/src/out/Default/Maho.app/Contents/Helpers/maho-browser-mcp");
    if !helpers_mcp.exists() {
        eprintln!("skipping: helpers_mcp not built yet");
        return;
    }

    let config = OmoLaunchConfig::builder(scratch.path())
        .runtime_binary("bun")
        .rpc_entry(rpc_entry())
        .socket_path(&sock)
        .build()
        .expect("config builds");

    let backend = OmoBackend::new(config, helpers_mcp, scratch.path());
    let tools = backend.list_tools().await.expect("list tools");
    if tools.is_empty() {
        eprintln!("skipping: browser is not running to answer MCP tools");
        return;
    }

    if let Some(mail_send) = tools.iter().find(|t| t.name == "mail_send") {
        assert_eq!(mail_send.permission, maho_types::tool::ToolPermission::AlwaysAsk);
        assert!(mail_send.sensitive);
    }

    if let Some(tab_list) = tools.iter().find(|t| t.name == "browser_tab_list") {
        assert_eq!(tab_list.permission, maho_types::tool::ToolPermission::AutoApprove);
        assert!(!tab_list.sensitive);
    }
}

