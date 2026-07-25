use std::collections::HashMap;

use crate::approval::ToolSensitivity;

#[derive(Clone, Copy, Debug, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum FallbackTier {
    TypedDomainApi,
    DomRefLocator,
    AccessibilitySnapshot,
    ScreenshotCua,
}

impl FallbackTier {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::TypedDomainApi => "typed_domain_api",
            Self::DomRefLocator => "dom_ref_locator",
            Self::AccessibilitySnapshot => "accessibility_snapshot",
            Self::ScreenshotCua => "screenshot_cua",
        }
    }

}

#[derive(Clone, Copy, Debug, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum FallbackDecisionReason {
    TypedDomainAvailable,
    TypedDomainUnavailable,
    TypedDomainAlreadySucceeded,
    DomRefMissing,
    AccessibilitySelected,
    ScreenshotCuaApprovalRequired,
    ScreenshotCuaApproved,
    ScreenshotCuaDenied,
}

impl FallbackDecisionReason {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::TypedDomainAvailable => "typed_domain_available",
            Self::TypedDomainUnavailable => "typed_domain_unavailable",
            Self::TypedDomainAlreadySucceeded => "typed_domain_already_succeeded",
            Self::DomRefMissing => "dom_ref_missing",
            Self::AccessibilitySelected => "accessibility_selected",
            Self::ScreenshotCuaApprovalRequired => "screenshot_cua_approval_required",
            Self::ScreenshotCuaApproved => "screenshot_cua_approved",
            Self::ScreenshotCuaDenied => "screenshot_cua_denied",
        }
    }
}

#[derive(Clone, Copy, Debug, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum FallbackSafetyState {
    Safe,
    Downgraded,
    Blocked,
    EscalationApprovalRequired,
    EscalationApproved,
    EscalationDenied,
}

