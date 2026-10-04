// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_CAPABILITY_TYPES_H_
#define MAHO_BROWSER_AI_MAHO_CAPABILITY_TYPES_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"

struct BrowserActionContract;

namespace maho::ai {

enum class CapabilityCategory {
  kUnknown,
  kTabs,
  kNavigation,
  kPage,
  kInput,
  kHistory,
  kBookmarks,
  kPolicy,
  kRoutines,
  kMail,
  kLease,
  kVault,
  kCapture,
};

enum class CapabilityMutability {
  kUnknown,
  kReadOnly,
  kMutable,
};

enum class CapabilityBrokerType {
  kUnknown,
  kBrowser,
  kMail,
  kRoutines,
  kVault,
};

enum class CapabilityBoundary {
  kUnknown,
  kSession,
  kProfile,
  kTab,
  kAccount,
};

enum class CapabilitySensitivity {
  kUnknown,
  kLow,
  kSensitive,
  kCredential,
};

enum class CapabilityApprovalRequirement {
  kUnknown,
  kNotRequired,
  kRequired,
};

enum class CapabilityLeaseRequirement {
  kUnknown,
  kNotRequired,
  kRequired,
};

enum class CapabilityOriginPolicy {
  kUnknown,
  kNotApplicable,
  kActiveTabOrigin,
  kActiveTabOriginOrApprovedDestination,
  kExactGrantedOrigin,
  kAnyOrigin,
};

enum class CapabilityRedactionPolicy {
  kNone,
  kUrlAndToken,
  kHar,
  kCredential,
  kScreenshot,
};

enum class CapabilityFeatureGate {
  kUnknown,
  kAlways,
  kMailBeta,
  kRoutines,
  kVault,
};

enum CapabilitySurface : uint32_t {
  kSurfaceNone = 0,
  kDesktopAgent = 1u << 0,
  kInBrowserAgent = kDesktopAgent,
  kPublicMcp = 1u << 1,
  kBrowserMcp = kPublicMcp,
  kControlPlane = 1u << 2,
  kCliAgent = 1u << 3,
  kCliGeneric = 1u << 4,
  kRoutine = 1u << 5,
  kExtensionHelper = 1u << 6,
  kUtilityProcess = 1u << 7,
};
using CapabilitySurfaceMask = uint32_t;

inline constexpr CapabilitySurfaceMask kDesktopAgentAndControlPlane =
    kDesktopAgent | kControlPlane;
inline constexpr CapabilitySurfaceMask kDesktopAgentAndBrowserMcp =
    kDesktopAgent | kBrowserMcp;
inline constexpr CapabilitySurfaceMask kAllSurfaces =
    kDesktopAgent | kPublicMcp | kControlPlane | kCliAgent | kCliGeneric |
    kRoutine | kExtensionHelper | kUtilityProcess;

enum class CapabilityMissingPolicy {
  kUnknown,
  kFailClosed,
};

enum class CapabilityAuditClass {
  kNone,
  kRead,
  kMutation,
  kSecuritySensitive,
  kHighRisk,
};

enum class UserPresenceRequirement {
  kNone,
  kLocalPresence,
  kBiometricAny,
  kBiometricStrong,
  kHardwareToken,
};

enum class MinimumDeviceProtection {
  kNone,
  kOSKeystore,
  kHardwareKey,
  kSecureEnclaveOrTpm,
};

struct CapabilityDescriptor {
  CapabilityDescriptor();
  CapabilityDescriptor(const CapabilityDescriptor&);
  CapabilityDescriptor(CapabilityDescriptor&&) noexcept;
  CapabilityDescriptor& operator=(const CapabilityDescriptor&);
  CapabilityDescriptor& operator=(CapabilityDescriptor&&) noexcept;
  ~CapabilityDescriptor();

