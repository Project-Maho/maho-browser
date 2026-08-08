//! Integration tests for SwiftideBackend MahoManaged path (T2.1).
//!
//! Tests verify:
//!   1. MahoManaged path sends correct Authorization header to proxy.
//!   2. The agent receives the response.
//!   3. Correct model is requested.
//!   4. Fallback to BYOK when access_token is absent.

use std::sync::{Arc, Mutex, OnceLock};

use async_trait::async_trait;
use axum::{extract::State, http::StatusCode, routing::post, Router};
use maho_agent::{AgentError, AgentRuntime, AgentStorage, SwiftideBackend, ZeroizedString};
use maho_types::chat::{ChatContent, ChatMessage};
use tokio::net::TcpListener;

// Serialize tests that mutate MAHO_PROXY_URL env var
static ENV_MUTEX: OnceLock<tokio::sync::Mutex<()>> = OnceLock::new();
fn env_lock() -> &'static tokio::sync::Mutex<()> {
    ENV_MUTEX.get_or_init(|| tokio::sync::Mutex::new(()))
}

#[derive(Clone)]
struct MahoManagedStorage {
    access_token: Option<String>,
    messages: Arc<Mutex<Vec<(String, String, String)>>>,
}

#[async_trait]
impl AgentStorage for MahoManagedStorage {
    async fn get_byok_key(&self, _provider: &str) -> Result<Option<ZeroizedString>, AgentError> {
        Ok(Some(ZeroizedString::new("byok-test-key".to_string())))
    }

    async fn get_setting(&self, key: &str) -> Result<Option<ZeroizedString>, AgentError> {
        if key == "auth:access_token" {
            Ok(self
                .access_token
                .as_ref()
                .map(|t| ZeroizedString::new(t.clone())))
        } else {
            Ok(None)
        }
    }

    async fn save_message(
        &self,
        session_id: &str,
        role: &str,
        content: &str,
    ) -> Result<(), AgentError> {
        self.messages.lock().unwrap().push((
            session_id.to_string(),
            role.to_string(),
            content.to_string(),
        ));
        Ok(())
    }

    async fn get_history(&self, _session_id: &str) -> Result<Vec<ChatMessage>, AgentError> {
        Ok(vec![])
    }
}

#[derive(Clone)]
struct ProxyState {
    received_auth_headers: Arc<Mutex<Vec<String>>>,
    received_bodies: Arc<Mutex<Vec<serde_json::Value>>>,
}

async fn handle_chat_completions(
    State(state): State<ProxyState>,
    headers: axum::http::HeaderMap,
    body: axum::body::Bytes,
) -> (StatusCode, axum::http::HeaderMap, String) {
    if let Some(auth) = headers.get("authorization") {
        state
            .received_auth_headers
            .lock()
            .unwrap()
            .push(auth.to_str().unwrap_or("").to_string());
    }

    if let Ok(json) = serde_json::from_slice::<serde_json::Value>(&body) {
        state.received_bodies.lock().unwrap().push(json);
    }

    let response_body = serde_json::json!({
        "id": "chatcmpl-test-123",
        "object": "chat.completion",
        "created": 1700000000,
        "model": "google/gemini-3-flash-lite:free",
        "choices": [{
            "index": 0,
            "message": {
                "role": "assistant",
                "content": "Hello from MahoManaged proxy!"
            },
            "finish_reason": "stop"
        }],
        "usage": {"prompt_tokens": 10, "completion_tokens": 5, "total_tokens": 15}
    });

    let mut resp_headers = axum::http::HeaderMap::new();
    resp_headers.insert("content-type", "application/json".parse().unwrap());
    (StatusCode::OK, resp_headers, response_body.to_string())
}

async fn spawn_mock_proxy() -> (u16, ProxyState) {
    let state = ProxyState {
        received_auth_headers: Arc::new(Mutex::new(Vec::new())),
        received_bodies: Arc::new(Mutex::new(Vec::new())),
    };

    let app = Router::new()
        .route("/chat/completions", post(handle_chat_completions))
        .with_state(state.clone());

    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();

    tokio::spawn(async move {
        axum::serve(listener, app).await.unwrap();
    });

    tokio::time::sleep(std::time::Duration::from_millis(50)).await;
    (port, state)
}

