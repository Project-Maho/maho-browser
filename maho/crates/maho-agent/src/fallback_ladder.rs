use std::collections::HashMap;

use crate::approval::ToolSensitivity;

#[derive(
    Clone, Copy, Debug, serde::Serialize, serde::Deserialize, PartialEq, Eq, PartialOrd, Ord,
)]
#[serde(rename_all = "snake_case")]
pub enum FallbackTier {
    TypedDomainApi,
    #[serde(alias = "page_adapter", alias = "adapter_tier", alias = "adapter")]
    ExactOriginPageAdapter,
    #[serde(
        alias = "dom_ref_locator",
        alias = "dom_tier",
        alias = "v2_locator",
        alias = "locator"
    )]
    DomRefLocator,
    #[serde(
        alias = "accessibility_snapshot",
        alias = "v1_ref",
        alias = "snapshot",
        alias = "accessibility"
    )]
    AccessibilitySnapshot,
    #[serde(alias = "screenshot_cua", alias = "screenshot", alias = "cua")]
    ScreenshotCua,
}

impl FallbackTier {
    #[allow(non_upper_case_globals)]
    pub const PageAdapter: Self = Self::ExactOriginPageAdapter;
    #[allow(non_upper_case_globals)]
    pub const AdapterTier: Self = Self::ExactOriginPageAdapter;
    #[allow(non_upper_case_globals)]
    pub const DomTier: Self = Self::DomRefLocator;
    #[allow(non_upper_case_globals)]
    pub const V2Locator: Self = Self::DomRefLocator;
    #[allow(non_upper_case_globals)]
    pub const V1Ref: Self = Self::AccessibilitySnapshot;
    #[allow(non_upper_case_globals)]
    pub const Screenshot: Self = Self::ScreenshotCua;

    pub const PAGE_ADAPTER: Self = Self::ExactOriginPageAdapter;
    pub const ADAPTER_TIER: Self = Self::ExactOriginPageAdapter;
    pub const DOM_TIER: Self = Self::DomRefLocator;
    pub const V2_LOCATOR: Self = Self::DomRefLocator;
    pub const V1_REF: Self = Self::AccessibilitySnapshot;
    pub const SCREENSHOT: Self = Self::ScreenshotCua;

