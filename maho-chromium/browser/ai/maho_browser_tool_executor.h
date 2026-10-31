// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_BROWSER_TOOL_EXECUTOR_H_
#define MAHO_BROWSER_AI_MAHO_BROWSER_TOOL_EXECUTOR_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/values.h"
#include "maho/browser/ai/maho_ai_page_context_extractor.h"
#include "maho/browser/ai/maho_mail_tool_authorization.h"
#include "ui/accessibility/ax_node_id_forward.h"

class Browser;

namespace maho {
class MahoMcpLeaseRegistry;
class MahoMcpBrowserDelegate;
}

namespace maho::ai {
struct BrowserActionContract;
struct CapabilityRequestContext;
}

// Global-scope session runtime config (maho_ai_runtime_adapter.h).
struct MahoAiRuntimeConfig;

namespace content {
class WebContents;
}

class MahoBrowserToolExecutor {
 public:
  struct BrowserActionAuthorization {
    BrowserActionAuthorization();
    BrowserActionAuthorization(const BrowserActionAuthorization&);
    BrowserActionAuthorization(BrowserActionAuthorization&&);
    BrowserActionAuthorization& operator=(const BrowserActionAuthorization&);
    BrowserActionAuthorization& operator=(BrowserActionAuthorization&&);
    ~BrowserActionAuthorization();

    std::string tool_name;
    std::string kind;
    std::string sensitivity;
    bool requires_approval = false;
    bool requires_lease = false;
    std::string domain_policy;
    std::string empty_allowlist_policy;
    std::string active_origin;
    std::string destination_origin;
    bool page_derived_justification = false;
  };

  struct BrowserActionApprovalDecision {
    bool approved = false;
    std::string error;
  };

  using ToolResultCallback = base::OnceCallback<void(base::DictValue result)>;
  using BrowserActionApprovalCallback = base::RepeatingCallback<
      BrowserActionApprovalDecision(const BrowserActionAuthorization& metadata)>;

  explicit MahoBrowserToolExecutor(
      Browser* browser,
      bool browser_tools_v1_enabled = true,
      base::RepeatingCallback<bool()> ai_gate = base::RepeatingCallback<bool()>(),
      BrowserActionApprovalCallback action_approval_callback =
          BrowserActionApprovalCallback(),
      maho::MahoMcpBrowserDelegate* browser_action_delegate_for_testing =
          nullptr,
      maho::MahoMcpLeaseRegistry* browser_action_lease_registry_for_testing =
          nullptr,
      std::string browser_action_lease_holder_id = std::string(),
      const MahoAiRuntimeConfig* runtime_config = nullptr);
  MahoBrowserToolExecutor(const MahoBrowserToolExecutor&) = delete;
  MahoBrowserToolExecutor& operator=(const MahoBrowserToolExecutor&) = delete;
  ~MahoBrowserToolExecutor();

  void Execute(const std::string& tool_name,
               const base::DictValue& arguments,
               ToolResultCallback callback);

  static base::DictValue BuildErrorResult(const std::string& tool_name,
                                            const std::string& error);

  // Wave 2A (G6 read-back): stamps the session runtime-config triple plus the
  // tier file-gate inputs into a broker request context. |runtime_config| is
  // the adapter's live session config; nullptr keeps the broker defaults.
  // |fs_path| is taken from the request's "path" argument (empty = the request
  // is not file-scoped and the tier gate is inert). Exposed so unittests can
  // pin the exact fields stamped at both broker-gate sites.
  static void StampRuntimeConfig(
      maho::ai::CapabilityRequestContext* req_ctx,
      const MahoAiRuntimeConfig* runtime_config,
      const base::DictValue& arguments);

  // True for every sensitive Mail write/account mutation in the canonical
  // Mail authorization table.
  static bool IsMailWriteTool(std::string_view tool_name);
  // True only for Mail reads whose asynchronous result must be re-authorized
  // immediately before it is returned to the Agent.
  static bool RequiresMailCompletionAuthorization(std::string_view tool_name);

