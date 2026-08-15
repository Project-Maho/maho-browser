#[path = "browser_action_shims/support.rs"]
mod support;

use std::sync::{Arc, Mutex};

use maho_agent::{AgentRuntime, PermissionDecision, PermissionRequest};
use maho_types::chat::{ChatContent, ChatMessage};
use support::{backend_with_profile_tools, has_tool, install_bridge, tool_names, OneToolCallLlm};
use swiftide_core::chat_completion::ChatCompletion;

#[tokio::test]
async fn static_browser_shim_discovery_is_separate_from_rust_owned_platform_tools() {
    let harness = backend_with_profile_tools(&[]);
    let local_before_bridge = tool_names(&harness.backend).await;

    assert!(has_tool(&local_before_bridge, "fs_read"));
    assert!(has_tool(&local_before_bridge, "fs_write"));
    assert!(has_tool(&local_before_bridge, "shell_exec"));
    assert!(!has_tool(&local_before_bridge, "list_browser_tabs"));

    let _bridge = install_bridge(&harness.backend);
    let with_bridge = tool_names(&harness.backend).await;

    assert!(has_tool(&with_bridge, "list_browser_tabs"));
    assert!(has_tool(&with_bridge, "read_current_page"));
    assert!(has_tool(&with_bridge, "fs_read"));
    assert!(has_tool(&with_bridge, "fs_write"));
    assert!(has_tool(&with_bridge, "shell_exec"));
}

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
    assert!(!has_tool(&flag_off, "browser_accessibility_snapshot"));

    harness.backend.set_browser_action_tools_enabled(true);

    let flag_on = tool_names(&harness.backend).await;

    assert!(has_tool(&flag_on, "browser_scroll"));
    assert!(has_tool(&flag_on, "browser_navigate"));
    assert!(has_tool(&flag_on, "browser_click"));
    assert!(
        has_tool(&flag_on, "browser_accessibility_snapshot"),
        "click/type/select require @ref values from the accessibility snapshot"
    );
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
    assert_eq!(
        calls
            .iter()
            .filter(|(tool_name, _)| tool_name == "browser_tab_list")
            .count(),
        1
    );
    let action_calls: Vec<_> = calls
        .iter()
        .filter(|(tool_name, _)| tool_name == "browser_scroll")
        .collect();
    assert_eq!(action_calls.len(), 1);
    assert_eq!(action_calls[0].0, "browser_scroll");
    let action_args: serde_json::Value =
        serde_json::from_str(&action_calls[0].1).expect("browser action args are JSON");
    assert_eq!(action_args["direction"], "down");
    assert_eq!(action_args["pixels"], 200);
    assert_eq!(action_args["tab_id"], 1);
    assert_eq!(
        calls
            .iter()
            .filter(|(tool_name, _)| tool_name == "browser_acquire_lease")
            .count(),
        1,
        "the mutation must acquire exactly one non-stealing lease"
    );
    // Ordinary scrolling is classified from the canonical action descriptor
    // plus arguments and proceeds without a modal. Browser tab/lease checks are
    // still exercised above and remain the enforcement boundary.
    let requests = support::clone_locked(&permission_requests);
    assert!(
        requests.is_empty(),
        "ordinary scroll should not prompt: {requests:?}"
    );
}

#[tokio::test]
async fn tool_using_turn_streams_only_the_authoritative_final_answer() {
    let harness = backend_with_profile_tools(&[]);
    let _bridge = install_bridge(&harness.backend);
    harness.backend.set_browser_action_tools_enabled(true);
    harness
        .backend
        .set_llm_override(Box::new(OneToolCallLlm::new(
            "browser_scroll",
            r#"{"direction":"down","pixels":200}"#,
        )) as Box<dyn ChatCompletion>);
    harness
        .backend
        .set_permission_callback(Box::new(|_| Box::pin(async { PermissionDecision::Allow })))
        .expect("permission callback installs");
    let streamed = Arc::new(Mutex::new(String::new()));
    let captured = Arc::clone(&streamed);

    let response = harness
        .backend
        .run_turn(
            "session-authoritative-stream",
            ChatMessage::user(ChatContent::text("scroll down".to_string())),
            Some(Box::new(move |chunk| {
                support::lock(&captured).push_str(chunk);
            })),
            None,
        )
        .await
        .expect("tool-using turn completes");
    let returned = match response.content {
        ChatContent::Text(text) => text,
        other => panic!("expected text response, got {other:?}"),
    };

    assert_eq!(support::clone_locked(&streamed), returned);
}

#[tokio::test]
async fn consequential_browser_action_requires_permission_callback() {
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
        "denied action is returned as a tool observation: {result:?}"
    );
    assert_eq!(
        *support::lock(&permission_calls),
        1,
        "new-origin navigation must consult the approval callback exactly once"
    );
    let calls = bridge.calls();
    assert!(
        !calls
            .iter()
            .any(|(tool_name, _)| tool_name == "browser_navigate"),
        "denied navigation must not reach the browser bridge: {calls:?}"
    );
}
