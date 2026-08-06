// Shared-core coverage: run_turn_internal is reached by both the CLI and the
// in-browser agent (via the FFI browser_tool_cb bridge), so these assertions
// hold for both surfaces.
#[path = "browser_action_shims/support.rs"]
mod support;

use std::sync::{Arc, Mutex};

use async_trait::async_trait;
use maho_agent::{AgentRuntime, PermissionDecision};
use maho_types::chat::{ChatContent, ChatMessage};
use support::{backend_with_profile_tools, install_bridge, lock};
use swiftide_core::chat_completion::{
    errors::LanguageModelError, ChatCompletion, ChatCompletionRequest, ChatCompletionResponse,
    ToolCall,
};

const SYNTH: &str = "SYNTHESIZED-FINAL-ANSWER";

#[derive(Clone)]
enum SecondTurn {
    Empty,
    IntentText(String),
}

#[derive(Clone)]
struct ScriptedLlm {
    calls: Arc<Mutex<usize>>,
    second: SecondTurn,
}

#[async_trait]
impl ChatCompletion for ScriptedLlm {
    async fn complete(
        &self,
        _request: &ChatCompletionRequest,
    ) -> Result<ChatCompletionResponse, LanguageModelError> {
        let n = {
            let mut c = lock(&self.calls);
            *c += 1;
            *c
        };
        if n == 1 {
            let tc = ToolCall::builder()
                .id("call-1".to_string())
                .name("browser_scroll".to_string())
                .args(r#"{"direction":"down","pixels":100}"#.to_string())
                .build()
                .expect("tool call builds");
            return Ok(ChatCompletionResponse::builder()
                .tool_calls(vec![tc])
                .build()
                .expect("tool-call response builds"));
        }
        if n == 2 {
            return match &self.second {
                SecondTurn::Empty => Ok(ChatCompletionResponse::builder()
                    .build()
                    .expect("empty response builds")),
                SecondTurn::IntentText(t) => {
                    let mut r = ChatCompletionResponse::builder()
                        .build()
                        .expect("intent response builds");
                    r.append_message_delta(Some(t.as_str()));
                    Ok(r)
                }
            };
        }
        let mut r = ChatCompletionResponse::builder()
            .build()
            .expect("synth response builds");
        r.append_message_delta(Some(SYNTH));
        Ok(r)
    }
}

async fn final_answer(second: SecondTurn) -> String {
    let harness = backend_with_profile_tools(&[]);
    let _bridge = install_bridge(&harness.backend);
    harness.backend.set_browser_action_tools_enabled(true);
    harness
        .backend
        .set_permission_callback(Box::new(|_req| {
            Box::pin(async { PermissionDecision::Allow })
        }))
        .expect("permission callback installs");
    harness.backend.set_llm_override(Box::new(ScriptedLlm {
        calls: Arc::new(Mutex::new(0)),
        second,
    }) as Box<dyn ChatCompletion>);

    let msg = harness
        .backend
        .run_turn(
            "session-synthesis",
            ChatMessage::user(ChatContent::text("do the task".to_string())),
            None,
            None,
        )
        .await
        .expect("turn completes");
    match msg.content {
        ChatContent::Text(t) => t,
        other => panic!("expected text answer, got {other:?}"),
    }
}

#[tokio::test]
async fn synthesis_fires_when_loop_ends_on_raw_tool_output() {
    let ans = final_answer(SecondTurn::Empty).await;
    assert_eq!(
        ans, SYNTH,
        "loop ending with no assistant answer must synthesize a final answer, not surface raw \
         tool output; got: {ans:?}"
    );
    assert!(
        !ans.contains("scrolled") && !ans.contains("\"ok\""),
        "raw bridge JSON must never leak as the user-facing answer; got: {ans:?}"
    );
}

#[tokio::test]
async fn synthesis_fires_on_unfinished_intent_narration() {
    let ans = final_answer(SecondTurn::IntentText(
        "I will navigate to example.com to continue the task".to_string(),
    ))
    .await;
    assert_eq!(
        ans, SYNTH,
        "an 'I will ...' intent narration must be replaced by a synthesized answer; got: {ans:?}"
    );
    assert!(
        !ans.starts_with("I will"),
        "intent narration must not surface as the final answer; got: {ans:?}"
    );
}