 private:
  void ExecuteReadCurrentPage(ToolResultCallback callback);
  void ExecuteGetSelectedText(ToolResultCallback callback);
  void ExecuteGetActiveTab(ToolResultCallback callback);
  void ExecuteSearchInPage(const base::DictValue& arguments,
                           ToolResultCallback callback);
  void ExecuteExtractStructuredPageContext(ToolResultCallback callback);
  void ExecuteMailTool(const std::string& tool_name,
                       const base::DictValue& arguments,
                       ToolResultCallback callback);
  // Runs the agent approval callback for a mail write tool. Returns true when
  // approved; on denial (or when no approval callback is wired) fills *error
  // and returns false.
  bool ApproveMailWrite(const std::string& tool_name, std::string* error);
  // Serializes structured mail-compose tool arguments into the JSON request
  // string the mail broker expects. Returns std::nullopt with *error set when
  // required fields are missing or an attachment is malformed.
  static std::optional<std::string> BuildComposeRequestJson(
      const base::DictValue& arguments,
      std::string* error);
  maho::ai::MailAuthorizationContext GetMailAuthorizationContext() const;
  void OnMailToolResult(const std::string& tool_name,
                        std::optional<uint64_t> helper_generation,
                        ToolResultCallback callback,
                        bool ok,
                        std::string result_json);
  void ExecuteBrowserAction(const maho::ai::BrowserActionContract& contract,
                            const base::DictValue& arguments,
                            ToolResultCallback callback);
  void ExecuteScreenshotProof(const std::string& tool_name,
                              const base::DictValue& arguments,
                              ToolResultCallback callback);
  void ExecuteBrowserObserve(const base::DictValue& arguments,
                             ToolResultCallback callback);

  void OnReadCurrentPageContext(ToolResultCallback callback,
                               MahoAiPageContextExtractor::PageContextResult result);
  void OnSearchInPageContext(ToolResultCallback callback,
                             std::string query,
                             int max_results,
                             MahoAiPageContextExtractor::PageContextResult result);
  void OnExtractStructuredPageContext(
      ToolResultCallback callback,
      MahoAiPageContextExtractor::PageContextResult result);
  void OnSelectedTextFromContext(ToolResultCallback callback,
                                 MahoAiPageContextExtractor::PageContextResult result);

  static base::DictValue BuildActiveTabResult(Browser* browser);
  static base::DictValue BuildReadCurrentPageResult(
      const MahoAiPageContextExtractor::PageContextResult& result);
  static base::DictValue BuildSearchResult(const std::string& query,
                                             const std::string& page_text,
                                             size_t max_results);
  static std::string GetStringArgument(const base::DictValue& arguments,
                                       const std::string& key);
  static int GetIntArgument(const base::DictValue& arguments,
                             const std::string& key,
                             int default_value);
  static base::DictValue BuildBrowserActionErrorResult(
      const std::string& tool_name,
      int error_code,
      const std::string& error);
  static base::DictValue BuildBrowserActionSuccessResult(
      const std::string& tool_name,
      base::DictValue action_result);

 public:
  enum class FallbackTier {
    kTypedDomainApi,
    kDomRefLocator,
    kAccessibilitySnapshot,
    kScreenshotCua,
  };

  enum class FallbackSafetyState {
    kSafe,
    kDowngraded,
    kBlocked,
    kEscalationApprovalRequired,
    kEscalationApproved,
  };

  struct BrowserActionFallbackDecision {
    FallbackTier attempted_tier = FallbackTier::kTypedDomainApi;
    FallbackTier selected_tier = FallbackTier::kTypedDomainApi;
    std::string reason;
    FallbackSafetyState safety_state = FallbackSafetyState::kSafe;
    bool approval_required = false;
  };

