use serde_json::{json, Value};

use maho_acp::events::parse_session_update;
use maho_acp::{AcpUpdate, PermissionOutcome, StopReason, ToolCallStatus};

#[test]
fn happy_path_event_sequence() {
    let events = vec![
        json!({"update": {"sessionUpdate": "agent_message_chunk", "messageId": "m1", "content": {"type": "text", "text": "Hello"}}}),
        json!({"update": {"sessionUpdate": "agent_message_chunk", "messageId": "m1", "content": {"type": "text", "text": " world"}}}),
        json!({"update": {"sessionUpdate": "agent_message_chunk", "messageId": "m1", "content": {"type": "text", "text": "!"}}}),
        json!({"update": {"sessionUpdate": "usage_update", "used": 10, "size": 5, "cost": {"amount": 0.001, "currency": "USD"}}}),
    ];

    let mut text_acc = String::new();
    let mut saw_usage = false;

    for params in &events {
        match parse_session_update(params) {
            AcpUpdate::AgentMessageChunk { text, .. } => text_acc.push_str(&text),
            AcpUpdate::UsageUpdate { used, size, cost } => {
                assert_eq!(used, Some(10));
                assert_eq!(size, Some(5));
                let c = cost.unwrap();
                assert!((c.amount - 0.001).abs() < f64::EPSILON);
                assert_eq!(c.currency, "USD");
                saw_usage = true;
            }
            _ => panic!("unexpected event"),
        }
    }

    assert_eq!(text_acc, "Hello world!");
    assert!(saw_usage);
}

#[test]
fn tool_call_then_update_all_statuses() {
    let tc = json!({"update": {
        "sessionUpdate": "tool_call",
        "toolCallId": "tc1",
        "title": "Read",
        "kind": "read",
        "status": "in_progress",
        "locations": [],
        "input": {"path": "/tmp"}
    }});

    match parse_session_update(&tc) {
        AcpUpdate::ToolCall { id, title, .. } => {
            assert_eq!(id, "tc1");
            assert_eq!(title, "Read");
        }
        other => panic!("expected ToolCall, got {:?}", other),
    }

    for (status_str, expected) in [
        ("completed", ToolCallStatus::Completed),
        ("failed", ToolCallStatus::Failed),
        ("cancelled", ToolCallStatus::Cancelled),
        ("pending", ToolCallStatus::Pending),
    ] {
        let upd = json!({"update": {
            "sessionUpdate": "tool_call_update",
            "toolCallId": "tc1",
            "status": status_str,
            "content": [{"type": "text", "text": "output"}]
        }});
        match parse_session_update(&upd) {
            AcpUpdate::ToolCallUpdate {
                id,
                status,
                content_text,
                ..
            } => {
                assert_eq!(id, "tc1");
                assert_eq!(status, expected);
                assert_eq!(content_text.as_deref(), Some("output"));
            }
            other => panic!("expected ToolCallUpdate, got {:?}", other),
        }
    }
}

#[test]
fn unknown_discriminator_fallback() {
    let params = json!({"update": {"sessionUpdate": "future_type_2099", "data": true}});
    match parse_session_update(&params) {
        AcpUpdate::Other { method, .. } => assert_eq!(method, "future_type_2099"),
        other => panic!("expected Other, got {:?}", other),
    }
}

#[test]
fn jsonrpc_error_surfaces_typed() {
    let error = maho_acp::Error::JsonRpc {
        code: -32601,
        message: "Method not found".to_string(),
    };
    match &error {
        maho_acp::Error::JsonRpc { code, .. } => assert_eq!(*code, -32601),
        _ => panic!("expected JsonRpc error"),
    }
}