#[tokio::test]
async fn test_mahomanaged_path_sends_bearer_token_and_model() {
    let _guard = env_lock().lock().await;

    let (port, proxy_state) = spawn_mock_proxy().await;
    let proxy_url = format!("http://127.0.0.1:{port}");
    unsafe { std::env::set_var("MAHO_PROXY_URL", &proxy_url) };

    let storage = Arc::new(MahoManagedStorage {
        access_token: Some("test-user-bearer-token".to_string()),
        messages: Arc::new(Mutex::new(Vec::new())),
    });

    let backend = SwiftideBackend::new(
        storage.clone() as Arc<dyn AgentStorage>,
        Some(Box::new(|_req| {
            Box::pin(async { maho_agent::PermissionDecision::Allow })
        })),
        std::env::temp_dir(),
        true,
    );

    let user_msg = ChatMessage::user(ChatContent::text("Say hello".to_string()));
    let result = backend
        .run_turn("session-maho-managed", user_msg, None, None)
        .await;

    unsafe { std::env::remove_var("MAHO_PROXY_URL") };

    let response = result.expect("MahoManaged run_turn should succeed");
    assert_eq!(response.role.as_str(), "assistant");
    match &response.content {
        ChatContent::Text(t) => {
            assert_eq!(t, "Hello from MahoManaged proxy!");
        }
        _ => panic!("Expected text content"),
    }

    let auth_headers = proxy_state.received_auth_headers.lock().unwrap();
    assert!(
        !auth_headers.is_empty(),
        "Proxy must receive at least one request"
    );
    assert_eq!(
        auth_headers[0], "Bearer test-user-bearer-token",
        "Authorization header must use the stored access_token"
    );

    let bodies = proxy_state.received_bodies.lock().unwrap();
    assert!(!bodies.is_empty(), "Proxy must receive a request body");
    let model = bodies[0]["model"].as_str().unwrap_or("");
    assert_eq!(
        model, "google/gemini-3-flash-lite:free",
        "MahoManaged path should use google/gemini-3-flash-lite:free model"
    );
}

#[tokio::test]
async fn test_byok_fallback_when_no_access_token() {
    let _guard = env_lock().lock().await;
    unsafe { std::env::remove_var("MAHO_PROXY_URL") };

    let storage = Arc::new(MahoManagedStorage {
        access_token: None,
        messages: Arc::new(Mutex::new(Vec::new())),
    });

    let backend = SwiftideBackend::new(
        storage.clone() as Arc<dyn AgentStorage>,
        Some(Box::new(|_req| {
            Box::pin(async { maho_agent::PermissionDecision::Allow })
        })),
        std::env::temp_dir(),
        true,
    );

    let user_msg = ChatMessage::user(ChatContent::text("Hello".to_string()));
    let result = backend.run_turn("session-byok", user_msg, None, None).await;

    // BYOK with fake key should fail (network/auth error to real openai)
    assert!(
        result.is_err(),
        "BYOK with fake key should fail (network/auth error)"
    );
    let err = result.unwrap_err().to_string();
    assert!(
        !err.contains("MahoManaged"),
        "Error should not mention MahoManaged; got: {err}"
    );
}

#[tokio::test]
async fn test_mahomanaged_stores_messages() {
    let _guard = env_lock().lock().await;

    let (port, _proxy_state) = spawn_mock_proxy().await;
    let proxy_url = format!("http://127.0.0.1:{port}");
    unsafe { std::env::set_var("MAHO_PROXY_URL", &proxy_url) };

    let storage = Arc::new(MahoManagedStorage {
        access_token: Some("store-test-token".to_string()),
        messages: Arc::new(Mutex::new(Vec::new())),
    });

    let backend = SwiftideBackend::new(
        storage.clone() as Arc<dyn AgentStorage>,
        Some(Box::new(|_req| {
            Box::pin(async { maho_agent::PermissionDecision::Allow })
        })),
        std::env::temp_dir(),
        true,
    );

    let user_msg = ChatMessage::user(ChatContent::text("Store test".to_string()));
    let _ = backend
        .run_turn("session-store", user_msg, None, None)
        .await;

    unsafe { std::env::remove_var("MAHO_PROXY_URL") };

    let messages = storage.messages.lock().unwrap();
    assert!(
        messages.len() >= 2,
        "Should store user + assistant messages"
    );
    assert_eq!(messages[0].1, "user");
    assert_eq!(messages[0].2, "Store test");
    let has_assistant = messages.iter().any(|(_, role, _)| role == "assistant");
    assert!(has_assistant, "Should have stored assistant response");
}
