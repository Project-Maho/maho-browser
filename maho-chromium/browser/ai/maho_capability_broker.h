// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_CAPABILITY_BROKER_H_
#define MAHO_BROWSER_AI_MAHO_CAPABILITY_BROKER_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/time/time.h"
#include "maho/browser/ai/maho_browser_action_contract.h"
#include "maho/browser/ai/maho_capability_principal.h"
#include "maho/browser/ai/maho_capability_types.h"

namespace maho::ai {

struct CapabilityRequestContext {
  CapabilityRequestContext();
  CapabilityRequestContext(const CapabilityRequestContext&);
  CapabilityRequestContext(CapabilityRequestContext&&) noexcept;
  CapabilityRequestContext& operator=(const CapabilityRequestContext&);
  CapabilityRequestContext& operator=(CapabilityRequestContext&&) noexcept;
  ~CapabilityRequestContext();

  CapabilityPrincipal principal;
  CapabilitySurface surface = CapabilitySurface::kDesktopAgent;
  std::string session_id;
  std::string capability_id;  // canonical_id or tool_name
  int active_tab_id = -1;
  int target_tab_id = -1;
  std::string source_origin;
  std::string destination_origin;
  std::string approval_token;
  std::string lease_token;
  std::string request_digest;
  std::string user_presence_token;
  std::string device_proof;
  bool is_incognito = false;
  bool is_guest = false;
  std::vector<std::string> granted_origins;
  std::vector<std::string> blocked_domains;
  std::vector<int> adopted_tabs;
};

struct CapabilityObligations {
  CapabilityObligations();
  CapabilityObligations(const CapabilityObligations&);
  CapabilityObligations(CapabilityObligations&&) noexcept;
  CapabilityObligations& operator=(const CapabilityObligations&);
  CapabilityObligations& operator=(CapabilityObligations&&) noexcept;
  ~CapabilityObligations();

  bool requires_url_redaction = false;
  bool requires_credential_redaction = false;
  bool requires_har_redaction = false;
  bool requires_screenshot_redaction = false;
  bool requires_target_tab_revalidation = false;
  bool requires_origin_revalidation = false;
  bool requires_lease_heartbeat = false;
  bool requires_audit_receipt = false;
  CapabilityAuditClass audit_class = CapabilityAuditClass::kNone;
  std::string audit_category;
  base::TimeDelta presence_freshness;
  MinimumDeviceProtection required_device_protection =
      MinimumDeviceProtection::kNone;
  bool transaction_binding = false;
};

enum class CapabilityDecisionKind {
  kPermit,
  kDeny,
  kRequiresApproval,
  kRequiresUserPresence,
};

enum class CapabilityDenyReason {
  kNone,
  kUnknownCapability,
  kSurfaceNotAllowed,
  kFeatureDisabled,
  kIncognitoOrGuestDisallowed,
  kMissingApproval,
  kInvalidApprovalToken,
  kMissingLease,
  kInvalidLeaseToken,
  kOriginNotAllowed,
  kDestinationOriginRequired,
  kDomainBlocked,
  kTabNotAdopted,
  kInsufficientPrincipalAuth,
  kMissingUserPresence,
  kStaleUserPresence,
  kTransactionBindingMismatch,
  kInsufficientDeviceProtection,
  kInvalidRequestContext,
};

struct ApprovalRequest {
  ApprovalRequest();
  ApprovalRequest(const ApprovalRequest&);
  ApprovalRequest(ApprovalRequest&&) noexcept;
  ApprovalRequest& operator=(const ApprovalRequest&);
  ApprovalRequest& operator=(ApprovalRequest&&) noexcept;
  ~ApprovalRequest();

  std::string capability_id;
  std::string tool_name;
  CapabilitySensitivity sensitivity = CapabilitySensitivity::kUnknown;
  std::string requested_destination_origin;
  int target_tab_id = -1;
  std::string description;
};

struct UserPresenceRequest {
  UserPresenceRequest();
  UserPresenceRequest(const UserPresenceRequest&);
  UserPresenceRequest(UserPresenceRequest&&) noexcept;
  UserPresenceRequest& operator=(const UserPresenceRequest&);
  UserPresenceRequest& operator=(UserPresenceRequest&&) noexcept;
  ~UserPresenceRequest();

  std::string capability_id;
  UserPresenceRequirement requirement = UserPresenceRequirement::kNone;
  base::TimeDelta freshness_window;
  std::string request_digest;
  MinimumDeviceProtection minimum_device_protection =
      MinimumDeviceProtection::kNone;
};

struct CapabilityEvaluationResult {
  CapabilityEvaluationResult();
  CapabilityEvaluationResult(const CapabilityEvaluationResult&);
  CapabilityEvaluationResult(CapabilityEvaluationResult&&) noexcept;
  CapabilityEvaluationResult& operator=(const CapabilityEvaluationResult&);
  CapabilityEvaluationResult& operator=(CapabilityEvaluationResult&&) noexcept;
  ~CapabilityEvaluationResult();

