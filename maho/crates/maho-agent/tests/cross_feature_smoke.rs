//! Cross-feature smoke test (Part D): end-to-end agent stack verification.
//!
//! Single test: signup mock user (tier=Max) → store access_token →
//! invoke Tab Tidy via maho-core → SwiftideBackend routes to mock proxy →
//! mock proxy returns folder list → assert the full chain delivered.

use std::sync::{Arc, Mutex, OnceLock};

use async_trait::async_trait;
use axum::{Router, extract::State, http::StatusCode, routing::post};
use maho_agent::{AgentError, AgentRuntime, AgentStorage, SwiftideBackend, ZeroizedString};
use maho_types::chat::{ChatContent, ChatMessage};
use maho_types::identifiers::{SpaceId, TabId};
use tokio::net::TcpListener;

static ENV_MUTEX: OnceLock<tokio::sync::Mutex<()>> = OnceLock::new();
fn env_lock() -> &'static tokio::sync::Mutex<()> {
    ENV_MUTEX.get_or_init(|| tokio::sync::Mutex::new(()))
}

// ---------------------------------------------------------------------------
// Mock Storage: Max-tier user with access_token
// ---------------------------------------------------------------------------

#[derive(Clone)]
struct MaxTierStorage {
    access_token: String,
    messages: Arc<Mutex<Vec<(String, String, String)>>>,
}

#[async_trait]
impl AgentStorage for MaxTierStorage {
    async fn get_byok_key(&self, _provider: &str) -> Result<Option<ZeroizedString>, AgentError> {
        Ok(None)
    }

