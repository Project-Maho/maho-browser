#[path = "browser_action_shims/support.rs"]
mod support;

use std::sync::{Arc, Mutex};

use maho_agent::{AgentRuntime, PermissionDecision, PermissionRequest};
use maho_types::chat::{ChatContent, ChatMessage};
use support::{backend_with_profile_tools, has_tool, install_bridge, tool_names, OneToolCallLlm};
use swiftide_core::chat_completion::ChatCompletion;

#[tokio::test]
async fn browser_action_shims_list_tools_requires_bridge_and_flag() {
    let harness = backend_with_profile_tools(&[]);
    harness.backend.set_browser_action_tools_enabled(true);

    let without_bridge = tool_names(&harness.backend).await;

    assert!(!has_tool(&without_bridge, "browser_scroll"));
    assert!(!has_tool(&without_bridge, "browser_navigate"));

    let _bridge = install_bridge(&harness.backend);
    harness.backend.set_browser_action_tools_enabled(false);

    let flag_off = tool_names(&harness.backend).await;

    assert!(has_tool(&flag_off, "list_browser_tabs"));
    assert!(has_tool(&flag_off, "read_current_page"));
    assert!(!has_tool(&flag_off, "browser_scroll"));
    assert!(!has_tool(&flag_off, "browser_navigate"));

    harness.backend.set_browser_action_tools_enabled(true);

    let flag_on = tool_names(&harness.backend).await;

    assert!(has_tool(&flag_on, "browser_scroll"));
    assert!(has_tool(&flag_on, "browser_navigate"));
    assert!(has_tool(&flag_on, "browser_click"));
}

#[tokio::test]
async fn browser_action_shims_respect_profile_and_allowed_tools_filters() {
    let profile_harness = backend_with_profile_tools(&["browser_scroll"]);
    let _profile_bridge = install_bridge(&profile_harness.backend);
    profile_harness
        .backend
        .set_browser_action_tools_enabled(true);

    let profile_filtered = tool_names(&profile_harness.backend).await;

    assert!(has_tool(&profile_filtered, "browser_scroll"));
    assert!(!has_tool(&profile_filtered, "browser_click"));
    assert!(!has_tool(&profile_filtered, "list_browser_tabs"));

    let allowed_harness = backend_with_profile_tools(&["browser_scroll", "browser_click"]);
    let _allowed_bridge = install_bridge(&allowed_harness.backend);
    allowed_harness
        .backend
        .set_browser_action_tools_enabled(true);
    allowed_harness
        .backend
        .set_allowed_tools(Some(vec!["browser_scroll".to_string()]));

    let allowed_filtered = tool_names(&allowed_harness.backend).await;

    assert!(has_tool(&allowed_filtered, "browser_scroll"));
    assert!(!has_tool(&allowed_filtered, "browser_click"));
}

#[tokio::test]
async fn browser_action_shims_allow_invokes_bridge_exactly_once() {
    let harness = backend_with_profile_tools(&[]);
    let bridge = install_bridge(&harness.backend);
    harness.backend.set_browser_action_tools_enabled(true);
    harness
        .backend
        .set_llm_override(Box::new(OneToolCallLlm::new(
            "browser_scroll",
            r#"{"direction":"down","pixels":200}"#,
        )) as Box<dyn ChatCompletion>);
    let permission_requests = Arc::new(Mutex::new(Vec::<PermissionRequest>::new()));
    let captured_requests = Arc::clone(&permission_requests);
    harness
        .backend
        .set_permission_callback(Box::new(move |request| {
            let captured_requests = Arc::clone(&captured_requests);
            Box::pin(async move {
                support::push_locked(&captured_requests, request);
                PermissionDecision::Allow
            })
        }))
        .expect("permission callback installs");

    let result = harness
        .backend
        .run_turn(
            "session-browser-action-allow",
            ChatMessage::user(ChatContent::text("scroll down".to_string())),
            None,
            None,
        )
        .await;

    assert!(result.is_ok(), "allowed action turn completes: {result:?}");
    let calls = bridge.calls();
    assert_eq!(calls.len(), 1);
    assert_eq!(calls[0].0, "browser_scroll");
    assert_eq!(calls[0].1, r#"{"direction":"down","pixels":200}"#);
    let requests = support::clone_locked(&permission_requests);
    assert_eq!(requests.len(), 1);
    assert_eq!(requests[0].tool_name, "browser_scroll");
}

#[tokio::test]
async fn browser_action_shims_deny_prevents_bridge_invocation() {
    let harness = backend_with_profile_tools(&[]);
    let bridge = install_bridge(&harness.backend);
    harness.backend.set_browser_action_tools_enabled(true);
    harness
        .backend
        .set_llm_override(Box::new(OneToolCallLlm::new(
            "browser_navigate",
            r#"{"url":"https://example.com"}"#,
        )) as Box<dyn ChatCompletion>);
    let permission_calls = Arc::new(Mutex::new(0usize));
    let captured_calls = Arc::clone(&permission_calls);
    harness
        .backend
        .set_permission_callback(Box::new(move |_request| {
            let captured_calls = Arc::clone(&captured_calls);
            Box::pin(async move {
                support::increment_locked(&captured_calls);
                PermissionDecision::Deny
            })
        }))
        .expect("permission callback installs");

    let result = harness
        .backend
        .run_turn(
            "session-browser-action-deny",
            ChatMessage::user(ChatContent::text("open example".to_string())),
            None,
            None,
        )
        .await;

    assert!(
        result.is_ok(),
        "denied tool returns an error to the model: {result:?}"
    );
    assert_eq!(bridge.calls().len(), 0);
    assert_eq!(*support::lock(&permission_calls), 1);
}