impl FallbackSafetyState {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Safe => "safe",
            Self::Downgraded => "downgraded",
            Self::Blocked => "blocked",
            Self::EscalationApprovalRequired => "escalation_approval_required",
            Self::EscalationApproved => "escalation_approved",
            Self::EscalationDenied => "escalation_denied",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ScreenshotCuaApproval {
    NotRequested,
    Approved,
    Denied,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FallbackSelectionRequest<'a> {
    pub tool_name: &'a str,
    pub requested_tier: FallbackTier,
    pub sensitivity: ToolSensitivity,
    pub screenshot_cua_approval: ScreenshotCuaApproval,
}

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
pub struct FallbackDecision {
    pub attempted_tier: FallbackTier,
    pub selected_tier: FallbackTier,
    pub reason: FallbackDecisionReason,
    pub safety_state: FallbackSafetyState,
    pub approval_required: bool,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum FallbackLadderError {
    UnsafeDowngrade(FallbackDecision),
    ScreenshotCuaApprovalRequired(FallbackDecision),
    ScreenshotCuaDenied(FallbackDecision),
}

impl FallbackLadderError {
    pub const fn decision(&self) -> &FallbackDecision {
        match self {
            Self::UnsafeDowngrade(decision)
            | Self::ScreenshotCuaApprovalRequired(decision)
            | Self::ScreenshotCuaDenied(decision) => decision,
        }
    }
}

#[derive(Clone, Copy, Debug, Hash, PartialEq, Eq)]
enum FallbackActionClass {
    Navigation,
    ElementAction,
    Keyboard,
    PageRead,
}

impl FallbackActionClass {
    fn for_tool_name(tool_name: &str) -> Self {
        match tool_name {
            "browser_navigate" | "browser_wait_for_navigation" => Self::Navigation,
            "browser_key_press" => Self::Keyboard,
            "read_current_page"
            | "get_selected_text"
            | "get_active_tab"
            | "search_in_page"
            | "extract_structured_page_context"
            | "browser_accessibility_snapshot"
            | "page_query_selector"
            | "page_get_text"
            | "page_get_attribute"
            | "page_wait_for_selector" => Self::PageRead,
            "browser_click" | "browser_type" | "browser_select" | "browser_scroll"
            | "browser_hover" => Self::ElementAction,
            _ => Self::ElementAction,
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FallbackLadderState {
    _session_id: String,
    progress_by_class: HashMap<FallbackActionClass, FallbackTier>,
}

impl FallbackLadderState {
    pub fn new(session_id: &str) -> Self {
        Self {
            _session_id: session_id.to_string(),
            progress_by_class: HashMap::new(),
        }
    }

    pub fn decide(
        &mut self,
        request: FallbackSelectionRequest<'_>,
    ) -> Result<FallbackDecision, FallbackLadderError> {
        let action_class = FallbackActionClass::for_tool_name(request.tool_name);
        let previous = self.progress_by_class.get(&action_class).copied();
        if previous == Some(FallbackTier::TypedDomainApi)
            && request.requested_tier != FallbackTier::TypedDomainApi
        {
            let decision = decision(
                request.requested_tier,
                FallbackTier::TypedDomainApi,
                FallbackDecisionReason::TypedDomainAlreadySucceeded,
                FallbackSafetyState::Blocked,
                false,
            );
            return Err(FallbackLadderError::UnsafeDowngrade(decision));
        }

        match request.requested_tier {
            FallbackTier::TypedDomainApi => {
                self.progress_by_class
                    .insert(action_class, FallbackTier::TypedDomainApi);
                Ok(decision(
                    FallbackTier::TypedDomainApi,
                    FallbackTier::TypedDomainApi,
                    FallbackDecisionReason::TypedDomainAvailable,
                    FallbackSafetyState::Safe,
                    false,
                ))
            }
            FallbackTier::DomRefLocator => {
                self.progress_by_class
                    .insert(action_class, FallbackTier::DomRefLocator);
                let state = if previous.is_some() {
                    FallbackSafetyState::Safe
                } else {
                    FallbackSafetyState::Downgraded
                };
                Ok(decision(
                    FallbackTier::TypedDomainApi,
                    FallbackTier::DomRefLocator,
                    FallbackDecisionReason::TypedDomainUnavailable,
                    state,
                    false,
                ))
            }
            FallbackTier::AccessibilitySnapshot => {
                self.progress_by_class
                    .insert(action_class, FallbackTier::AccessibilitySnapshot);
                Ok(decision(
                    FallbackTier::DomRefLocator,
                    FallbackTier::AccessibilitySnapshot,
                    if previous == Some(FallbackTier::DomRefLocator) {
                        FallbackDecisionReason::DomRefMissing
                    } else {
                        FallbackDecisionReason::AccessibilitySelected
                    },
                    FallbackSafetyState::Downgraded,
                    false,
                ))
            }
            FallbackTier::ScreenshotCua => self.decide_screenshot_cua(action_class, request),
        }
    }

    fn decide_screenshot_cua(
        &mut self,
        action_class: FallbackActionClass,
        request: FallbackSelectionRequest<'_>,
    ) -> Result<FallbackDecision, FallbackLadderError> {
        let approval_required = request.sensitivity == ToolSensitivity::Sensitive;
        match request.screenshot_cua_approval {
            ScreenshotCuaApproval::Approved => {
                self.progress_by_class
                    .insert(action_class, FallbackTier::ScreenshotCua);
                Ok(decision(
                    FallbackTier::AccessibilitySnapshot,
                    FallbackTier::ScreenshotCua,
                    FallbackDecisionReason::ScreenshotCuaApproved,
                    FallbackSafetyState::EscalationApproved,
                    approval_required,
                ))
            }
            ScreenshotCuaApproval::Denied => Err(FallbackLadderError::ScreenshotCuaDenied(
                decision(
                    FallbackTier::AccessibilitySnapshot,
                    FallbackTier::ScreenshotCua,
                    FallbackDecisionReason::ScreenshotCuaDenied,
                    FallbackSafetyState::EscalationDenied,
                    approval_required,
                ),
            )),
            ScreenshotCuaApproval::NotRequested => {
                Err(FallbackLadderError::ScreenshotCuaApprovalRequired(decision(
                    FallbackTier::AccessibilitySnapshot,
                    FallbackTier::ScreenshotCua,
                    FallbackDecisionReason::ScreenshotCuaApprovalRequired,
                    FallbackSafetyState::EscalationApprovalRequired,
                    true,
                )))
            }
        }
    }
}

fn decision(
    attempted_tier: FallbackTier,
    selected_tier: FallbackTier,
    reason: FallbackDecisionReason,
    safety_state: FallbackSafetyState,
    approval_required: bool,
) -> FallbackDecision {
    FallbackDecision {
        attempted_tier,
        selected_tier,
        reason,
        safety_state,
        approval_required,
    }
}