    async fn get_setting(&self, key: &str) -> Result<Option<ZeroizedString>, AgentError> {
        if key == "auth:access_token" {
            Ok(Some(ZeroizedString::new(self.access_token.clone())))
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

// ---------------------------------------------------------------------------
// Mock Proxy: returns a tidy-tabs folder JSON response
// ---------------------------------------------------------------------------

#[derive(Clone)]
struct TidyProxyState {
    received_requests: Arc<Mutex<Vec<serde_json::Value>>>,
}

async fn handle_tidy_completions(
    State(state): State<TidyProxyState>,
    body: axum::body::Bytes,
) -> (StatusCode, String) {
    if let Ok(json) = serde_json::from_slice::<serde_json::Value>(&body) {
        state.received_requests.lock().unwrap().push(json);
    }

    // Return an assistant message containing the folder list JSON
    let folder_response = serde_json::json!({
        "folders": [
            {"name": "Development", "tab_ids": ["tab_1", "tab_2", "tab_3"]},
            {"name": "Social", "tab_ids": ["tab_4", "tab_5"]},
            {"name": "News", "tab_ids": ["tab_6", "tab_7", "tab_8"]}
        ]
    });

    let response_body = serde_json::json!({
        "id": "chatcmpl-tidy-001",
        "object": "chat.completion",
        "created": 1700000000,
        "model": "google/gemini-2.5-flash",
        "choices": [{
            "index": 0,
            "message": {
                "role": "assistant",
                "content": folder_response.to_string()
            },
            "finish_reason": "stop"
        }],
        "usage": {"prompt_tokens": 50, "completion_tokens": 20, "total_tokens": 70}
    });

    (StatusCode::OK, response_body.to_string())
}

async fn spawn_tidy_proxy() -> (u16, TidyProxyState) {
    let state = TidyProxyState {
        received_requests: Arc::new(Mutex::new(Vec::new())),
    };

    let app = Router::new()
        .route("/chat/completions", post(handle_tidy_completions))
        .with_state(state.clone());

    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();

    tokio::spawn(async move {
        axum::serve(listener, app).await.unwrap();
    });

    tokio::time::sleep(std::time::Duration::from_millis(50)).await;
    (port, state)
}

// ---------------------------------------------------------------------------
// Cross-feature smoke test: full chain
// ---------------------------------------------------------------------------

#[tokio::test]
async fn test_cross_feature_agent_stack_e2e() {
    let _guard = env_lock().lock().await;

    let (port, proxy_state) = spawn_tidy_proxy().await;
    let proxy_url = format!("http://127.0.0.1:{port}");
    unsafe { std::env::set_var("MAHO_PROXY_URL", &proxy_url) };

    // Step 2: Create Max-tier storage with access_token
    let storage = Arc::new(MaxTierStorage {
        access_token: "max-tier-user-token".to_string(),
        messages: Arc::new(Mutex::new(Vec::new())),
    });

    // Step 3: Create SwiftideBackend (MahoManaged path)
    let backend = SwiftideBackend::new(
        storage.clone() as Arc<dyn AgentStorage>,
        Some(Box::new(|_req| {
            Box::pin(async { maho_agent::PermissionDecision::Allow })
        })),
        std::env::temp_dir(),
        true,
    );

    // Step 4: Use LLMManager to build the Tab Tidy request prompt
    let mut llm_manager = maho_core::llm_manager::LLMManager::new();
    let space_id = SpaceId::new("space-1");
    let tabs: Vec<(TabId, String, String)> = vec![
        (TabId::new("tab_1"), "Rust Docs".into(), "https://doc.rust-lang.org".into()),
        (TabId::new("tab_2"), "GitHub PR".into(), "https://github.com/pulls".into()),
        (TabId::new("tab_3"), "VS Code".into(), "https://code.visualstudio.com".into()),
        (TabId::new("tab_4"), "Twitter".into(), "https://twitter.com".into()),
        (TabId::new("tab_5"), "LinkedIn".into(), "https://linkedin.com".into()),
        (TabId::new("tab_6"), "HN".into(), "https://news.ycombinator.com".into()),
        (TabId::new("tab_7"), "TechCrunch".into(), "https://techcrunch.com".into()),
        (TabId::new("tab_8"), "The Verge".into(), "https://theverge.com".into()),
    ];

    let core_update = llm_manager.request_tidy_tabs(&space_id, tabs);

    // Extract the prompt from the CoreUpdate
    let prompt = match &core_update {
        maho_types::events::core_update::CoreUpdate::RequestLlmCompletion { prompt, .. } => {
            prompt.clone()
        }
        _ => panic!("Expected RequestLlmCompletion"),
    };

    // Step 5: Send prompt through SwiftideBackend → mock proxy
    let user_msg = ChatMessage::user(ChatContent::text(prompt));
    let result = backend
        .run_turn("session-tidy-e2e", user_msg, None)
        .await;

    unsafe { std::env::remove_var("MAHO_PROXY_URL") };

    // Step 6: Verify the response came through
    let response = result.expect("Cross-feature e2e should succeed");
    assert_eq!(response.role.as_str(), "assistant");

    let response_text = match &response.content {
        ChatContent::Text(t) => t.clone(),
        _ => panic!("Expected text content"),
    };

    // Step 7: Parse the LLM response as folder list (simulating what handle_result does)
    let request_id = match &core_update {
        maho_types::events::core_update::CoreUpdate::RequestLlmCompletion {
            request_id, ..
        } => request_id.clone(),
        _ => unreachable!(),
    };

    let updates = llm_manager.handle_result(&request_id, &response_text);
    assert!(!updates.is_empty(), "handle_result should produce updates");

    // Verify TidyTabsReady event with correct folders
    match &updates[0] {
        maho_types::events::core_update::CoreUpdate::TidyTabsReady {
            space_id: sid,
            folders,
        } => {
            assert_eq!(sid, &space_id);
            assert_eq!(folders.len(), 3);

            let dev_folder = folders.iter().find(|f| f.name == "Development").unwrap();
            assert_eq!(dev_folder.tab_ids.len(), 3);
            assert!(dev_folder.tab_ids.contains(&TabId::new("tab_1")));

            let social_folder = folders.iter().find(|f| f.name == "Social").unwrap();
            assert_eq!(social_folder.tab_ids.len(), 2);

            let news_folder = folders.iter().find(|f| f.name == "News").unwrap();
            assert_eq!(news_folder.tab_ids.len(), 3);
        }
        other => panic!("Expected TidyTabsReady; got: {other:?}"),
    }

    // Step 8: Verify pending_tidy_tabs is populated
    let pending = llm_manager.take_pending_tidy_tabs();
    assert!(pending.is_some());
    let (pending_space, pending_folders) = pending.unwrap();
    assert_eq!(pending_space, space_id);
    assert_eq!(pending_folders.len(), 3);

    // Step 9: Verify proxy received at least one request
    let requests = proxy_state.received_requests.lock().unwrap();
    assert!(!requests.is_empty(), "Proxy must have received requests");
}

// ---------------------------------------------------------------------------
// Test: Tier gate prevents non-Max users from running routines
// ---------------------------------------------------------------------------

#[tokio::test]
async fn test_routine_tier_gate_integration() {
    use maho_core::routines::{run_routine, UserTier, RoutineError};

    // Free user → rejected
    let result = run_routine("morning_briefing", UserTier::Free, |_prompt| async {
        Ok("should not run".to_string())
    })
    .await;
    assert_eq!(result.unwrap_err(), RoutineError::TierLockedToMax);

    // Max user → runs prompt
    let result = run_routine("weekly_tab_tidy", UserTier::Max, |prompt| async move {
        assert!(prompt.contains("Weekly Tab Tidy"));
        Ok("folders result".to_string())
    })
    .await;
    let res = result.unwrap();
    assert!(res.success);
    assert_eq!(res.content, "folders result");
}
