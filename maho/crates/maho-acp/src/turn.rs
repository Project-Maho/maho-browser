use serde_json::Value;

use crate::events::{PermissionOutcome, PromptResult, StopReason, PromptUsage};

#[derive(Debug, Clone)]
pub(crate) struct PromptParams {
    pub session_id: String,
    pub prompt: Vec<PromptContentPart>,
}

#[derive(Debug, Clone, serde::Serialize)]
pub(crate) struct PromptContentPart {
    #[serde(rename = "type")]
    pub content_type: String,
    pub text: String,
}

impl PromptParams {
    pub fn to_value(&self) -> Value {
        serde_json::json!({
            "sessionId": self.session_id,
            "prompt": self.prompt.iter().map(|p| {
                serde_json::json!({"type": &p.content_type, "text": &p.text})
            }).collect::<Vec<_>>()
        })
    }
}

pub fn parse_prompt_result(value: &Value) -> PromptResult {
    let stop_reason_str = value
        .get("stopReason")
        .or_else(|| value.get("stop_reason"))
        .and_then(|v| v.as_str())
        .unwrap_or("unknown");

    let usage: Option<PromptUsage> = value
        .get("usage")
        .and_then(|v| serde_json::from_value(v.clone()).ok());

    PromptResult {
        stop_reason: StopReason::parse(stop_reason_str),
        usage,
        raw: value.clone(),
    }
}

pub fn build_permission_reply(outcome: &PermissionOutcome) -> Value {
    match outcome {
        PermissionOutcome::Selected { option_id } => {
            serde_json::json!({
                "outcome": {
                    "outcome": "selected",
                    "optionId": option_id
                }
            })
        }
        PermissionOutcome::Cancelled => {
            serde_json::json!({
                "outcome": {
                    "outcome": "cancelled"
                }
            })
        }
    }
}

pub fn build_cancel_notification(session_id: &str) -> Value {
    serde_json::json!({
        "sessionId": session_id
    })
}
