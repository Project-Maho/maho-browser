use crate::approval::ToolSensitivity;
use crate::fallback_ladder::{
    tool_profile, FallbackSelectionRequest, FallbackTier, ScreenshotCuaApproval,
};

pub fn browser_tool_fallback_request<'a>(
    tool_name: &'a str,
    args_json: &str,
    sensitivity: ToolSensitivity,
) -> FallbackSelectionRequest<'a> {
    let parsed = serde_json::from_str::<serde_json::Value>(args_json).ok();
    let requested_tier = infer_requested_tier(tool_name, parsed.as_ref());
    // Security invariant: NEVER trust model-supplied screenshot_cua_approved as consent.
    // Explicit human/system escalation approval is required and cannot be granted via
    // unverified tool call arguments from the model.
    let screenshot_cua_approval = match parsed.as_ref().and_then(|value| {
        value
            .get("screenshot_cua_approved")
            .and_then(serde_json::Value::as_bool)
    }) {
        Some(false) => ScreenshotCuaApproval::Denied,
        _ => ScreenshotCuaApproval::NotRequested,
    };
    FallbackSelectionRequest {
        tool_name,
        requested_tier,
        sensitivity,
        screenshot_cua_approval,
    }
}

fn infer_requested_tier(tool_name: &str, args: Option<&serde_json::Value>) -> FallbackTier {
    // Tier comes from the same table as the ladder's action class, so the two can
    // never disagree; unknown tools get its most conservative profile.
    let profile = tool_profile(tool_name);
    if profile.tier == FallbackTier::ScreenshotCua || args_request_screenshot_cua(args) {
        return FallbackTier::ScreenshotCua;
    }
    if profile.element_action {
        element_action_tier(tool_name, args)
    } else {
        profile.tier
    }
}

fn element_action_tier(tool_name: &str, args: Option<&serde_json::Value>) -> FallbackTier {
    if let Some(value) = args {
        if value.get("selector").is_some()
            || value.get("ref_id").is_some()
            || value.get("locator").is_some()
            || value.get("css").is_some()
        {
            return FallbackTier::DomRefLocator;
        }
        if (tool_name == "browser_scroll" || tool_name == "input.scroll")
            && value.get("ref").is_none()
        {
            return FallbackTier::TypedDomainApi;
        }
    }
    FallbackTier::AccessibilitySnapshot
}

fn args_request_screenshot_cua(args: Option<&serde_json::Value>) -> bool {
    args.is_some_and(|value| {
        value
            .get("fallback_tier")
            .and_then(serde_json::Value::as_str)
            == Some("screenshot_cua")
            || value
                .get("screenshot_cua")
                .and_then(serde_json::Value::as_bool)
                .unwrap_or(false)
            || value
                .get("sentinel")
                .and_then(serde_json::Value::as_bool)
                .unwrap_or(false)
    })
}