  CapabilityDecisionKind decision = CapabilityDecisionKind::kDeny;
  CapabilityDenyReason deny_reason = CapabilityDenyReason::kNone;
  std::string message;
  CapabilityObligations obligations;
  std::optional<ApprovalRequest> approval_request;
  std::optional<UserPresenceRequest> presence_request;

  bool IsPermitted() const {
    return decision == CapabilityDecisionKind::kPermit;
  }
  bool IsDenied() const {
    return decision == CapabilityDecisionKind::kDeny;
  }
  bool RequiresApproval() const {
    return decision == CapabilityDecisionKind::kRequiresApproval;
  }
  bool RequiresUserPresence() const {
    return decision == CapabilityDecisionKind::kRequiresUserPresence;
  }

  static CapabilityEvaluationResult Permit(CapabilityObligations obligations);
  static CapabilityEvaluationResult Deny(CapabilityDenyReason reason,
                                         std::string_view message);
  static CapabilityEvaluationResult RequiresApproval(
      ApprovalRequest request,
      CapabilityObligations obligations);
  static CapabilityEvaluationResult RequiresUserPresence(
      UserPresenceRequest request);
};

struct FeatureGateState {
  bool mail_enabled = true;
  bool routines_enabled = true;
  bool vault_enabled = true;
};

class MahoCapabilityBroker {
 public:
  MahoCapabilityBroker();
  ~MahoCapabilityBroker();

  MahoCapabilityBroker(const MahoCapabilityBroker&) = delete;
  MahoCapabilityBroker& operator=(const MahoCapabilityBroker&) = delete;

  static const CapabilityDescriptor* FindCapability(
      std::string_view identifier);
  static const CapabilityDescriptor* FindCapabilityById(
      std::string_view canonical_id);
  static const CapabilityDescriptor* FindCapabilityByToolName(
      std::string_view tool_name);
  static base::span<const CapabilityDescriptor> GetAllCapabilities();

  static bool ValidateDescriptorInvariants(
      const CapabilityDescriptor& descriptor);
  static bool ValidateAllDescriptors();

  CapabilityEvaluationResult Evaluate(
      const CapabilityRequestContext& context,
      const FeatureGateState& features = FeatureGateState()) const;

  CapabilityEvaluationResult RevalidateAfterPresence(
      const CapabilityRequestContext& original_context,
      std::string_view validated_presence_token,
      std::string_view bound_digest,
      base::Time presence_timestamp,
      base::Time current_time,
      const FeatureGateState& features = FeatureGateState()) const;

  std::vector<const CapabilityDescriptor*> GetCapabilitiesForSurface(
      CapabilitySurface surface,
      const FeatureGateState& features = FeatureGateState()) const;
};

inline constexpr std::string_view CapabilityDecisionKindName(
    CapabilityDecisionKind kind) {
  switch (kind) {
    case CapabilityDecisionKind::kPermit:
      return "permit";
    case CapabilityDecisionKind::kDeny:
      return "deny";
    case CapabilityDecisionKind::kRequiresApproval:
      return "requires_approval";
    case CapabilityDecisionKind::kRequiresUserPresence:
      return "requires_user_presence";
  }
  return "deny";
}

inline constexpr std::string_view CapabilityDenyReasonName(
    CapabilityDenyReason reason) {
  switch (reason) {
    case CapabilityDenyReason::kNone:
      return "none";
    case CapabilityDenyReason::kUnknownCapability:
      return "unknown_capability";
    case CapabilityDenyReason::kSurfaceNotAllowed:
      return "surface_not_allowed";
    case CapabilityDenyReason::kFeatureDisabled:
      return "feature_disabled";
    case CapabilityDenyReason::kIncognitoOrGuestDisallowed:
      return "incognito_or_guest_disallowed";
    case CapabilityDenyReason::kMissingApproval:
      return "missing_approval";
    case CapabilityDenyReason::kInvalidApprovalToken:
      return "invalid_approval_token";
    case CapabilityDenyReason::kMissingLease:
      return "missing_lease";
    case CapabilityDenyReason::kInvalidLeaseToken:
      return "invalid_lease_token";
    case CapabilityDenyReason::kOriginNotAllowed:
      return "origin_not_allowed";
    case CapabilityDenyReason::kDestinationOriginRequired:
      return "destination_origin_required";
    case CapabilityDenyReason::kDomainBlocked:
      return "domain_blocked";
    case CapabilityDenyReason::kTabNotAdopted:
      return "tab_not_adopted";
    case CapabilityDenyReason::kInsufficientPrincipalAuth:
      return "insufficient_principal_auth";
    case CapabilityDenyReason::kMissingUserPresence:
      return "missing_user_presence";
    case CapabilityDenyReason::kStaleUserPresence:
      return "stale_user_presence";
    case CapabilityDenyReason::kTransactionBindingMismatch:
      return "transaction_binding_mismatch";
    case CapabilityDenyReason::kInsufficientDeviceProtection:
      return "insufficient_device_protection";
    case CapabilityDenyReason::kInvalidRequestContext:
      return "invalid_request_context";
  }
  return "none";
}

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_CAPABILITY_BROKER_H_
