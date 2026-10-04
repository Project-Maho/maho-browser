// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_BROWSER_ACTION_CONTRACT_H_
#define MAHO_BROWSER_AI_MAHO_BROWSER_ACTION_CONTRACT_H_

#include <array>
#include <cstddef>
#include <string_view>

namespace maho::ai {

enum class BrowserActionKind {
  kRead,
  kAction,
};

enum class BrowserActionSensitivity {
  kLow,
  kSensitive,
};

enum class BrowserActionApproval {
  kNotRequired,
  kRequired,
};

enum class BrowserActionLease {
  kNotRequired,
  kRequired,
};

enum class BrowserActionDomainPolicy {
  kActiveTabOrigin,
  kActiveTabOriginOrApprovedDestination,
};

enum class BrowserActionEmptyAllowlistPolicy {
  kFailClosed,
};

struct BrowserActionContract {
  std::string_view tool_name;
  BrowserActionKind kind;
  bool changes_authority;
  BrowserActionSensitivity sensitivity;
  BrowserActionApproval approval;
  BrowserActionLease lease;
  BrowserActionDomainPolicy domain_policy;
  BrowserActionEmptyAllowlistPolicy empty_allowlist_policy;
};

// Stable public-contract reason codes for generic typing into credential-class
// fields. Keep these strings synchronized with maho-agent permission.rs and the
// website Agent/MCP documentation. allow_credentials requests this gate; it is
// never evidence that the user approved it.
inline constexpr std::string_view kCredentialTypingApprovalRequired =
    "credential_typing_approval_required";
inline constexpr std::string_view kCredentialTypingDenied =
    "credential_typing_denied";

enum class CredentialTypingAuthorizationAction {
  kAllow,
  kRequireApproval,
};

struct CredentialTypingAuthorizationDecision {
  CredentialTypingAuthorizationAction action =
      CredentialTypingAuthorizationAction::kAllow;
  std::string_view reason_code;
};

constexpr CredentialTypingAuthorizationDecision AuthorizeCredentialTyping(
    bool allow_credentials_requested) {
  if (!allow_credentials_requested) {
    return {CredentialTypingAuthorizationAction::kAllow, {}};
  }
  return {CredentialTypingAuthorizationAction::kRequireApproval,
          kCredentialTypingApprovalRequired};
}

inline constexpr bool IsSensitive(const BrowserActionContract& contract) {
  return contract.sensitivity == BrowserActionSensitivity::kSensitive;
}

inline constexpr bool RequiresApproval(const BrowserActionContract& contract) {
  return contract.approval == BrowserActionApproval::kRequired;
}

// Product policy: driving the browser is full access. Clicking, typing,
// scrolling, navigating, reading a page, and managing tabs or leases never ask
// the user for permission, no matter which controller issues the call. An
// approval gate exists only where a capability leaves the browser:
//
//   * another resource broker:            mail.*, vault.*, routines.*
//   * the consent/authority plane itself: control.*, policy.*
//   * the local filesystem:               artifact.export,
//                                         input.file_upload_select
//   * OS-level (computer use) input:      browser.visual_click
//
// Caller identity never decides this; only the capability does. The predicate
// is keyed on the canonical capability id so that both descriptor flavors
// (maho::ai::CapabilityDescriptor and
// MahoBrowserToolRegistry::CapabilityDescriptor) share one policy source.
inline constexpr bool CapabilityLeavesBrowserBoundary(
    std::string_view canonical_id) {
  constexpr std::string_view kOffBrowserPrefixes[] = {
      "mail.", "vault.", "routines.", "control.", "policy.",
  };
  for (std::string_view prefix : kOffBrowserPrefixes) {
    if (canonical_id.starts_with(prefix)) {
      return true;
    }
  }
  return canonical_id == "artifact.export";
}

inline constexpr bool RequiresLease(const BrowserActionContract& contract) {
  return contract.lease == BrowserActionLease::kRequired;
}

inline constexpr bool EmptyAllowlistFailsClosed(
    const BrowserActionContract& contract) {
  return contract.empty_allowlist_policy ==
         BrowserActionEmptyAllowlistPolicy::kFailClosed;
}

inline constexpr std::string_view BrowserActionKindName(
    BrowserActionKind kind) {
  switch (kind) {
    case BrowserActionKind::kRead:
      return "read";
    case BrowserActionKind::kAction:
      return "action";
  }
  return "action";
}

inline constexpr std::string_view BrowserActionSensitivityName(
    BrowserActionSensitivity sensitivity) {
  switch (sensitivity) {
    case BrowserActionSensitivity::kLow:
      return "low";
    case BrowserActionSensitivity::kSensitive:
      return "sensitive";
  }
  return "sensitive";
}

inline constexpr std::string_view BrowserActionApprovalName(
    BrowserActionApproval approval) {
  switch (approval) {
    case BrowserActionApproval::kNotRequired:
      return "not_required";
    case BrowserActionApproval::kRequired:
      return "required";
  }
  return "required";
}

inline constexpr std::string_view BrowserActionLeaseName(
    BrowserActionLease lease) {
  switch (lease) {
    case BrowserActionLease::kNotRequired:
      return "not_required";
    case BrowserActionLease::kRequired:
      return "required";
  }
  return "required";
}

inline constexpr std::string_view BrowserActionDomainPolicyName(
    BrowserActionDomainPolicy policy) {
  switch (policy) {
    case BrowserActionDomainPolicy::kActiveTabOrigin:
      return "active_tab_origin";
    case BrowserActionDomainPolicy::kActiveTabOriginOrApprovedDestination:
      return "active_tab_origin_or_approved_destination";
  }
  return "active_tab_origin";
}

inline constexpr std::string_view BrowserActionEmptyAllowlistPolicyName(
    BrowserActionEmptyAllowlistPolicy policy) {
  switch (policy) {
    case BrowserActionEmptyAllowlistPolicy::kFailClosed:
      return "fail_closed";
  }
  return "fail_closed";
}

inline constexpr std::size_t kMahoBrowserActionContractCount = 0
#define MAHO_BROWSER_ACTION_CONTRACT(id, tool_name, kind, authority_change,    \
                                     sensitivity, approval, lease,             \
                                     domain_policy, empty_allowlist) +1
#include "maho/browser/ai/maho_browser_action_contract.def"
#undef MAHO_BROWSER_ACTION_CONTRACT
    ;

inline constexpr std::array<BrowserActionContract,
                            kMahoBrowserActionContractCount>
    kMahoBrowserActionContracts = {{
#define MAHO_BROWSER_ACTION_CONTRACT(id, tool_name, kind, authority_change,    \
                                     sensitivity, approval, lease,             \
                                     domain_policy, empty_allowlist)            \
  {tool_name,                                                                 \
   BrowserActionKind::k##kind,                                                \
   std::string_view(#authority_change) == "Yes",                              \
   BrowserActionSensitivity::k##sensitivity,                                  \
   BrowserActionApproval::k##approval,                                        \
   BrowserActionLease::k##lease,                                              \
   BrowserActionDomainPolicy::k##domain_policy,                               \
   BrowserActionEmptyAllowlistPolicy::k##empty_allowlist},
#include "maho/browser/ai/maho_browser_action_contract.def"
#undef MAHO_BROWSER_ACTION_CONTRACT
}};

inline constexpr const BrowserActionContract* FindBrowserActionContract(
    std::string_view tool_name) {
  for (const BrowserActionContract& contract : kMahoBrowserActionContracts) {
    if (contract.tool_name == tool_name) {
      return &contract;
    }
  }
  return nullptr;
}

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_BROWSER_ACTION_CONTRACT_H_