  struct BrowserActionFallbackProgress {
    bool typed_domain_succeeded = false;
    bool dom_ref_selected = false;
    bool accessibility_snapshot_selected = false;
  };

  static const char* FallbackTierName(FallbackTier tier);
  static const char* FallbackSafetyStateName(FallbackSafetyState state);
  static void AttachFallbackDecision(
      base::DictValue& result,
      const BrowserActionFallbackDecision& decision);
  bool EnforceBrowserActionFallback(
      const maho::ai::BrowserActionContract& contract,
      const base::DictValue& arguments,
      BrowserActionFallbackDecision& decision,
      std::string& error);
  void RecordBrowserActionFallbackSuccess(
      const maho::ai::BrowserActionContract& contract,
      const BrowserActionFallbackDecision& decision);

  void ExecuteBrowserObserve(
      const base::DictValue& arguments,
      int tab_id,
      maho::MahoMcpBrowserDelegate* delegate,
      BrowserActionFallbackDecision fallback_decision,
      const maho::ai::BrowserActionContract& contract,
      ToolResultCallback callback);

 private:

  content::WebContents* GetActiveWebContents() const;
  maho::MahoMcpBrowserDelegate* GetBrowserActionDelegate() const;
  std::optional<int> ResolveBrowserActionTabId(
      maho::MahoMcpBrowserDelegate* delegate,
      const base::DictValue& arguments,
      std::string& error) const;
  std::optional<std::string> GetActiveTabOrigin(
      maho::MahoMcpBrowserDelegate* delegate,
      int tab_id) const;
  bool AuthorizeBrowserAction(const maho::ai::BrowserActionContract& contract,
                              const base::DictValue& arguments,
                              int tab_id,
                              maho::MahoMcpBrowserDelegate* delegate,
                              std::string& error);
  bool EnsureBrowserActionLease(const maho::ai::BrowserActionContract& contract,
                                int tab_id,
                                std::string& error);
  // Runs the action approval callback for a broker-mandated ask (tier file
  // ask, final-confirm gate) on a tool whose contract carries no approval
  // handshake of its own. Returns true when approved; on denial (or when no
  // callback is wired) fills *error and returns false.
  bool RunBrokerApprovalAsk(const std::string& tool_name,
                            const std::string& kind,
                            const std::string& sensitivity,
                            const std::string& active_origin,
                            std::string* error);

  SEQUENCE_CHECKER(sequence_checker_);

  raw_ptr<Browser> browser_;
  bool browser_tools_v1_enabled_ = true;
  base::RepeatingCallback<bool()> ai_gate_;
  BrowserActionApprovalCallback action_approval_callback_;
  raw_ptr<maho::MahoMcpBrowserDelegate> browser_action_delegate_for_testing_;
  raw_ptr<maho::MahoMcpLeaseRegistry>
      browser_action_lease_registry_for_testing_;
  std::string browser_action_lease_holder_id_;
  // Points at the adapter's live session runtime_config_ (Wave 2A). Never
  // owned; dereferenced only during the synchronous broker-gate prefix of
  // Execute(), so every Evaluate reads the current flags (per-call freshness
  // for mid-session tier tightening).
  raw_ptr<const MahoAiRuntimeConfig> runtime_config_;
  std::unordered_map<int, ui::AXNodeID> browser_action_refs_;
  int browser_action_refs_tab_id_ = 0;
  std::string browser_action_refs_origin_;
  std::unordered_map<int, int> browser_action_scroll_positions_;
  std::unordered_map<std::string, BrowserActionFallbackProgress>
      browser_action_fallback_progress_;
  std::unique_ptr<MahoAiPageContextExtractor> page_context_extractor_;
  base::WeakPtrFactory<MahoBrowserToolExecutor> weak_factory_{this};
};

#endif  // MAHO_BROWSER_AI_MAHO_BROWSER_TOOL_EXECUTOR_H_
