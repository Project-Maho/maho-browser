// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_MAIL_TOOL_AUTHORIZATION_H_
#define MAHO_BROWSER_AI_MAHO_MAIL_TOOL_AUTHORIZATION_H_

#include <array>
#include <cstdint>
#include <string_view>

namespace maho::ai {

inline constexpr std::array<std::string_view, 7> kMailReadTools = {
    "mail_list_accounts", "mail_list_folders", "mail_list_emails",
    "mail_get_email",     "mail_search_emails", "mail_list_thread",
    "mail_extract_otp"};

inline constexpr std::array<std::string_view, 12> kMailWriteAccountTools = {
    "mail_save_draft", "mail_update_draft", "mail_send",
    "mail_queue_email", "mail_flag", "mail_test_connection",
    "mail_add_account", "mail_delete_account", "mail_reconnect_account",
    "mail_start_oauth", "mail_complete_oauth",
    "mail_import_migration_archive"};

enum class MailGlobalPolicy { kPrompt, kAllow, kDeny };
enum class MailToolClass { kNotMail, kRead, kWriteAccount };
enum class MailAuthorizationAction { kAllow, kRequireApproval, kDeny };

struct MailAuthorizationContext {
  bool feature_enabled = false;
  bool helper_ready = false;
  bool helper_starting = false;
  bool read_allowed = false;
  MailGlobalPolicy global_policy = MailGlobalPolicy::kPrompt;
  uint64_t helper_generation = 0;
};

struct MailAuthorizationDecision {
  MailAuthorizationAction action = MailAuthorizationAction::kDeny;
  std::string_view reason_code = "mail_tool_unknown";
};

constexpr MailToolClass ClassifyMailTool(std::string_view tool_name) {
  for (std::string_view read_tool : kMailReadTools) {
    if (tool_name == read_tool) {
      return MailToolClass::kRead;
    }
  }
  for (std::string_view write_tool : kMailWriteAccountTools) {
    if (tool_name == write_tool) {
      return MailToolClass::kWriteAccount;
    }
  }
  return MailToolClass::kNotMail;
}

constexpr MailAuthorizationDecision AuthorizeMailTool(
    std::string_view tool_name,
    const MailAuthorizationContext& context) {
  const MailToolClass tool_class = ClassifyMailTool(tool_name);
  if (tool_class == MailToolClass::kNotMail) {
    return {MailAuthorizationAction::kDeny, "mail_tool_unknown"};
  }
  if (!context.feature_enabled) {
    return {MailAuthorizationAction::kDeny, "mail_feature_disabled"};
  }
  if (!context.helper_ready) {
    return {MailAuthorizationAction::kDeny, context.helper_starting
                                                    ? "mail_helper_starting"
                                                    : "mail_helper_unavailable"};
  }
  if (context.global_policy == MailGlobalPolicy::kDeny) {
    return {MailAuthorizationAction::kDeny, "mail_global_policy_denied"};
  }
  if (tool_class == MailToolClass::kRead) {
    if (!context.read_allowed) {
      return {MailAuthorizationAction::kDeny,
              "mail_read_consent_required"};
    }
    return {MailAuthorizationAction::kAllow, "mail_read_allowed"};
  }
  // Mail writes/account mutations never inherit global allow. They retain a
  // typed, per-call approval and backend validation under every non-deny
  // global policy.
  return {MailAuthorizationAction::kRequireApproval,
          "mail_typed_approval_required"};
}

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_MAIL_TOOL_AUTHORIZATION_H_
