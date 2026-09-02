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

// NOTE: CapabilityRequestContext (below) carries the session runtime_config
// triple (permission_tier / final_confirm / proactive_mode).

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

  // Session runtime_config plumbing (plan row 1). The defaults mirror
  // today's effective broker behavior, so carrying the triple on the
  // request context changes no decision; enforcement reads these fields
  // in later plan rows only.
  std::string permission_tier = "guard";
  bool final_confirm = true;
  bool proactive_mode = false;

  // Runtime permission-tier file gate inputs (plan row 3). `fs_path` is the
  // local filesystem path this request reads or writes; empty means the
  // request is not file-scoped and the tier gate is inert.
  // `fs_whitelist_roots` are the session-supplied absolute directory roots
  // (account dirs, Downloads, Documents, session dirs); an empty list means
  // nothing is whitelisted (fail closed).
  std::string fs_path;
  std::vector<std::string> fs_whitelist_roots;

  // Final-confirmation gate input (plan row 5). Browser-minted consequence
  // classification for this request (e.g. "submit", "ordinary"); empty
  // means the request is not consequence-scoped and the gate is inert,
  // except for browser action tools whose missing classification is Unknown
  // and fails closed to gated. Never model- or MCP-argument-supplied.
  std::string action_consequence;
};

// Parses a raw session permission-tier string into its canonical form,
// failing closed to "guard" for empty or unknown input (plan row 1
// plumbing; enforcement consumes the parsed value in later rows).
std::string ParseRuntimeConfigTier(std::string_view raw_tier);

// Component-boundary whitelist containment for the runtime permission-tier
// file gate (plan row 3). A path is inside when it equals a root or lives
// underneath it at a path-component boundary. Fails closed: empty path,
// relative path, empty root list, or empty roots never match.
bool FsPathInsideRuntimeWhitelist(
    const std::string& path,
    const std::vector<std::string>& whitelist_roots);

// Final-confirmation gate classification (plan row 5). Returns true when the
// resolved consequence class must obtain a user confirmation before commit
// under final_confirm=true: submit, purchase_or_transfer, account_security,
// credential_fill, file_transfer, destructive. "ordinary" and "new_origin"
// (navigation is reversible) are free; unknown, unrecognized, or empty input
// fails closed to gated. The accepted strings mirror the Rust
// classify_action_consequence vocabulary in
// maho/crates/maho-agent/src/permission.rs.
bool IsFinalConfirmGatedConsequence(std::string_view consequence);

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
  // Runtime permission-tier denials (plan row 3). The serialized names in
  // CapabilityDenyReasonName are machine-consumed contract strings and must
  // stay in lockstep with the Rust constants PERMISSION_TIER_DENIED_* in
  // maho/crates/maho-agent/src/permission.rs.
  kPermissionTierDeniedWrite,
  kPermissionTierDeniedRead,
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
    case CapabilityDenyReason::kPermissionTierDeniedWrite:
      return "permission_tier_denied_write";
    case CapabilityDenyReason::kPermissionTierDeniedRead:
      return "permission_tier_denied_read";
  }
  return "none";
}

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_CAPABILITY_BROKER_H_
