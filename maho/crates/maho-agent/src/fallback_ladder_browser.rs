use crate::approval::ToolSensitivity;
use crate::fallback_ladder::{FallbackSelectionRequest, FallbackTier, ScreenshotCuaApproval};

pub fn browser_tool_fallback_request<'a>(
    tool_name: &'a str,
    args_json: &str,
    sensitivity: ToolSensitivity,
) -> FallbackSelectionRequest<'a> {
    let parsed = serde_json::from_str::<serde_json::Value>(args_json).ok();
    let requested_tier = infer_requested_tier(tool_name, parsed.as_ref());
    let screenshot_cua_approval = match parsed.as_ref().and_then(|value| {
        value
            .get("screenshot_cua_approved")
            .and_then(serde_json::Value::as_bool)
    }) {
        Some(true) => ScreenshotCuaApproval::Approved,
        Some(false) => ScreenshotCuaApproval::Denied,
        Option::None => ScreenshotCuaApproval::NotRequested,
    };
    FallbackSelectionRequest {
        tool_name,
        requested_tier,
        sensitivity,
        screenshot_cua_approval,
    }
}

fn infer_requested_tier(tool_name: &str, args: Option<&serde_json::Value>) -> FallbackTier {
    if requests_screenshot_cua(tool_name, args) {
        return FallbackTier::ScreenshotCua;
    }
    match tool_name {
        "browser_navigate" | "browser_key_press" => FallbackTier::TypedDomainApi,
        "page_query_selector"
        | "page_get_text"
        | "page_get_attribute"
        | "page_wait_for_selector"
        | "browser_wait_for_navigation" => FallbackTier::DomRefLocator,
        "browser_accessibility_snapshot" => FallbackTier::AccessibilitySnapshot,
        "browser_click" | "browser_type" | "browser_select" | "browser_scroll"
        | "browser_hover" => element_action_tier(tool_name, args),
        _ => FallbackTier::TypedDomainApi,
    }
}

fn element_action_tier(tool_name: &str, args: Option<&serde_json::Value>) -> FallbackTier {
    if let Some(value) = args {
        if value.get("selector").is_some() || value.get("ref_id").is_some() {
            return FallbackTier::DomRefLocator;
        }
        if tool_name == "browser_scroll" && value.get("ref").is_none() {
            return FallbackTier::TypedDomainApi;
        }
    }
    FallbackTier::AccessibilitySnapshot
}

fn requests_screenshot_cua(tool_name: &str, args: Option<&serde_json::Value>) -> bool {
    if tool_name.contains("screenshot") || tool_name.contains("cua") {
        return true;
    }
    args.is_some_and(|value| {
        value
            .get("fallback_tier")
            .and_then(serde_json::Value::as_str)
            == Some("screenshot_cua")
            || value
                .get("screenshot_cua")
                .and_then(serde_json::Value::as_bool)
                .unwrap_or(false)
    })
}