  std::string_view canonical_id;
  std::string_view tool_name;
  int schema_version = 1;
  int result_version = 1;
  CapabilityCategory category = CapabilityCategory::kUnknown;
  CapabilityMutability mutability = CapabilityMutability::kUnknown;
  bool changes_authority = false;
  CapabilityBrokerType required_broker = CapabilityBrokerType::kUnknown;
  CapabilityBoundary resource_boundary = CapabilityBoundary::kUnknown;
  CapabilitySensitivity sensitivity = CapabilitySensitivity::kUnknown;
  CapabilityApprovalRequirement approval_requirement =
      CapabilityApprovalRequirement::kUnknown;
  CapabilityLeaseRequirement lease_requirement =
      CapabilityLeaseRequirement::kUnknown;
  CapabilityOriginPolicy origin_policy = CapabilityOriginPolicy::kUnknown;
  CapabilityRedactionPolicy redaction_policy = CapabilityRedactionPolicy::kNone;
  CapabilityFeatureGate feature_gate = CapabilityFeatureGate::kUnknown;
  CapabilitySurfaceMask allowed_surfaces = kSurfaceNone;
  int request_schema_version = 1;
  int response_schema_version = 1;
  CapabilityMissingPolicy missing_policy = CapabilityMissingPolicy::kFailClosed;
  CapabilityAuditClass audit_class = CapabilityAuditClass::kNone;
  UserPresenceRequirement user_presence_requirement =
      UserPresenceRequirement::kNone;
  base::TimeDelta presence_freshness;
  MinimumDeviceProtection minimum_device_protection =
      MinimumDeviceProtection::kNone;
  bool transaction_binding = false;
  std::string_view description;
  raw_ptr<const BrowserActionContract> browser_action_contract = nullptr;
};

inline constexpr bool IsSensitive(const CapabilityDescriptor& descriptor) {
  return descriptor.sensitivity == CapabilitySensitivity::kSensitive ||
         descriptor.sensitivity == CapabilitySensitivity::kCredential;
}

inline constexpr bool RequiresApproval(const CapabilityDescriptor& descriptor) {
  return descriptor.approval_requirement ==
         CapabilityApprovalRequirement::kRequired;
}

inline constexpr bool RequiresLease(const CapabilityDescriptor& descriptor) {
  return descriptor.lease_requirement == CapabilityLeaseRequirement::kRequired;
}

inline constexpr bool RequiresUserPresence(
    const CapabilityDescriptor& descriptor) {
  return descriptor.user_presence_requirement !=
         UserPresenceRequirement::kNone;
}

inline constexpr bool EmptyAllowlistFailsClosed(
    const CapabilityDescriptor& descriptor) {
  return descriptor.missing_policy == CapabilityMissingPolicy::kFailClosed;
}

inline constexpr std::string_view CapabilityCategoryName(
    CapabilityCategory category) {
  switch (category) {
    case CapabilityCategory::kUnknown:
      return "unknown";
    case CapabilityCategory::kTabs:
      return "tabs";
    case CapabilityCategory::kNavigation:
      return "navigation";
    case CapabilityCategory::kPage:
      return "page";
    case CapabilityCategory::kInput:
      return "input";
    case CapabilityCategory::kHistory:
      return "history";
    case CapabilityCategory::kBookmarks:
      return "bookmarks";
    case CapabilityCategory::kPolicy:
      return "policy";
    case CapabilityCategory::kRoutines:
      return "routines";
    case CapabilityCategory::kMail:
      return "mail";
    case CapabilityCategory::kLease:
      return "lease";
    case CapabilityCategory::kVault:
      return "vault";
    case CapabilityCategory::kCapture:
      return "capture";
  }
  return "unknown";
}

inline constexpr std::string_view CapabilityMutabilityName(
    CapabilityMutability mutability) {
  switch (mutability) {
    case CapabilityMutability::kUnknown:
      return "unknown";
    case CapabilityMutability::kReadOnly:
      return "read_only";
    case CapabilityMutability::kMutable:
      return "mutable";
  }
  return "unknown";
}

inline constexpr std::string_view CapabilityBrokerTypeName(
    CapabilityBrokerType broker) {
  switch (broker) {
    case CapabilityBrokerType::kUnknown:
      return "unknown";
    case CapabilityBrokerType::kBrowser:
      return "browser";
    case CapabilityBrokerType::kMail:
      return "mail";
    case CapabilityBrokerType::kRoutines:
      return "routines";
    case CapabilityBrokerType::kVault:
      return "vault";
  }
  return "unknown";
}

inline constexpr std::string_view CapabilityBoundaryName(
    CapabilityBoundary boundary) {
  switch (boundary) {
    case CapabilityBoundary::kUnknown:
      return "unknown";
    case CapabilityBoundary::kSession:
      return "session";
    case CapabilityBoundary::kProfile:
      return "profile";
    case CapabilityBoundary::kTab:
      return "tab";
    case CapabilityBoundary::kAccount:
      return "account";
  }
  return "unknown";
}

inline constexpr std::string_view CapabilitySensitivityName(
    CapabilitySensitivity sensitivity) {
  switch (sensitivity) {
    case CapabilitySensitivity::kUnknown:
      return "unknown";
    case CapabilitySensitivity::kLow:
      return "low";
    case CapabilitySensitivity::kSensitive:
      return "sensitive";
    case CapabilitySensitivity::kCredential:
      return "credential";
  }
  return "unknown";
}

inline constexpr std::string_view CapabilityApprovalRequirementName(
    CapabilityApprovalRequirement req) {
  switch (req) {
    case CapabilityApprovalRequirement::kUnknown:
      return "unknown";
    case CapabilityApprovalRequirement::kNotRequired:
      return "not_required";
    case CapabilityApprovalRequirement::kRequired:
      return "required";
  }
  return "unknown";
}

inline constexpr std::string_view CapabilityLeaseRequirementName(
    CapabilityLeaseRequirement req) {
  switch (req) {
    case CapabilityLeaseRequirement::kUnknown:
      return "unknown";
    case CapabilityLeaseRequirement::kNotRequired:
      return "not_required";
    case CapabilityLeaseRequirement::kRequired:
      return "required";
  }
  return "unknown";
}

inline constexpr std::string_view CapabilityOriginPolicyName(
    CapabilityOriginPolicy policy) {
  switch (policy) {
    case CapabilityOriginPolicy::kUnknown:
      return "unknown";
    case CapabilityOriginPolicy::kNotApplicable:
      return "not_applicable";
    case CapabilityOriginPolicy::kActiveTabOrigin:
      return "active_tab_origin";
    case CapabilityOriginPolicy::kActiveTabOriginOrApprovedDestination:
      return "active_tab_origin_or_approved_destination";
    case CapabilityOriginPolicy::kExactGrantedOrigin:
      return "exact_granted_origin";
    case CapabilityOriginPolicy::kAnyOrigin:
      return "any_origin";
  }
  return "unknown";
}

inline constexpr std::string_view CapabilityRedactionPolicyName(
    CapabilityRedactionPolicy policy) {
  switch (policy) {
    case CapabilityRedactionPolicy::kNone:
      return "none";
    case CapabilityRedactionPolicy::kUrlAndToken:
      return "url_and_token";
    case CapabilityRedactionPolicy::kHar:
      return "har";
    case CapabilityRedactionPolicy::kCredential:
      return "credential";
    case CapabilityRedactionPolicy::kScreenshot:
      return "screenshot";
  }
  return "none";
}

inline constexpr std::string_view CapabilityFeatureGateName(
    CapabilityFeatureGate gate) {
  switch (gate) {
    case CapabilityFeatureGate::kUnknown:
      return "unknown";
    case CapabilityFeatureGate::kAlways:
      return "always";
    case CapabilityFeatureGate::kMailBeta:
      return "mail_beta";
    case CapabilityFeatureGate::kRoutines:
      return "routines";
    case CapabilityFeatureGate::kVault:
      return "vault";
  }
  return "unknown";
}

inline constexpr std::string_view CapabilityAuditClassName(
    CapabilityAuditClass audit_class) {
  switch (audit_class) {
    case CapabilityAuditClass::kNone:
      return "none";
    case CapabilityAuditClass::kRead:
      return "read";
    case CapabilityAuditClass::kMutation:
      return "mutation";
    case CapabilityAuditClass::kSecuritySensitive:
      return "security_sensitive";
    case CapabilityAuditClass::kHighRisk:
      return "high_risk";
  }
  return "none";
}

inline constexpr std::string_view UserPresenceRequirementName(
    UserPresenceRequirement req) {
  switch (req) {
    case UserPresenceRequirement::kNone:
      return "none";
    case UserPresenceRequirement::kLocalPresence:
      return "local_presence";
    case UserPresenceRequirement::kBiometricAny:
      return "biometric_any";
    case UserPresenceRequirement::kBiometricStrong:
      return "biometric_strong";
    case UserPresenceRequirement::kHardwareToken:
      return "hardware_token";
  }
  return "none";
}

inline constexpr std::string_view MinimumDeviceProtectionName(
    MinimumDeviceProtection prot) {
  switch (prot) {
    case MinimumDeviceProtection::kNone:
      return "none";
    case MinimumDeviceProtection::kOSKeystore:
      return "os_keystore";
    case MinimumDeviceProtection::kHardwareKey:
      return "hardware_key";
    case MinimumDeviceProtection::kSecureEnclaveOrTpm:
      return "secure_enclave_or_tpm";
  }
  return "none";
}

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_CAPABILITY_TYPES_H_