#[test]
fn permission_reply_uses_exact_json_rpc_id() {
    let outcome = PermissionOutcome::Selected {
        option_id: "once".to_string(),
    };
    let reply_body = maho_acp::events::PermissionRequest {
        json_rpc_id: 5,
        session_id: "s1".to_string(),
        tool_call: maho_acp::PermissionToolCall {
            id: "tc1".to_string(),
            tool: "write".to_string(),
            extra: Value::Null,
        },
        options: vec![],
        raw_params: Value::Null,
    };

    assert_eq!(reply_body.json_rpc_id, 5);

    let reply_value = maho_acp::turn::build_permission_reply(&outcome);
    assert_eq!(reply_value["outcome"]["outcome"], "selected");
    assert_eq!(reply_value["outcome"]["optionId"], "once");
}

#[test]
fn permission_reply_cancelled_shape() {
    let reply = maho_acp::turn::build_permission_reply(&PermissionOutcome::Cancelled);
    assert_eq!(reply["outcome"]["outcome"], "cancelled");
}

#[test]
fn stop_reason_end_turn() {
    let result = json!({"stopReason": "end_turn", "usage": {"inputTokens": 10}});
    let parsed = maho_acp::turn::parse_prompt_result(&result);
    assert_eq!(parsed.stop_reason, StopReason::EndTurn);
    assert!(parsed.usage.is_some());
}

#[test]
fn stop_reason_cancelled() {
    let result = json!({"stopReason": "cancelled"});
    let parsed = maho_acp::turn::parse_prompt_result(&result);
    assert_eq!(parsed.stop_reason, StopReason::Cancelled);
}

#[tokio::test]
#[ignore]
async fn full_prompt_turn_e2e() {
    let opencode_binary =
        std::env::var("OPENCODE_BINARY").unwrap_or_else(|_| "opencode".to_string());

    let which = std::process::Command::new("which")
        .arg(&opencode_binary)
        .output();

    match which {
        Ok(output) if output.status.success() => {}
        _ => {
            eprintln!("SKIP: `{}` not found on PATH", opencode_binary);
            return;
        }
    }

    let client = maho_acp::AcpClient::start(&opencode_binary)
        .await
        .expect("failed to start opencode acp");

    let init = client.initialize().await.expect("initialize failed");
    assert_eq!(init.protocol_version, 1);
    eprintln!("initialized: protocolVersion={}", init.protocol_version);

    let session = client
        .new_session(std::env::current_dir().unwrap(), vec![])
        .await
        .expect("session/new failed");
    eprintln!("session_id: {}", session.session_id);
    assert!(!session.session_id.is_empty());

    let mut handle = client
        .submit_prompt(
            &session.session_id,
            "Reply with the single word PROBE_OK and nothing else.".to_string(),
        )
        .await
        .expect("submit_prompt failed");

    let mut text_acc = String::new();
    let mut got_message_chunk = false;

    loop {
        tokio::select! {
            event = handle.events.recv() => {
                match event {
                    Some(AcpUpdate::AgentMessageChunk { text, .. }) => {
                        text_acc.push_str(&text);
                        got_message_chunk = true;
                    }
                    Some(_) => {}
                    None => break,
                }
            }
            perm = handle.permission_requests.recv() => {
                if let Some(req) = perm {
                    eprintln!("auto-rejecting permission request id={}", req.json_rpc_id);
                    client.reply_permission(
                        req.json_rpc_id,
                        PermissionOutcome::Selected { option_id: "reject".to_string() },
                    ).await.expect("reply_permission failed");
                }
            }
        }
    }

    let result = handle.await_completion().await.expect("prompt failed");
    eprintln!("stop_reason: {:?}", result.stop_reason);
    eprintln!("accumulated text: {}", text_acc.trim());

    assert!(got_message_chunk, "expected at least one AgentMessageChunk");
    assert_eq!(result.stop_reason, StopReason::EndTurn);
    assert!(
        text_acc.contains("PROBE_OK"),
        "expected PROBE_OK in response, got: {}",
        text_acc
    );

    client.shutdown().await.expect("shutdown failed");
}