    pub const fn as_str(self) -> &'static str {
        match self {
            Self::TypedDomainApi => "typed_domain_api",
            Self::ExactOriginPageAdapter => "exact_origin_page_adapter",
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
    PageAdapterAvailable,
    PageAdapterUnavailable,
    PageAdapterAlreadySucceeded,
    PageAdapterSelected,
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
            Self::PageAdapterAvailable => "page_adapter_available",
            Self::PageAdapterUnavailable => "page_adapter_unavailable",
            Self::PageAdapterAlreadySucceeded => "page_adapter_already_succeeded",
            Self::PageAdapterSelected => "page_adapter_selected",
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
    AccessibilitySnapshot,
    Click,
    Type,
    Select,
    Scroll,
    Hover,
    Keyboard,
    PageRead,
}

impl FallbackActionClass {
    fn for_tool_name(tool_name: &str) -> Self {
        match tool_name {
            "browser_navigate" | "browser_wait_for_navigation" => Self::Navigation,
            "browser_key_press" | "input.key_press" | "input_key_press" => Self::Keyboard,
            "read_current_page"
            | "list_browser_tabs"
            | "get_selected_text"
            | "get_active_tab"
            | "search_in_page"
            | "extract_structured_page_context"
            | "page_query_selector"
            | "page_get_text"
            | "page_get_attribute"
            | "page_wait_for_selector"
            | "browser_file_upload_select"
            | "input.file_upload_select" => Self::PageRead,
            "browser_accessibility_snapshot"
            | "page.accessibility_snapshot"
            | "page.accessibility_snapshot_v2"
            | "page_accessibility_snapshot_v2"
            | "browser_accessibility_snapshot_v2" => Self::AccessibilitySnapshot,
            "browser_click"
            | "input.click"
            | "input.locator_click"
            | "locator_click"
            | "browser_locator_click"
            | "input_locator_click"
            | "browser_act_and_observe"
            | "browser.act_and_observe" => Self::Click,
            "browser_type"
            | "input.type"
            | "input.locator_type"
            | "locator_type"
            | "browser_locator_type"
            | "input_locator_type" => Self::Type,
            "browser_select" | "input.select" => Self::Select,
            "browser_scroll" | "input.scroll" => Self::Scroll,
            "browser_hover" | "input.hover" => Self::Hover,
            _ => Self::PageRead,
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
        if previous == Some(FallbackTier::ExactOriginPageAdapter)
            && request.requested_tier > FallbackTier::ExactOriginPageAdapter
        {
            let decision = decision(
                request.requested_tier,
                FallbackTier::ExactOriginPageAdapter,
                FallbackDecisionReason::PageAdapterAlreadySucceeded,
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
            FallbackTier::ExactOriginPageAdapter => {
                self.progress_by_class
                    .insert(action_class, FallbackTier::ExactOriginPageAdapter);
                let state = if previous.is_some() {
                    FallbackSafetyState::Safe
                } else {
                    FallbackSafetyState::Downgraded
                };
                Ok(decision(
                    FallbackTier::TypedDomainApi,
                    FallbackTier::ExactOriginPageAdapter,
                    FallbackDecisionReason::TypedDomainUnavailable,
                    state,
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

    pub fn decide_with_adapter_registry(
        &mut self,
        tool_name: &str,
        origin: &str,
        operation: &str,
        registry: &crate::page_adapters::PageAdapterRegistry,
    ) -> Result<FallbackDecision, FallbackLadderError> {
        if registry.lookup(origin, operation).is_some() {
            self.decide(FallbackSelectionRequest {
                tool_name,
                requested_tier: FallbackTier::ExactOriginPageAdapter,
                sensitivity: ToolSensitivity::ReadOnly,
                screenshot_cua_approval: ScreenshotCuaApproval::NotRequested,
            })
        } else {
            self.decide(FallbackSelectionRequest {
                tool_name,
                requested_tier: FallbackTier::DomRefLocator,
                sensitivity: ToolSensitivity::ReadOnly,
                screenshot_cua_approval: ScreenshotCuaApproval::NotRequested,
            })
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
            ScreenshotCuaApproval::Denied => {
                Err(FallbackLadderError::ScreenshotCuaDenied(decision(
                    FallbackTier::AccessibilitySnapshot,
                    FallbackTier::ScreenshotCua,
                    FallbackDecisionReason::ScreenshotCuaDenied,
                    FallbackSafetyState::EscalationDenied,
                    approval_required,
                )))
            }
            ScreenshotCuaApproval::NotRequested => Err(
                FallbackLadderError::ScreenshotCuaApprovalRequired(decision(
                    FallbackTier::AccessibilitySnapshot,
                    FallbackTier::ScreenshotCua,
                    FallbackDecisionReason::ScreenshotCuaApprovalRequired,
                    FallbackSafetyState::EscalationApprovalRequired,
                    true,
                )),
            ),
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

#[cfg(test)]
mod tests {
    use super::*;

    fn request(tool_name: &str, tier: FallbackTier) -> FallbackSelectionRequest<'_> {
        FallbackSelectionRequest {
            tool_name,
            requested_tier: tier,
            sensitivity: ToolSensitivity::ReadOnly,
            screenshot_cua_approval: ScreenshotCuaApproval::NotRequested,
        }
    }

    #[test]
    fn page_read_does_not_block_snapshot_needed_for_element_refs() {
        let mut state = FallbackLadderState::new("form-session");

        state
            .decide(request("read_current_page", FallbackTier::TypedDomainApi))
            .expect("page read is allowed");
        state
            .decide(request("list_browser_tabs", FallbackTier::TypedDomainApi))
            .expect("tab listing is a page-read prerequisite");
        let snapshot = state.decide(request(
            "browser_accessibility_snapshot",
            FallbackTier::AccessibilitySnapshot,
        ));

        assert!(
            snapshot.is_ok(),
            "snapshot supplies @refs for click/type/select and is not a downgrade from a page read: {snapshot:?}"
        );
    }

    #[test]
    fn scroll_does_not_block_snapshot_and_click_sequence() {
        let mut state = FallbackLadderState::new("scroll-form-session");

        state
            .decide(request("browser_scroll", FallbackTier::TypedDomainApi))
            .expect("viewport scroll is allowed");
        state
            .decide(request(
                "browser_accessibility_snapshot",
                FallbackTier::AccessibilitySnapshot,
            ))
            .expect("snapshot after scrolling supplies fresh element refs");
        state
            .decide(request("browser_click", FallbackTier::DomRefLocator))
            .expect("click may consume the snapshot ref");
    }
}
