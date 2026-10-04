// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_capability_broker.h"

#include <algorithm>
#include <vector>

#include "base/check.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "maho/browser/ai/maho_browser_action_contract.h"

namespace maho::ai {

constexpr CapabilityFeatureGate kNativeInputBrokerGate =
    static_cast<CapabilityFeatureGate>(100);

std::string ParseRuntimeConfigTier(std::string_view raw_tier) {
  if (raw_tier == "read_only" || raw_tier == "guard" ||
      raw_tier == "full_access") {
    return std::string(raw_tier);
  }
  // Fail closed to the session default tier.
  return "guard";
}

bool IsFinalConfirmGatedConsequence(std::string_view consequence) {
  // Free classes: ordinary work and navigation escalations (reversible —
  // the user can navigate back). Everything else — submit,
  // purchase_or_transfer, account_security, credential_fill, file_transfer,
  // destructive, and any unknown/unrecognized value — fails closed to
  // gated so a missing or malformed classification can never auto-pass.
  return consequence != "ordinary" && consequence != "new_origin";
}

bool FsPathInsideRuntimeWhitelist(
    const std::string& path,
    const std::vector<std::string>& whitelist_roots) {
  if (path.empty() || path.front() != '/') {
    return false;
  }
  for (const std::string& root : whitelist_roots) {
    std::string_view trimmed(root);
    while (trimmed.size() > 1 && trimmed.back() == '/') {
      trimmed.remove_suffix(1);
    }
    // A root that normalizes to the filesystem root ("/", "///") would
    // whitelist everything absolute; mirror the Rust
    // fs_path_inside_whitelist and fail closed instead.
    if (trimmed == "/") {
      continue;
    }
    if (trimmed.empty() || trimmed.front() != '/') {
      continue;
    }
    if (path == trimmed ||
        (path.compare(0, trimmed.size(), trimmed) == 0 &&
         path.size() > trimmed.size() &&
         path[trimmed.size()] == '/')) {
      return true;
    }
  }
  return false;
}

CapabilityRequestContext::CapabilityRequestContext() = default;
CapabilityRequestContext::CapabilityRequestContext(const CapabilityRequestContext&) = default;
CapabilityRequestContext::CapabilityRequestContext(CapabilityRequestContext&&) noexcept = default;
CapabilityRequestContext& CapabilityRequestContext::operator=(const CapabilityRequestContext&) = default;
CapabilityRequestContext& CapabilityRequestContext::operator=(CapabilityRequestContext&&) noexcept = default;
CapabilityRequestContext::~CapabilityRequestContext() = default;

CapabilityObligations::CapabilityObligations() = default;
CapabilityObligations::CapabilityObligations(const CapabilityObligations&) = default;
CapabilityObligations::CapabilityObligations(CapabilityObligations&&) noexcept = default;
CapabilityObligations& CapabilityObligations::operator=(const CapabilityObligations&) = default;
CapabilityObligations& CapabilityObligations::operator=(CapabilityObligations&&) noexcept = default;
CapabilityObligations::~CapabilityObligations() = default;

ApprovalRequest::ApprovalRequest() = default;
ApprovalRequest::ApprovalRequest(const ApprovalRequest&) = default;
ApprovalRequest::ApprovalRequest(ApprovalRequest&&) noexcept = default;
ApprovalRequest& ApprovalRequest::operator=(const ApprovalRequest&) = default;
ApprovalRequest& ApprovalRequest::operator=(ApprovalRequest&&) noexcept = default;
ApprovalRequest::~ApprovalRequest() = default;

UserPresenceRequest::UserPresenceRequest() = default;
UserPresenceRequest::UserPresenceRequest(const UserPresenceRequest&) = default;
UserPresenceRequest::UserPresenceRequest(UserPresenceRequest&&) noexcept = default;
UserPresenceRequest& UserPresenceRequest::operator=(const UserPresenceRequest&) = default;
UserPresenceRequest& UserPresenceRequest::operator=(UserPresenceRequest&&) noexcept = default;
UserPresenceRequest::~UserPresenceRequest() = default;

CapabilityEvaluationResult::CapabilityEvaluationResult() = default;
CapabilityEvaluationResult::CapabilityEvaluationResult(const CapabilityEvaluationResult&) = default;
CapabilityEvaluationResult::CapabilityEvaluationResult(CapabilityEvaluationResult&&) noexcept = default;
CapabilityEvaluationResult& CapabilityEvaluationResult::operator=(const CapabilityEvaluationResult&) = default;
CapabilityEvaluationResult& CapabilityEvaluationResult::operator=(CapabilityEvaluationResult&&) noexcept = default;
CapabilityEvaluationResult::~CapabilityEvaluationResult() = default;

namespace {

CapabilityObligations BuildObligations(const CapabilityDescriptor& descriptor) {
  CapabilityObligations obligations;
  obligations.requires_url_redaction =
      (descriptor.redaction_policy == CapabilityRedactionPolicy::kUrlAndToken);
  obligations.requires_credential_redaction =
      (descriptor.redaction_policy == CapabilityRedactionPolicy::kCredential);
  obligations.requires_har_redaction =
      (descriptor.redaction_policy == CapabilityRedactionPolicy::kHar);
  obligations.requires_screenshot_redaction =
      (descriptor.redaction_policy == CapabilityRedactionPolicy::kScreenshot);

  obligations.requires_target_tab_revalidation =
      (descriptor.resource_boundary == CapabilityBoundary::kTab);
  obligations.requires_origin_revalidation =
      (descriptor.origin_policy != CapabilityOriginPolicy::kNotApplicable &&
       descriptor.origin_policy != CapabilityOriginPolicy::kUnknown);
  obligations.requires_lease_heartbeat = RequiresLease(descriptor);

  obligations.requires_audit_receipt =
      (descriptor.audit_class != CapabilityAuditClass::kNone);
  obligations.audit_class = descriptor.audit_class;
  obligations.audit_category =
      std::string(CapabilityCategoryName(descriptor.category));

  obligations.presence_freshness = descriptor.presence_freshness;
  obligations.required_device_protection = descriptor.minimum_device_protection;
  obligations.transaction_binding = descriptor.transaction_binding;
  return obligations;
}

CapabilityDescriptor MakeBrowserActionDescriptor(
    std::string_view canonical_id,
    std::string_view tool_name,
    CapabilityCategory category,
    CapabilityBrokerType broker,
    CapabilityBoundary boundary,
    CapabilityFeatureGate feature_gate,
    CapabilitySurfaceMask surfaces,
    std::string_view description) {
  const BrowserActionContract* action_contract =
      FindBrowserActionContract(tool_name);
  CHECK(action_contract);

  CapabilityDescriptor desc;
  desc.canonical_id = canonical_id;
  desc.tool_name = tool_name;
  desc.schema_version = 1;
  desc.result_version = 1;
  desc.category = category;
  desc.required_broker = broker;
  desc.resource_boundary = boundary;
  desc.feature_gate = feature_gate;
  desc.allowed_surfaces = surfaces;
  desc.description = description;
  desc.browser_action_contract = action_contract;

  desc.mutability = (action_contract->kind == BrowserActionKind::kAction)
                        ? CapabilityMutability::kMutable
                        : CapabilityMutability::kReadOnly;
  desc.changes_authority = action_contract->changes_authority;
  desc.sensitivity =
      (action_contract->sensitivity == BrowserActionSensitivity::kSensitive)
          ? CapabilitySensitivity::kSensitive
          : CapabilitySensitivity::kLow;
  desc.approval_requirement =
      (action_contract->approval == BrowserActionApproval::kRequired)
          ? CapabilityApprovalRequirement::kRequired
          : CapabilityApprovalRequirement::kNotRequired;
  desc.lease_requirement =
      (action_contract->lease == BrowserActionLease::kRequired)
          ? CapabilityLeaseRequirement::kRequired
          : CapabilityLeaseRequirement::kNotRequired;

  switch (action_contract->domain_policy) {
    case BrowserActionDomainPolicy::kActiveTabOrigin:
      desc.origin_policy = CapabilityOriginPolicy::kActiveTabOrigin;
      break;
    case BrowserActionDomainPolicy::kActiveTabOriginOrApprovedDestination:
      desc.origin_policy =
          CapabilityOriginPolicy::kActiveTabOriginOrApprovedDestination;
      break;
  }

  desc.redaction_policy = CapabilityRedactionPolicy::kUrlAndToken;
  desc.missing_policy = CapabilityMissingPolicy::kFailClosed;

  if (desc.changes_authority) {
    desc.audit_class = CapabilityAuditClass::kSecuritySensitive;
    desc.transaction_binding = true;
  } else if (desc.mutability == CapabilityMutability::kMutable) {
    desc.audit_class = CapabilityAuditClass::kMutation;
  } else {
    desc.audit_class = CapabilityAuditClass::kRead;
  }

  return desc;
}

CapabilityDescriptor MakeCapabilityDescriptor(
    std::string_view canonical_id,
    std::string_view tool_name,
    CapabilityCategory category,
    CapabilityMutability mutability,
    bool changes_authority,
    CapabilityBrokerType broker,
    CapabilityBoundary boundary,
    CapabilitySensitivity sensitivity,
    CapabilityFeatureGate feature_gate,
    CapabilitySurfaceMask surfaces,
    CapabilityMissingPolicy missing_policy,
    std::string_view description) {
  CapabilityDescriptor d;
  d.canonical_id = canonical_id;
  d.tool_name = tool_name;
  d.schema_version = 1;
  d.result_version = 1;
  d.category = category;
  d.mutability = mutability;
  d.changes_authority = changes_authority;
  d.required_broker = broker;
  d.resource_boundary = boundary;
  d.sensitivity = sensitivity;
  d.feature_gate = feature_gate;
  d.allowed_surfaces = surfaces;
  d.missing_policy = missing_policy;
  d.description = description;

  // Browser manipulation is full access: tabs, pages, input, navigation,
  // history, bookmarks and leases never carry a user-approval gate. Only a
  // capability that leaves the browser boundary asks the user, and only when
  // it mutates state or handles credential-class data.
  const bool mutating_or_credential =
      d.mutability == CapabilityMutability::kMutable ||
      d.sensitivity == CapabilitySensitivity::kCredential;
  d.approval_requirement =
      (mutating_or_credential && CapabilityLeavesBrowserBoundary(canonical_id))
          ? CapabilityApprovalRequirement::kRequired
          : CapabilityApprovalRequirement::kNotRequired;

  if (tool_name.rfind("lease.", 0) == 0 ||
      tool_name.rfind("browser_heartbeat_lease", 0) == 0 ||
      tool_name == "browser_acquire_lease" ||
      tool_name == "browser_release_lease" ||
      tool_name == "browser_same_origin_fetch" ||
      tool_name == "browser_visual_click") {
    d.lease_requirement = CapabilityLeaseRequirement::kRequired;
  } else {
    d.lease_requirement = CapabilityLeaseRequirement::kNotRequired;
  }

  d.browser_action_contract = FindBrowserActionContract(tool_name);

  if (d.resource_boundary == CapabilityBoundary::kTab) {
    d.origin_policy = CapabilityOriginPolicy::kActiveTabOrigin;
  } else if (tool_name == "browser_same_origin_fetch") {
    d.origin_policy =
        CapabilityOriginPolicy::kActiveTabOriginOrApprovedDestination;
  } else if (tool_name.rfind("browser_grant_exact", 0) == 0 ||
             tool_name.rfind("browser_revoke_exact", 0) == 0 ||
             tool_name.rfind("browser_list_exact", 0) == 0) {
    d.origin_policy = CapabilityOriginPolicy::kExactGrantedOrigin;
  } else {
    d.origin_policy = CapabilityOriginPolicy::kNotApplicable;
  }

  if (d.category == CapabilityCategory::kPage) {
    d.redaction_policy = CapabilityRedactionPolicy::kUrlAndToken;
  } else if (tool_name.rfind("browser_screenshot", 0) == 0) {
    d.redaction_policy = CapabilityRedactionPolicy::kScreenshot;
  } else if (tool_name.rfind("browser_network", 0) == 0) {
    d.redaction_policy = CapabilityRedactionPolicy::kHar;
  } else if (d.category == CapabilityCategory::kMail ||
             d.category == CapabilityCategory::kVault) {
    d.redaction_policy = CapabilityRedactionPolicy::kCredential;
  } else {
    d.redaction_policy = CapabilityRedactionPolicy::kNone;
  }

  if (d.category == CapabilityCategory::kVault) {
    if (tool_name == "vault_list_credentials_for_active_page") {
      d.audit_class = CapabilityAuditClass::kRead;
      d.user_presence_requirement = UserPresenceRequirement::kLocalPresence;
      d.presence_freshness = base::Seconds(60);
      d.minimum_device_protection = MinimumDeviceProtection::kOSKeystore;
    } else {
      d.audit_class = CapabilityAuditClass::kHighRisk;
      d.user_presence_requirement =
          UserPresenceRequirement::kBiometricStrong;
      d.presence_freshness = base::Seconds(30);
      d.minimum_device_protection = MinimumDeviceProtection::kHardwareKey;
      d.transaction_binding = true;
    }
  } else if (d.category == CapabilityCategory::kMail) {
    if (tool_name == "mail_extract_otp") {
      d.audit_class = CapabilityAuditClass::kSecuritySensitive;
      d.user_presence_requirement = UserPresenceRequirement::kLocalPresence;
      d.presence_freshness = base::Seconds(60);
      d.minimum_device_protection = MinimumDeviceProtection::kOSKeystore;
    } else if (d.changes_authority ||
               d.sensitivity == CapabilitySensitivity::kCredential) {
      d.audit_class = CapabilityAuditClass::kHighRisk;
      d.user_presence_requirement =
          UserPresenceRequirement::kBiometricStrong;
      d.presence_freshness = base::Seconds(30);
      d.minimum_device_protection = MinimumDeviceProtection::kOSKeystore;
      d.transaction_binding = true;
    } else if (d.mutability == CapabilityMutability::kMutable) {
      d.audit_class = CapabilityAuditClass::kMutation;
      d.transaction_binding = true;
    } else {
      d.audit_class = CapabilityAuditClass::kRead;
    }
  } else if (d.changes_authority) {
    d.audit_class = CapabilityAuditClass::kSecuritySensitive;
    d.transaction_binding = true;
  } else if (d.mutability == CapabilityMutability::kMutable) {
    d.audit_class = CapabilityAuditClass::kMutation;
  } else {
    d.audit_class = CapabilityAuditClass::kRead;
  }

  return d;
}

std::vector<CapabilityDescriptor> BuildCanonicalDescriptors() {
  std::vector<CapabilityDescriptor> descriptors;

  // Surface constants in catalog context
  constexpr CapabilitySurfaceMask DesktopAgent =
      CapabilitySurface::kDesktopAgent;
  constexpr CapabilitySurfaceMask BrowserMcp = CapabilitySurface::kBrowserMcp;
  constexpr CapabilitySurfaceMask ControlPlane =
      CapabilitySurface::kControlPlane;
  constexpr CapabilitySurfaceMask DesktopAgentAndBrowserMcp =
      CapabilitySurface::kDesktopAgent | CapabilitySurface::kBrowserMcp;

  constexpr CapabilityCategory BrokerCategory_Tabs = CapabilityCategory::kTabs;
  constexpr CapabilityCategory BrokerCategory_Navigation =
      CapabilityCategory::kNavigation;
  constexpr CapabilityCategory BrokerCategory_Page = CapabilityCategory::kPage;
  constexpr CapabilityCategory BrokerCategory_Input =
      CapabilityCategory::kInput;
  constexpr CapabilityCategory BrokerCategory_Action =
      CapabilityCategory::kInput;
  constexpr CapabilityCategory BrokerCategory_History =
      CapabilityCategory::kHistory;
  constexpr CapabilityCategory BrokerCategory_Bookmarks =
      CapabilityCategory::kBookmarks;
  constexpr CapabilityCategory BrokerCategory_Policy =
      CapabilityCategory::kPolicy;
  constexpr CapabilityCategory BrokerCategory_Routines =
      CapabilityCategory::kRoutines;
  constexpr CapabilityCategory BrokerCategory_Mail = CapabilityCategory::kMail;
  constexpr CapabilityCategory BrokerCategory_Lease =
      CapabilityCategory::kLease;
  constexpr CapabilityCategory BrokerCategory_Vault =
      CapabilityCategory::kVault;
  constexpr CapabilityCategory BrokerCategory_Capture =
      CapabilityCategory::kCapture;

  [[maybe_unused]] constexpr CapabilityFeatureGate BrokerFeatureGate_Unknown =
      CapabilityFeatureGate::kUnknown;
  constexpr CapabilityFeatureGate BrokerFeatureGate_Always =
      CapabilityFeatureGate::kAlways;
  constexpr CapabilityFeatureGate BrokerFeatureGate_MailBeta =
      CapabilityFeatureGate::kMailBeta;
  constexpr CapabilityFeatureGate BrokerFeatureGate_Routines =
      CapabilityFeatureGate::kRoutines;
  constexpr CapabilityFeatureGate BrokerFeatureGate_Vault =
      CapabilityFeatureGate::kVault;
  constexpr CapabilityFeatureGate BrokerFeatureGate_NativeInput =
      kNativeInputBrokerGate;

#define MAHO_BROWSER_CAPABILITY(id, canonical_id, tool_name, category,         \
                                mutability, authority_change, broker,          \
                                boundary, sensitivity, feature_gate, surfaces, \
                                missing_policy, description)                   \
  descriptors.push_back(MakeCapabilityDescriptor(                              \
      canonical_id, tool_name, BrokerCategory_##category,                      \
      CapabilityMutability::k##mutability,                                     \
      (std::string_view(#authority_change) == "Yes"),                          \
      CapabilityBrokerType::k##broker, CapabilityBoundary::k##boundary,        \
      CapabilitySensitivity::k##sensitivity,                                   \
      BrokerFeatureGate_##feature_gate, surfaces,                              \
      CapabilityMissingPolicy::k##missing_policy, description));

#define MAHO_BROWSER_ACTION_CAPABILITY(id, canonical_id, tool_name, category, \
                                       broker, boundary, feature_gate,        \
                                       surfaces, description)                 \
  descriptors.push_back(MakeBrowserActionDescriptor(                          \
      canonical_id, tool_name, BrokerCategory_##category,                     \
      CapabilityBrokerType::k##broker, CapabilityBoundary::k##boundary,       \
      BrokerFeatureGate_##feature_gate, surfaces, description));

#include "maho/browser/ai/maho_browser_capability_catalog.def"

#undef MAHO_BROWSER_ACTION_CAPABILITY
#undef MAHO_BROWSER_CAPABILITY

  // Expand DesktopAgent and BrowserMcp to CLI mirrors for execution consistency
  for (CapabilityDescriptor& descriptor : descriptors) {
    if (descriptor.allowed_surfaces & CapabilitySurface::kDesktopAgent) {
      descriptor.allowed_surfaces |= CapabilitySurface::kCliAgent;
    }
    if (descriptor.allowed_surfaces & CapabilitySurface::kBrowserMcp) {
      descriptor.allowed_surfaces |= CapabilitySurface::kCliGeneric;
    }
  }

  return descriptors;
}

const std::vector<CapabilityDescriptor>& GetDescriptorsStorage() {
  static const base::NoDestructor<std::vector<CapabilityDescriptor>>
      kDescriptors(BuildCanonicalDescriptors());
  return *kDescriptors;
}

bool IsFeatureActive(CapabilityFeatureGate gate,
                     const FeatureGateState& features) {
  switch (gate) {
    case CapabilityFeatureGate::kUnknown:
      return false;
    case CapabilityFeatureGate::kAlways:
      return true;
    case CapabilityFeatureGate::kMailBeta:
      return features.mail_enabled;
    case CapabilityFeatureGate::kRoutines:
      return features.routines_enabled;
    case CapabilityFeatureGate::kVault:
      return features.vault_enabled;
    case kNativeInputBrokerGate:
      return false;
  }
  return false;
}

}  // namespace

CapabilityEvaluationResult CapabilityEvaluationResult::Permit(
    CapabilityObligations obligations) {
  CapabilityEvaluationResult result;
  result.decision = CapabilityDecisionKind::kPermit;
  result.deny_reason = CapabilityDenyReason::kNone;
  result.obligations = std::move(obligations);
  return result;
}

CapabilityEvaluationResult CapabilityEvaluationResult::Deny(
    CapabilityDenyReason reason,
    std::string_view message) {
  CapabilityEvaluationResult result;
  result.decision = CapabilityDecisionKind::kDeny;
  result.deny_reason = reason;
  result.message = std::string(message);
  return result;
}

CapabilityEvaluationResult CapabilityEvaluationResult::RequiresApproval(
    ApprovalRequest request,
    CapabilityObligations obligations) {
  CapabilityEvaluationResult result;
  result.decision = CapabilityDecisionKind::kRequiresApproval;
  result.deny_reason = CapabilityDenyReason::kNone;
  result.approval_request = std::move(request);
  result.obligations = std::move(obligations);
  return result;
}

CapabilityEvaluationResult CapabilityEvaluationResult::RequiresUserPresence(
    UserPresenceRequest request) {
  CapabilityEvaluationResult result;
  result.decision = CapabilityDecisionKind::kRequiresUserPresence;
  result.deny_reason = CapabilityDenyReason::kNone;
  result.presence_request = std::move(request);
  return result;
}

MahoCapabilityBroker::MahoCapabilityBroker() = default;
MahoCapabilityBroker::~MahoCapabilityBroker() = default;

// static
const CapabilityDescriptor* MahoCapabilityBroker::FindCapability(
    std::string_view identifier) {
  const auto& descriptors = GetDescriptorsStorage();
  for (const CapabilityDescriptor& descriptor : descriptors) {
    if (descriptor.canonical_id == identifier ||
        descriptor.tool_name == identifier) {
      return &descriptor;
    }
  }
  return nullptr;
}

// static
const CapabilityDescriptor* MahoCapabilityBroker::FindCapabilityById(
    std::string_view canonical_id) {
  const auto& descriptors = GetDescriptorsStorage();
  for (const CapabilityDescriptor& descriptor : descriptors) {
    if (descriptor.canonical_id == canonical_id) {
      return &descriptor;
    }
  }
  return nullptr;
}

// static
const CapabilityDescriptor* MahoCapabilityBroker::FindCapabilityByToolName(
    std::string_view tool_name) {
  const auto& descriptors = GetDescriptorsStorage();
  for (const CapabilityDescriptor& descriptor : descriptors) {
    if (descriptor.tool_name == tool_name) {
      return &descriptor;
    }
  }
  return nullptr;
}

// static
base::span<const CapabilityDescriptor>
MahoCapabilityBroker::GetAllCapabilities() {
  const auto& descriptors = GetDescriptorsStorage();
  return base::span<const CapabilityDescriptor>(descriptors);
}

// static
bool MahoCapabilityBroker::ValidateDescriptorInvariants(
    const CapabilityDescriptor& descriptor) {
  if (descriptor.canonical_id.empty() || descriptor.tool_name.empty() ||
      descriptor.description.empty()) {
    return false;
  }
  if (descriptor.schema_version <= 0 || descriptor.result_version <= 0 ||
      descriptor.request_schema_version <= 0 ||
      descriptor.response_schema_version <= 0) {
    return false;
  }
  if (descriptor.category == CapabilityCategory::kUnknown ||
      descriptor.mutability == CapabilityMutability::kUnknown ||
      descriptor.required_broker == CapabilityBrokerType::kUnknown ||
      descriptor.resource_boundary == CapabilityBoundary::kUnknown ||
      descriptor.sensitivity == CapabilitySensitivity::kUnknown ||
      descriptor.approval_requirement ==
          CapabilityApprovalRequirement::kUnknown ||
      descriptor.lease_requirement == CapabilityLeaseRequirement::kUnknown ||
      descriptor.origin_policy == CapabilityOriginPolicy::kUnknown ||
      descriptor.feature_gate == CapabilityFeatureGate::kUnknown ||
      descriptor.missing_policy != CapabilityMissingPolicy::kFailClosed) {
    return false;
  }
  if (descriptor.allowed_surfaces == CapabilitySurface::kSurfaceNone) {
    return false;
  }

  // Invariant 1: Lease-required actions must be sensitive
  if (RequiresLease(descriptor) && !IsSensitive(descriptor)) {
    return false;
  }

  // Invariant 2: Authority-changing actions require explicit destination policy
  if (descriptor.changes_authority &&
      descriptor.origin_policy == CapabilityOriginPolicy::kNotApplicable &&
      descriptor.category != CapabilityCategory::kMail &&
      descriptor.category != CapabilityCategory::kVault &&
      descriptor.category != CapabilityCategory::kPolicy &&
      descriptor.category != CapabilityCategory::kLease &&
      descriptor.category != CapabilityCategory::kRoutines) {
    return false;
  }

  // Invariant 3: Biometric strong requirement requires device protection and freshness
  if (descriptor.user_presence_requirement ==
      UserPresenceRequirement::kBiometricStrong) {
    if (descriptor.presence_freshness.is_zero() ||
        descriptor.minimum_device_protection ==
            MinimumDeviceProtection::kNone) {
      return false;
    }
  }

  // Invariant 4: BrowserActionContract parity if attached
  if (descriptor.browser_action_contract) {
    const auto& contract = *descriptor.browser_action_contract;
    if (descriptor.changes_authority != contract.changes_authority) {
      return false;
    }
    bool is_action = (contract.kind == BrowserActionKind::kAction);
    bool is_mutable =
        (descriptor.mutability == CapabilityMutability::kMutable);
    if (is_action != is_mutable) {
      return false;
    }
    bool contract_sensitive =
        (contract.sensitivity == BrowserActionSensitivity::kSensitive);
    bool desc_sensitive =
        (descriptor.sensitivity == CapabilitySensitivity::kSensitive);
    if (contract_sensitive != desc_sensitive) {
      return false;
    }
  }

  return true;
}

// static
bool MahoCapabilityBroker::ValidateAllDescriptors() {
  const auto& descriptors = GetDescriptorsStorage();
  for (const CapabilityDescriptor& desc : descriptors) {
    if (!ValidateDescriptorInvariants(desc)) {
      LOG(ERROR) << "Capability descriptor failed invariant validation: "
                 << desc.canonical_id;
      return false;
    }
  }
  return true;
}

CapabilityEvaluationResult MahoCapabilityBroker::Evaluate(
    const CapabilityRequestContext& context,
    const FeatureGateState& features) const {
  // 1. Context validation
  if (context.capability_id.empty()) {
    return CapabilityEvaluationResult::Deny(
        CapabilityDenyReason::kInvalidRequestContext,
        "Capability identifier cannot be empty");
  }
  if (context.principal.kind == PrincipalKind::kUnknown) {
    return CapabilityEvaluationResult::Deny(
        CapabilityDenyReason::kInsufficientPrincipalAuth,
        "Principal identity is unknown or unauthenticated");
  }

  // 2. Capability lookup (Fail-closed on unknown IDs)
  const CapabilityDescriptor* descriptor =
      FindCapability(context.capability_id);
  if (!descriptor) {
    return CapabilityEvaluationResult::Deny(
        CapabilityDenyReason::kUnknownCapability,
        "Unknown capability identifier");
  }

  // 3. Feature gate check
  if (!IsFeatureActive(descriptor->feature_gate, features)) {
    return CapabilityEvaluationResult::Deny(
        CapabilityDenyReason::kFeatureDisabled,
        "Capability is disabled by feature gate");
  }

  // 4. Allowed surface check
  if ((descriptor->allowed_surfaces & context.surface) == 0) {
    return CapabilityEvaluationResult::Deny(
        CapabilityDenyReason::kSurfaceNotAllowed,
        "Capability is not allowed on requested surface");
  }

  // 5. Incognito / Guest isolation check
  if (context.is_incognito || context.is_guest) {
    if (descriptor->category == CapabilityCategory::kHistory ||
        descriptor->category == CapabilityCategory::kBookmarks ||
        descriptor->category == CapabilityCategory::kMail ||
        descriptor->category == CapabilityCategory::kVault ||
        descriptor->category == CapabilityCategory::kRoutines ||
        descriptor->category == CapabilityCategory::kPolicy ||
        descriptor->category == CapabilityCategory::kLease) {
      return CapabilityEvaluationResult::Deny(
          CapabilityDenyReason::kIncognitoOrGuestDisallowed,
          "Capability category is disallowed in incognito or guest mode");
    }
  }

  // 6. Principal authorization & device security check
  if (descriptor->sensitivity == CapabilitySensitivity::kCredential &&
      !context.principal.IsInternalTrusted() &&
      context.principal.auth_strength <
          AuthenticationStrength::kHardwareSigned) {
    if (context.surface != CapabilitySurface::kControlPlane) {
      return CapabilityEvaluationResult::Deny(
          CapabilityDenyReason::kInsufficientPrincipalAuth,
          "Credential operations require trusted internal or hardware-authenticated principal");
    }
  }

  if (descriptor->minimum_device_protection !=
      MinimumDeviceProtection::kNone) {
    if (!context.principal.MeetsMinimumProtection(
            descriptor->minimum_device_protection)) {
      return CapabilityEvaluationResult::Deny(
          CapabilityDenyReason::kInsufficientDeviceProtection,
          "Principal device does not satisfy minimum hardware protection requirement");
    }
  }

  // 7. User presence obligation
  if (descriptor->user_presence_requirement !=
      UserPresenceRequirement::kNone) {
    if (context.user_presence_token.empty()) {
      UserPresenceRequest presence_req;
      presence_req.capability_id = std::string(descriptor->canonical_id);
      presence_req.requirement = descriptor->user_presence_requirement;
      presence_req.freshness_window = descriptor->presence_freshness;
      presence_req.request_digest = context.request_digest;
      presence_req.minimum_device_protection =
          descriptor->minimum_device_protection;
      return CapabilityEvaluationResult::RequiresUserPresence(
          std::move(presence_req));
    }
    if (context.user_presence_token == "invalid" ||
        context.user_presence_token == "stale") {
      return CapabilityEvaluationResult::Deny(
          CapabilityDenyReason::kStaleUserPresence,
          "User presence verification token is stale or invalid");
    }
  }

  // 8. Resource boundary & adopted tab check
  if (descriptor->resource_boundary == CapabilityBoundary::kTab) {
    int tab_id = (context.target_tab_id >= 0) ? context.target_tab_id
                                              : context.active_tab_id;
    if (tab_id < 0) {
      return CapabilityEvaluationResult::Deny(
          CapabilityDenyReason::kInvalidRequestContext,
          "Valid target or active tab ID is required for tab-scoped capability");
    }
    if ((context.principal.kind == PrincipalKind::kLocalMcpPeer ||
         context.principal.kind == PrincipalKind::kCliGeneric) &&
        !context.adopted_tabs.empty()) {
      if (std::find(context.adopted_tabs.begin(), context.adopted_tabs.end(),
                    tab_id) == context.adopted_tabs.end()) {
        return CapabilityEvaluationResult::Deny(
            CapabilityDenyReason::kTabNotAdopted,
            "Target tab has not been adopted by this controller");
      }
    }
  }

  // 9. Blocked domains & origin policy check
  if (!context.blocked_domains.empty()) {
    for (const std::string& blocked : context.blocked_domains) {
      if (!context.source_origin.empty() &&
          context.source_origin.find(blocked) != std::string::npos) {
        return CapabilityEvaluationResult::Deny(
            CapabilityDenyReason::kDomainBlocked,
            "Source origin is blocked by policy");
      }
      if (!context.destination_origin.empty() &&
          context.destination_origin.find(blocked) != std::string::npos) {
        return CapabilityEvaluationResult::Deny(
            CapabilityDenyReason::kDomainBlocked,
            "Destination origin is blocked by policy");
      }
    }
  }

  if (descriptor->origin_policy ==
      CapabilityOriginPolicy::kActiveTabOrigin) {
    if (!context.destination_origin.empty() &&
        !context.source_origin.empty() &&
        context.destination_origin != context.source_origin) {
      return CapabilityEvaluationResult::Deny(
          CapabilityDenyReason::kOriginNotAllowed,
          "Operation destination origin differs from active tab origin");
    }
  } else if (descriptor->origin_policy ==
             CapabilityOriginPolicy::kExactGrantedOrigin) {
    if (!context.destination_origin.empty()) {
      if (context.granted_origins.empty() ||
          std::find(context.granted_origins.begin(),
                    context.granted_origins.end(),
                    context.destination_origin) ==
              context.granted_origins.end()) {
        return CapabilityEvaluationResult::Deny(
            CapabilityDenyReason::kOriginNotAllowed,
            "Destination origin is not in controller granted origins allowlist");
      }
    }
  }

  // 10. Lease requirement check
  if (RequiresLease(*descriptor)) {
    if (context.lease_token.empty()) {
      return CapabilityEvaluationResult::Deny(
          CapabilityDenyReason::kMissingLease,
          "Exclusive tab lease is required for this operation");
    }
    if (context.lease_token == "invalid" || context.lease_token == "expired") {
      return CapabilityEvaluationResult::Deny(
          CapabilityDenyReason::kInvalidLeaseToken,
          "Tab lease token is invalid or expired");
    }
  }

  // 10.5 Runtime permission-tier gate (plan row 3). Applies only to
  // file-scoped requests (non-empty fs_path). The structural checks above
  // always run first: a tier can add a denial or an approval ask, but can
  // never remove a sandbox, origin, lease, or presence obligation.
  bool tier_forces_approval = false;
  if (!context.fs_path.empty()) {
    const std::string tier = ParseRuntimeConfigTier(context.permission_tier);
    const bool inside_whitelist = FsPathInsideRuntimeWhitelist(
        context.fs_path, context.fs_whitelist_roots);
    if (tier == "read_only") {
      if (descriptor->mutability == CapabilityMutability::kMutable) {
        return CapabilityEvaluationResult::Deny(
            CapabilityDenyReason::kPermissionTierDeniedWrite,
            "Permission tier read_only denies local file writes");
      }
      if (!inside_whitelist) {
        return CapabilityEvaluationResult::Deny(
            CapabilityDenyReason::kPermissionTierDeniedRead,
            "Permission tier read_only denies reads outside the session file whitelist");
      }
    } else if (tier == "guard" && !inside_whitelist) {
      // Free inside whitelist roots; outside, ask-gate through the existing
      // approval-token handshake below.
      tier_forces_approval = true;
    }
    // full_access and in-whitelist requests fall through unchanged.
  }

  // 10.6 Final-confirmation gate (plan row 5). Driving the browser is full
  // access, so this gate never reaches a capability that stays inside the
  // browser: clicking, typing, scrolling and navigating are allowed outright
  // however the consequence was classified, and final_confirm is scoped to
  // capabilities that leave the browser boundary (another broker, the consent
  // plane, the filesystem, OS-level input). Within that scope, requests
  // resolving to an externally visible or irreversible consequence class route
  // through the approval-token ask-gate below. A boundary action tool gates
  // even without a declared classification: a missing validated context is
  // Unknown and fails closed to gated. Other boundary descriptors gate only
  // when a consequence was declared (empty stays inert, mirroring fs_path).
  // Mail writes additionally gate through the typed mail-authorization path
  // (maho_mail_tool_authorization.h), which requires per-call approval under
  // every policy. Like the tier gate, this can only add an ask, never remove a
  // sandbox, origin, lease, or presence obligation.
  bool final_confirm_forces_approval = false;
  if (context.final_confirm &&
      CapabilityLeavesBrowserBoundary(descriptor->canonical_id)) {
    const bool is_action_tool =
        descriptor->browser_action_contract &&
        descriptor->browser_action_contract->kind ==
            BrowserActionKind::kAction;
    if (is_action_tool || !context.action_consequence.empty()) {
      final_confirm_forces_approval = IsFinalConfirmGatedConsequence(
          context.action_consequence.empty() ? "unknown"
                                             : context.action_consequence);
    }
  }

  // 11. Approval requirement check
  if (RequiresApproval(*descriptor) || tier_forces_approval ||
      final_confirm_forces_approval) {
    if (context.approval_token.empty()) {
      ApprovalRequest approval_req;
      approval_req.capability_id = std::string(descriptor->canonical_id);
      approval_req.tool_name = std::string(descriptor->tool_name);
      approval_req.sensitivity = descriptor->sensitivity;
      approval_req.requested_destination_origin = context.destination_origin;
      approval_req.target_tab_id = (context.target_tab_id >= 0)
                                       ? context.target_tab_id
                                       : context.active_tab_id;
      approval_req.description = std::string(descriptor->description);
      return CapabilityEvaluationResult::RequiresApproval(
          std::move(approval_req), BuildObligations(*descriptor));
    }
    if (context.approval_token == "invalid" ||
        context.approval_token == "rejected" ||
        context.approval_token == "denied") {
      return CapabilityEvaluationResult::Deny(
          CapabilityDenyReason::kInvalidApprovalToken,
          "Approval token is invalid or was rejected by user");
    }
  }

  // 12. Success - permit with full obligations
  return CapabilityEvaluationResult::Permit(BuildObligations(*descriptor));
}

CapabilityEvaluationResult MahoCapabilityBroker::RevalidateAfterPresence(
    const CapabilityRequestContext& original_context,
    std::string_view validated_presence_token,
    std::string_view bound_digest,
    base::Time presence_timestamp,
    base::Time current_time,
    const FeatureGateState& features) const {
  const CapabilityDescriptor* descriptor =
      FindCapability(original_context.capability_id);
  if (!descriptor) {
    return CapabilityEvaluationResult::Deny(
        CapabilityDenyReason::kUnknownCapability,
        "Unknown capability identifier during presence revalidation");
  }

  // 1. Transaction binding revalidation
  if (descriptor->transaction_binding) {
    if (bound_digest.empty() || original_context.request_digest.empty() ||
        bound_digest != original_context.request_digest) {
      return CapabilityEvaluationResult::Deny(
          CapabilityDenyReason::kTransactionBindingMismatch,
          "Bound presence digest does not match original request digest");
    }
  }

  // 2. Freshness check
  if (!descriptor->presence_freshness.is_zero()) {
    if (current_time < presence_timestamp ||
        (current_time - presence_timestamp) > descriptor->presence_freshness) {
      return CapabilityEvaluationResult::Deny(
          CapabilityDenyReason::kStaleUserPresence,
          "User presence verification has expired");
    }
  }

  // 3. Token check
  if (validated_presence_token.empty() ||
      validated_presence_token == "invalid") {
    return CapabilityEvaluationResult::Deny(
        CapabilityDenyReason::kMissingUserPresence,
        "User presence token is missing or invalid");
  }

  CapabilityRequestContext context_with_presence = original_context;
  context_with_presence.user_presence_token =
      std::string(validated_presence_token);
  return Evaluate(context_with_presence, features);
}

std::vector<const CapabilityDescriptor*>
MahoCapabilityBroker::GetCapabilitiesForSurface(
    CapabilitySurface surface,
    const FeatureGateState& features) const {
  std::vector<const CapabilityDescriptor*> filtered;
  const auto& descriptors = GetDescriptorsStorage();
  for (const CapabilityDescriptor& desc : descriptors) {
    if ((desc.allowed_surfaces & surface) != 0 &&
        IsFeatureActive(desc.feature_gate, features)) {
      filtered.push_back(&desc);
    }
  }
  return filtered;
}

}  // namespace maho::ai
