//! Real native-input dispatcher for the visual CUA pipeline.
//!
//! Maps validated [`NativeActionRequest`]s onto the browser MCP native-input
//! tools (`browser_visual_click`, `browser_native_type`,
//! `browser_native_key`). The browser enforces the frame-token, lease and
//! frontmost-window gates; every refusal it returns surfaces as a typed
//! [`VisualCuaError::DispatchError`], never as success.

use serde_json::{json, Value};

use crate::mcp_client::McpClient;
use crate::visual_cua::{NativeAction, NativeActionDispatcher, NativeActionRequest, VisualCuaError};

/// The browser tool surface the dispatcher needs; implemented by [`McpClient`].
#[async_trait::async_trait]
pub trait BrowserToolCaller: Send + Sync {
    async fn call_browser_tool(&self, name: &str, args: Value) -> anyhow::Result<Value>;
}

#[async_trait::async_trait]
impl BrowserToolCaller for McpClient {
    async fn call_browser_tool(&self, name: &str, args: Value) -> anyhow::Result<Value> {
        self.call_tool(name, args).await
    }
}

pub struct McpNativeActionDispatcher<C: BrowserToolCaller> {
    caller: C,
}

impl<C: BrowserToolCaller> McpNativeActionDispatcher<C> {
    pub fn new(caller: C) -> Self {
        Self { caller }
    }
}

/// Browser tool name and arguments for one native action.
pub fn native_action_tool_call(request: &NativeActionRequest) -> (&'static str, Value) {
    let mut args = json!({
        "tab_id": request.tab_id,
        "frame_token": request.frame_token,
        "lease_epoch": request.lease_epoch,
    });
    let tool = match &request.action {
        NativeAction::Click {
            click_point_css,
            target_rect_css,
            ..
        } => {
            args["click_point_css"] = json!({ "x": click_point_css.x, "y": click_point_css.y });
            args["target_rect_css"] = json!({
                "x": target_rect_css.x,
                "y": target_rect_css.y,
                "width": target_rect_css.width,
                "height": target_rect_css.height,
            });
            "browser_visual_click"
        }
        NativeAction::Type { text } => {
            args["text"] = json!(text);
            "browser_native_type"
        }
        NativeAction::Key { key, modifiers } => {
            args["key"] = json!(key);
            args["modifiers"] = json!(modifiers);
            "browser_native_key"
        }
    };
    (tool, args)
}

fn tool_result_error(result: &Value) -> Option<String> {
    if let Some(error) = result.get("error") {
        let code = error.get("code").and_then(Value::as_i64).unwrap_or_default();
        let message = error
            .get("message")
            .and_then(Value::as_str)
            .unwrap_or("browser refused the native action");
        return Some(format!("{code}: {message}"));
    }
    if result.get("isError").and_then(Value::as_bool) == Some(true) {
        let text = result
            .get("content")
            .and_then(Value::as_array)
            .and_then(|items| items.iter().find_map(|item| item.get("text")?.as_str()))
            .unwrap_or("browser refused the native action");
        return Some(text.to_string());
    }
    None
}

#[async_trait::async_trait]
impl<C: BrowserToolCaller> NativeActionDispatcher for McpNativeActionDispatcher<C> {
    async fn dispatch_native_action(
        &self,
        request: &NativeActionRequest,
    ) -> Result<Value, VisualCuaError> {
        request.validate()?;
        if request.user_modifiers_active {
            return Err(VisualCuaError::DispatchError(
                "user is holding modifier keys; native input deferred".into(),
            ));
        }
        let (tool, args) = native_action_tool_call(request);
        let result = self
            .caller
            .call_browser_tool(tool, args)
            .await
            .map_err(|error| VisualCuaError::DispatchError(error.to_string()))?;
        if let Some(error) = tool_result_error(&result) {
            return Err(VisualCuaError::DispatchError(error));
        }
        Ok(result)
    }
}
