// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_browser_tool_executor.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/ai/maho_ai_page_context_extractor.h"
#include "maho/browser/ai/maho_browser_action_contract.h"
#include "maho/browser/ai/maho_browser_tool_registry.h"
#include "maho/browser/mcp/maho_mcp_browser_action_handler.h"
#include "maho/browser/mcp/maho_mcp_firewall.h"
#include "maho/browser/mcp/maho_mcp_lease_registry.h"
#include "maho/browser/mcp/maho_mcp_session.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace {

constexpr size_t kDefaultSearchResultLimit = 5;
constexpr size_t kSnippetRadius = 80;
constexpr int kInvalidParamsError = -32602;
constexpr int kActionFailedError = -32000;
constexpr int kActionDeniedError = -32008;

bool HasUsablePageContext(
    const MahoAiPageContextExtractor::PageContextResult& result) {
  return result.HasUsableContent();
}

std::string ToolStatusToString(
    MahoAiPageContextExtractor::PageContextResult::Status status) {
  switch (status) {
    case MahoAiPageContextExtractor::PageContextResult::Status::kSuccess:
      return "success";
    case MahoAiPageContextExtractor::PageContextResult::Status::kPartial:
      return "partial";
    case MahoAiPageContextExtractor::PageContextResult::Status::kNoActiveTab:
      return "no_active_tab";
    case MahoAiPageContextExtractor::PageContextResult::Status::kCannotAccess:
      return "cannot_access";
    case MahoAiPageContextExtractor::PageContextResult::Status::kExtractionFailed:
      return "extraction_failed";
  }
  return "extraction_failed";
}

bool IsBlockedNavigationScheme(const GURL& url) {
  return url.SchemeIs("data") || url.SchemeIs("javascript") ||
         url.SchemeIs("file") || url.SchemeIs("chrome") ||
         url.SchemeIs("chrome-untrusted") || url.SchemeIs("blob") ||
         url.SchemeIs("filesystem");
}

std::optional<std::string> OriginString(const GURL& url) {
  if (!url.is_valid() || url.host().empty()) {
    return std::nullopt;
  }
  std::string serialized = url::Origin::Create(url).Serialize();
  if (serialized.empty() || serialized == "null") {
    return std::nullopt;
  }
  return serialized;
}

bool RequestsScreenshotCua(const std::string& tool_name,
                           const base::DictValue& arguments) {
  if (tool_name.find("screenshot") != std::string::npos ||
      tool_name.find("cua") != std::string::npos) {
    return true;
  }
  const std::string* fallback_tier = arguments.FindString("fallback_tier");
  if (fallback_tier && *fallback_tier == "screenshot_cua") {
    return true;
  }
  return arguments.FindBool("screenshot_cua").value_or(false);
}

bool ScreenshotCuaApproved(const base::DictValue& arguments) {
  return arguments.FindBool("screenshot_cua_approved").value_or(false);
}

std::string FallbackActionClassForToolName(const std::string& tool_name) {
  if (tool_name == "browser_navigate" ||
      tool_name == "browser_wait_for_navigation") {
    return "navigation";
  }
  if (tool_name == "browser_key_press") {
    return "keyboard";
  }
  if (tool_name == "browser_accessibility_snapshot" ||
      tool_name == "page_query_selector" || tool_name == "page_get_text" ||
      tool_name == "page_get_attribute" ||
      tool_name == "page_wait_for_selector") {
    return "page_read";
  }
  return "element_action";
}

MahoBrowserToolExecutor::FallbackTier InferFallbackTier(
    const std::string& tool_name,
    const base::DictValue& arguments) {
  if (RequestsScreenshotCua(tool_name, arguments)) {
    return MahoBrowserToolExecutor::FallbackTier::kScreenshotCua;
  }
  if (tool_name == "browser_navigate" || tool_name == "browser_key_press") {
    return MahoBrowserToolExecutor::FallbackTier::kTypedDomainApi;
  }
  if (tool_name == "page_query_selector" || tool_name == "page_get_text" ||
      tool_name == "page_get_attribute" ||
      tool_name == "page_wait_for_selector" ||
      tool_name == "browser_wait_for_navigation") {
    return MahoBrowserToolExecutor::FallbackTier::kDomRefLocator;
  }
  if (tool_name == "browser_accessibility_snapshot") {
    return MahoBrowserToolExecutor::FallbackTier::kAccessibilitySnapshot;
  }
  if (tool_name == "browser_scroll" && !arguments.FindInt("ref").has_value()) {
    return MahoBrowserToolExecutor::FallbackTier::kTypedDomainApi;
  }
  if (arguments.FindString("selector") || arguments.FindString("ref_id")) {
    return MahoBrowserToolExecutor::FallbackTier::kDomRefLocator;
  }
  return MahoBrowserToolExecutor::FallbackTier::kAccessibilitySnapshot;
}

}  // namespace

MahoBrowserToolExecutor::BrowserActionAuthorization::
    BrowserActionAuthorization() = default;
MahoBrowserToolExecutor::BrowserActionAuthorization::
    BrowserActionAuthorization(const BrowserActionAuthorization&) = default;
MahoBrowserToolExecutor::BrowserActionAuthorization::
    BrowserActionAuthorization(BrowserActionAuthorization&&) = default;
MahoBrowserToolExecutor::BrowserActionAuthorization&
MahoBrowserToolExecutor::BrowserActionAuthorization::operator=(
    const BrowserActionAuthorization&) = default;
MahoBrowserToolExecutor::BrowserActionAuthorization&
MahoBrowserToolExecutor::BrowserActionAuthorization::operator=(
    BrowserActionAuthorization&&) = default;
MahoBrowserToolExecutor::BrowserActionAuthorization::
    ~BrowserActionAuthorization() = default;

MahoBrowserToolExecutor::MahoBrowserToolExecutor(
    Browser* browser,
    bool browser_tools_v1_enabled,
    base::RepeatingCallback<bool()> ai_gate,
    BrowserActionApprovalCallback action_approval_callback,
    maho::MahoMcpBrowserDelegate* browser_action_delegate_for_testing,
    maho::MahoMcpLeaseRegistry* browser_action_lease_registry_for_testing,
    std::string browser_action_lease_holder_id)
    : browser_(browser),
      browser_tools_v1_enabled_(browser_tools_v1_enabled),
      ai_gate_(ai_gate),
      action_approval_callback_(std::move(action_approval_callback)),
      browser_action_delegate_for_testing_(browser_action_delegate_for_testing),
      browser_action_lease_registry_for_testing_(
          browser_action_lease_registry_for_testing),
      browser_action_lease_holder_id_(std::move(browser_action_lease_holder_id)),
      page_context_extractor_(
          std::make_unique<MahoAiPageContextExtractor>(browser, ai_gate)) {}

MahoBrowserToolExecutor::~MahoBrowserToolExecutor() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

// static
base::DictValue MahoBrowserToolExecutor::BuildErrorResult(
    const std::string& tool_name,
    const std::string& error) {
  base::DictValue result;
  result.Set("ok", false);
  result.Set("tool", tool_name);
  result.Set("error", error);
  result.Set("context_scope", "active_tab");
  return result;
}

void MahoBrowserToolExecutor::Execute(const std::string& tool_name,
                                      const base::DictValue& arguments,
                                      ToolResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!ai_gate_ || !ai_gate_.Run()) {
    std::move(callback).Run(BuildErrorResult(
        tool_name, "AI is not available in this context."));
    return;
  }

  if (!browser_tools_v1_enabled_) {
    std::move(callback).Run(BuildErrorResult(
        tool_name, "Browser tools v1 is disabled."));
    return;
  }

  const MahoBrowserToolRegistry::ToolSchema* schema =
      MahoBrowserToolRegistry::FindToolSchema(tool_name);
  if (!schema) {
    std::move(callback).Run(BuildErrorResult(
        tool_name, "Unknown browser tool: " + tool_name));
    return;
  }

  if (schema->browser_action_contract) {
    ExecuteBrowserAction(*schema->browser_action_contract, arguments,
                         std::move(callback));
    return;
  }

  if (tool_name == "read_current_page") {
    ExecuteReadCurrentPage(std::move(callback));
    return;
  }
  if (tool_name == "get_selected_text") {
    ExecuteGetSelectedText(std::move(callback));
    return;
  }
  if (tool_name == "get_active_tab") {
    ExecuteGetActiveTab(std::move(callback));
    return;
  }
  if (tool_name == "search_in_page") {
    ExecuteSearchInPage(arguments, std::move(callback));
    return;
  }
  if (tool_name == "extract_structured_page_context") {
    ExecuteExtractStructuredPageContext(std::move(callback));
    return;
  }
  if (base::StartsWith(tool_name, "mail_")) {
    ExecuteMailTool(tool_name, arguments, std::move(callback));
    return;
  }

  std::move(callback).Run(
      BuildErrorResult(tool_name, "Browser tool is registered but not implemented."));
}

void MahoBrowserToolExecutor::ExecuteReadCurrentPage(ToolResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_context_extractor_->GetPageContext(base::BindOnce(
      &MahoBrowserToolExecutor::OnReadCurrentPageContext,
      weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoBrowserToolExecutor::ExecuteGetSelectedText(ToolResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_context_extractor_->GetPageContext(base::BindOnce(
      &MahoBrowserToolExecutor::OnSelectedTextFromContext,
      weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoBrowserToolExecutor::ExecuteGetActiveTab(ToolResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(BuildActiveTabResult(browser_));
}

void MahoBrowserToolExecutor::ExecuteSearchInPage(
    const base::DictValue& arguments,
    ToolResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const std::string query = GetStringArgument(arguments, "query");
  if (query.empty()) {
    std::move(callback).Run(
        BuildErrorResult("search_in_page", "Missing required argument: query"));
    return;
  }

  const int max_results = GetIntArgument(arguments, "max_results", kDefaultSearchResultLimit);
  page_context_extractor_->GetPageContext(base::BindOnce(
      &MahoBrowserToolExecutor::OnSearchInPageContext,
      weak_factory_.GetWeakPtr(), std::move(callback), query, max_results));
}

void MahoBrowserToolExecutor::ExecuteExtractStructuredPageContext(
    ToolResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_context_extractor_->GetPageContext(base::BindOnce(
      &MahoBrowserToolExecutor::OnExtractStructuredPageContext,
      weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoBrowserToolExecutor::ExecuteMailTool(
    const std::string& tool_name,
    const base::DictValue& arguments,
    ToolResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::MahoMcpBrowserDelegate* delegate = GetBrowserActionDelegate();
  if (!delegate) {
    std::move(callback).Run(
        BuildErrorResult(tool_name, "Mail broker unavailable."));
    return;
  }

  auto completion = base::BindOnce(
      &MahoBrowserToolExecutor::OnMailToolResult,
      weak_factory_.GetWeakPtr(), tool_name, std::move(callback));
  if (tool_name == "mail_list_accounts") {
    delegate->MailListAccounts(std::move(completion));
    return;
  }

  const std::string account_id = GetStringArgument(arguments, "account_id");
  if (tool_name == "mail_list_folders") {
    delegate->MailListFolders(account_id, std::move(completion));
    return;
  }
  if (tool_name == "mail_list_emails") {
    delegate->MailListEmails(
        account_id, GetStringArgument(arguments, "folder_id"),
        GetIntArgument(arguments, "limit", 50),
        GetIntArgument(arguments, "offset", 0), std::move(completion));
    return;
  }
  if (tool_name == "mail_get_email") {
    delegate->MailGetEmail(GetStringArgument(arguments, "email_id"),
                           std::move(completion));
    return;
  }
  if (tool_name == "mail_search_emails") {
    delegate->MailSearchEmails(GetStringArgument(arguments, "query_json"),
                               std::move(completion));
    return;
  }
  if (tool_name == "mail_list_thread") {
    delegate->MailListThread(account_id,
                             GetStringArgument(arguments, "message_id"),
                             std::move(completion));
    return;
  }
  std::move(callback).Run(
      BuildErrorResult(tool_name, "Mail tool is not implemented."));
}

void MahoBrowserToolExecutor::OnMailToolResult(
    const std::string& tool_name,
    ToolResultCallback callback,
    bool ok,
    std::string result_json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!ok) {
    std::move(callback).Run(BuildErrorResult(
        tool_name, result_json.empty() ? "Mail broker request failed."
                                       : result_json));
    return;
  }
  base::DictValue result;
  result.Set("ok", true);
  result.Set("tool", tool_name);
  std::optional<base::Value> parsed = base::JSONReader::Read(result_json, 0);
  if (!parsed) {
    std::move(callback).Run(
        BuildErrorResult(tool_name, "Mail broker returned invalid JSON."));
    return;
  }
  result.Set("result", std::move(*parsed));
  std::move(callback).Run(std::move(result));
}

void MahoBrowserToolExecutor::ExecuteBrowserAction(
    const maho::ai::BrowserActionContract& contract,
    const base::DictValue& arguments,
    ToolResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  maho::MahoMcpBrowserDelegate* delegate = GetBrowserActionDelegate();
  if (!delegate) {
    std::move(callback).Run(BuildBrowserActionErrorResult(
        std::string(contract.tool_name), kActionFailedError,
        "Browser action delegate is unavailable."));
    return;
  }

  std::string resolve_error;
  std::optional<int> maybe_tab_id =
      ResolveBrowserActionTabId(delegate, arguments, resolve_error);
  if (!maybe_tab_id.has_value()) {
    std::move(callback).Run(BuildBrowserActionErrorResult(
        std::string(contract.tool_name), kActionFailedError, resolve_error));
    return;
  }

  BrowserActionFallbackDecision fallback_decision;
  std::string fallback_error;
  if (!EnforceBrowserActionFallback(contract, arguments, fallback_decision,
                                    fallback_error)) {
    base::DictValue result = BuildBrowserActionErrorResult(
        std::string(contract.tool_name), kActionDeniedError, fallback_error);
    AttachFallbackDecision(result, fallback_decision);
    std::move(callback).Run(std::move(result));
    return;
  }

  std::string authorization_error;
  if (!AuthorizeBrowserAction(contract, arguments, *maybe_tab_id, delegate,
                               authorization_error)) {
    base::DictValue result = BuildBrowserActionErrorResult(
        std::string(contract.tool_name), kActionDeniedError,
        authorization_error);
    AttachFallbackDecision(result, fallback_decision);
    std::move(callback).Run(std::move(result));
    return;
  }

  const std::string tool_name(contract.tool_name);
  const int tab_id = *maybe_tab_id;

  if (tool_name == "browser_navigate") {
    const std::string* url_str = arguments.FindString("url");
    if (!url_str) {
      std::move(callback).Run(BuildBrowserActionErrorResult(
          tool_name, kInvalidParamsError, "Invalid params: url required"));
      return;
    }
    GURL nav_url(*url_str);
    if (!nav_url.is_valid()) {
      std::move(callback).Run(BuildBrowserActionErrorResult(
          tool_name, kInvalidParamsError,
          "Invalid params: url is not a valid GURL"));
      return;
    }
    if (IsBlockedNavigationScheme(nav_url)) {
      std::move(callback).Run(BuildBrowserActionErrorResult(
          tool_name, kActionFailedError,
          "Navigation blocked: scheme not permitted for automated navigation"));
      return;
    }
    if (!delegate->Navigate(tab_id, nav_url)) {
      std::move(callback).Run(BuildBrowserActionErrorResult(
          tool_name, kActionFailedError,
          "Action failed: navigation could not be dispatched"));
      return;
    }
    base::DictValue result;
    result.Set("navigated", true);
    result.Set("url", nav_url.spec());
    base::DictValue success =
        BuildBrowserActionSuccessResult(tool_name, std::move(result));
    AttachFallbackDecision(success, fallback_decision);
    RecordBrowserActionFallbackSuccess(contract, fallback_decision);
    std::move(callback).Run(std::move(success));
    return;
  }

  if (tool_name == "browser_accessibility_snapshot") {
    browser_action_refs_.clear();
    base::Value snapshot =
        delegate->GetAccessibilitySnapshot(tab_id, &browser_action_refs_);
    auto redacted = maho::MahoMcpFirewall::Wrap(std::move(snapshot));
    base::Value value = redacted.get().Clone();
    base::DictValue result;
    if (value.is_dict()) {
      result = std::move(value).TakeDict();
    } else {
      result.Set("snapshot", std::move(value));
    }
    base::DictValue success =
        BuildBrowserActionSuccessResult(tool_name, std::move(result));
    AttachFallbackDecision(success, fallback_decision);
    RecordBrowserActionFallbackSuccess(contract, fallback_decision);
    std::move(callback).Run(std::move(success));
    return;
  }

  if (tool_name == "page_query_selector") {
    const std::string* selector = arguments.FindString("selector");
    if (!selector) {
      std::move(callback).Run(BuildBrowserActionErrorResult(
          tool_name, kInvalidParamsError, "Invalid params: selector required"));
      return;
    }
    maho::MahoMcpBrowserDelegate::QuerySelectorResult query_result =
        delegate->QuerySelector(tab_id, *selector);
    base::DictValue result;
    result.Set("ref_id", query_result.ref_id);
    result.Set("tag", query_result.tag);
    base::DictValue success =
        BuildBrowserActionSuccessResult(tool_name, std::move(result));
    AttachFallbackDecision(success, fallback_decision);
    RecordBrowserActionFallbackSuccess(contract, fallback_decision);
    std::move(callback).Run(std::move(success));
    return;
  }

  if (tool_name == "page_get_text") {
    const std::string* ref_id = arguments.FindString("ref_id");
    if (!ref_id) {
      std::move(callback).Run(BuildBrowserActionErrorResult(
          tool_name, kInvalidParamsError, "Invalid params: ref_id required"));
      return;
    }
    base::DictValue result;
    result.Set("text", delegate->GetElementText(tab_id, *ref_id));
    base::DictValue success =
        BuildBrowserActionSuccessResult(tool_name, std::move(result));
    AttachFallbackDecision(success, fallback_decision);
    RecordBrowserActionFallbackSuccess(contract, fallback_decision);
    std::move(callback).Run(std::move(success));
    return;
  }

  if (tool_name == "page_get_attribute") {
    const std::string* ref_id = arguments.FindString("ref_id");
    const std::string* attribute = arguments.FindString("attribute");
    if (!ref_id || !attribute) {
      std::move(callback).Run(BuildBrowserActionErrorResult(
          tool_name, kInvalidParamsError,
          "Invalid params: ref_id and attribute required"));
      return;
    }
    base::DictValue result;
    result.Set("value",
               delegate->GetElementAttribute(tab_id, *ref_id, *attribute));
    base::DictValue success =
        BuildBrowserActionSuccessResult(tool_name, std::move(result));
    AttachFallbackDecision(success, fallback_decision);
    RecordBrowserActionFallbackSuccess(contract, fallback_decision);
    std::move(callback).Run(std::move(success));
    return;
  }

  if (tool_name == "page_wait_for_selector") {
    const std::string* selector = arguments.FindString("selector");
    if (!selector) {
      std::move(callback).Run(BuildBrowserActionErrorResult(
          tool_name, kInvalidParamsError, "Invalid params: selector required"));
      return;
    }
    const int timeout_ms = GetIntArgument(arguments, "timeout_ms", 10000);
    base::DictValue result;
    result.Set("found", delegate->WaitForSelector(tab_id, *selector, timeout_ms));
    base::DictValue success =
        BuildBrowserActionSuccessResult(tool_name, std::move(result));
    AttachFallbackDecision(success, fallback_decision);
    RecordBrowserActionFallbackSuccess(contract, fallback_decision);
    std::move(callback).Run(std::move(success));
    return;
  }

  if (tool_name == "browser_wait_for_navigation") {
    int64_t since_ms = 0;
    if (std::optional<double> since_timestamp =
            arguments.FindDouble("since_timestamp_ms")) {
      since_ms = static_cast<int64_t>(*since_timestamp);
    } else if (std::optional<int> since_timestamp_int =
                   arguments.FindInt("since_timestamp_ms")) {
      since_ms = static_cast<int64_t>(*since_timestamp_int);
    }
    if (since_ms == 0) {
      since_ms = base::Time::Now().InMillisecondsSinceUnixEpoch();
    }

    base::DictValue result;
    std::vector<maho::MahoMcpSession::NavigationEvent> events =
        delegate->GetNavigationEvents(tab_id, since_ms);
    if (!events.empty()) {
      const maho::MahoMcpSession::NavigationEvent& event = events.back();
      result.Set("navigated", true);
      result.Set("url", event.url);
      result.Set("title", event.title);
      result.Set("status_code", event.status_code);
      result.Set("timestamp_ms", static_cast<double>(event.timestamp_ms));
    } else {
      result.Set("navigated", false);
      result.Set("timeout", true);
    }
    std::move(callback).Run(
        BuildBrowserActionSuccessResult(tool_name, std::move(result)));
    return;
  }

  maho::MahoMcpBrowserActionHandler::Result action_result;
  if (tool_name == "browser_click") {
    action_result = maho::MahoMcpBrowserActionHandler::HandleClick(
        delegate, browser_action_refs_, tab_id, &arguments);
  } else if (tool_name == "browser_type") {
    action_result = maho::MahoMcpBrowserActionHandler::HandleType(
        delegate, browser_action_refs_, tab_id, &arguments);
  } else if (tool_name == "browser_select") {
    action_result = maho::MahoMcpBrowserActionHandler::HandleSelect(
        delegate, browser_action_refs_, tab_id, &arguments);
  } else if (tool_name == "browser_scroll") {
    action_result = maho::MahoMcpBrowserActionHandler::HandleScroll(
        delegate, browser_action_refs_, browser_action_scroll_positions_, tab_id,
        &arguments);
  } else if (tool_name == "browser_hover") {
    action_result = maho::MahoMcpBrowserActionHandler::HandleHover(
        delegate, browser_action_refs_, tab_id, &arguments);
  } else if (tool_name == "browser_key_press") {
    action_result = maho::MahoMcpBrowserActionHandler::HandleKeyPress(
        delegate, tab_id, &arguments);
  } else {
    std::move(callback).Run(BuildBrowserActionErrorResult(
        tool_name, kActionFailedError,
        "Browser action is registered but not implemented."));
    return;
  }

  if (action_result.error.has_value()) {
    base::DictValue result = BuildBrowserActionErrorResult(
        tool_name, action_result.error->code, action_result.error->message);
    AttachFallbackDecision(result, fallback_decision);
    std::move(callback).Run(std::move(result));
    return;
  }

  base::DictValue success =
      BuildBrowserActionSuccessResult(tool_name, std::move(action_result.value));
  AttachFallbackDecision(success, fallback_decision);
  RecordBrowserActionFallbackSuccess(contract, fallback_decision);
  std::move(callback).Run(std::move(success));
}

void MahoBrowserToolExecutor::OnReadCurrentPageContext(
    ToolResultCallback callback,
    MahoAiPageContextExtractor::PageContextResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(BuildReadCurrentPageResult(result));
}

void MahoBrowserToolExecutor::OnSearchInPageContext(
    ToolResultCallback callback,
    std::string query,
    int max_results,
    MahoAiPageContextExtractor::PageContextResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!HasUsablePageContext(result)) {
    std::move(callback).Run(
        BuildErrorResult("search_in_page", "Unable to read page for search."));
    return;
  }
  std::move(callback).Run(
      BuildSearchResult(query, result.main_text, static_cast<size_t>(max_results)));
}

void MahoBrowserToolExecutor::OnExtractStructuredPageContext(
    ToolResultCallback callback,
    MahoAiPageContextExtractor::PageContextResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  base::DictValue output;
  output.Set("tool", "extract_structured_page_context");
  output.Set("context_scope", "active_tab");
  output.Set("ok", HasUsablePageContext(result));
  output.Set("page_status", ToolStatusToString(result.extraction_status));
  if (!HasUsablePageContext(result)) {
    output.Set("error", "Unable to extract structured page context.");
  } else {
    base::Value data = result.ToValue();
    output.Set("data", std::move(data));
  }
  std::move(callback).Run(std::move(output));
}

void MahoBrowserToolExecutor::OnSelectedTextFromContext(
    ToolResultCallback callback,
    MahoAiPageContextExtractor::PageContextResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  base::DictValue output;
  output.Set("tool", "get_selected_text");
  output.Set("context_scope", "active_tab");
  output.Set("ok", HasUsablePageContext(result));
  output.Set("page_status", ToolStatusToString(result.extraction_status));
  if (!HasUsablePageContext(result)) {
    output.Set("error", "Unable to read the selected text.");
  } else {
    output.Set("selected_text", result.selected_text);
  }
  std::move(callback).Run(std::move(output));
}

// static
base::DictValue MahoBrowserToolExecutor::BuildActiveTabResult(Browser* browser) {
  base::DictValue result;
  result.Set("tool", "get_active_tab");
  result.Set("context_scope", "active_tab");

  if (!browser || !browser->tab_strip_model()) {
    result.Set("ok", false);
    result.Set("error", "No active browser tab is available.");
    return result;
  }

  content::WebContents* web_contents = browser->tab_strip_model()->GetActiveWebContents();
  if (!web_contents) {
    result.Set("ok", false);
    result.Set("error", "No active browser tab is available.");
    return result;
  }

  result.Set("ok", true);
  result.Set("title", base::UTF16ToUTF8(web_contents->GetTitle()));
  result.Set("url", web_contents->GetVisibleURL().spec());
  result.Set("active_index", browser->tab_strip_model()->active_index());
  result.Set("tab_count", browser->tab_strip_model()->count());
  return result;
}

// static
base::DictValue MahoBrowserToolExecutor::BuildReadCurrentPageResult(
    const MahoAiPageContextExtractor::PageContextResult& result) {
  base::DictValue output;
  output.Set("tool", "read_current_page");
  output.Set("context_scope", "active_tab");
  output.Set("ok", HasUsablePageContext(result));
  output.Set("page_status", ToolStatusToString(result.extraction_status));
  if (!HasUsablePageContext(result)) {
    output.Set("error", "Unable to read the current page.");
  } else {
    output.Set("title", result.title);
    output.Set("url", result.url);
    output.Set("text", result.main_text);
    output.Set("selected_text", result.selected_text);
  }
  return output;
}

// static
base::DictValue MahoBrowserToolExecutor::BuildSearchResult(
    const std::string& query,
    const std::string& page_text,
    size_t max_results) {
  base::DictValue output;
  output.Set("ok", true);
  output.Set("tool", "search_in_page");
  output.Set("context_scope", "active_tab");
  output.Set("query", query);

  base::ListValue matches;
  std::string lowered_text = base::ToLowerASCII(page_text);
  std::string lowered_query = base::ToLowerASCII(query);
  size_t cursor = 0;
  while (matches.size() < max_results) {
    const size_t found = lowered_text.find(lowered_query, cursor);
    if (found == std::string::npos) {
      break;
    }

    const size_t snippet_start = found > kSnippetRadius ? found - kSnippetRadius : 0;
    const size_t snippet_end = std::min(page_text.size(),
                                        found + query.size() + kSnippetRadius);
    base::DictValue match;
    match.Set("index", static_cast<int>(found));
    match.Set("snippet", page_text.substr(snippet_start, snippet_end - snippet_start));
    matches.Append(std::move(match));

    cursor = found + std::max<size_t>(1, query.size());
  }

  output.Set("matches", std::move(matches));
  output.Set("match_count", static_cast<int>(matches.size()));
  return output;
}

// static
std::string MahoBrowserToolExecutor::GetStringArgument(
    const base::DictValue& arguments,
    const std::string& key) {
  const std::string* value = arguments.FindString(key);
  return value ? *value : std::string();
}

// static
int MahoBrowserToolExecutor::GetIntArgument(const base::DictValue& arguments,
                                            const std::string& key,
                                            int default_value) {
  std::optional<int> value = arguments.FindInt(key);
  return value.value_or(default_value);
}

base::DictValue MahoBrowserToolExecutor::BuildBrowserActionErrorResult(
    const std::string& tool_name,
    int error_code,
    const std::string& error) {
  base::DictValue result = BuildErrorResult(tool_name, error);
  result.Set("error_code", error_code);
  return result;
}

base::DictValue MahoBrowserToolExecutor::BuildBrowserActionSuccessResult(
    const std::string& tool_name,
    base::DictValue action_result) {
  action_result.Set("ok", true);
  action_result.Set("tool", tool_name);
  action_result.Set("context_scope", "active_tab");
  return action_result;
}

const char* MahoBrowserToolExecutor::FallbackTierName(FallbackTier tier) {
  switch (tier) {
    case FallbackTier::kTypedDomainApi:
      return "typed_domain_api";
    case FallbackTier::kDomRefLocator:
      return "dom_ref_locator";
    case FallbackTier::kAccessibilitySnapshot:
      return "accessibility_snapshot";
    case FallbackTier::kScreenshotCua:
      return "screenshot_cua";
  }
  return "typed_domain_api";
}

const char* MahoBrowserToolExecutor::FallbackSafetyStateName(
    FallbackSafetyState state) {
  switch (state) {
    case FallbackSafetyState::kSafe:
      return "safe";
    case FallbackSafetyState::kDowngraded:
      return "downgraded";
    case FallbackSafetyState::kBlocked:
      return "blocked";
    case FallbackSafetyState::kEscalationApprovalRequired:
      return "escalation_approval_required";
    case FallbackSafetyState::kEscalationApproved:
      return "escalation_approved";
  }
  return "blocked";
}

void MahoBrowserToolExecutor::AttachFallbackDecision(
    base::DictValue& result,
    const BrowserActionFallbackDecision& decision) {
  base::DictValue fallback;
  fallback.Set("attempted_tier", FallbackTierName(decision.attempted_tier));
  fallback.Set("selected_tier", FallbackTierName(decision.selected_tier));
  fallback.Set("reason", decision.reason);
  fallback.Set("safety_state", FallbackSafetyStateName(decision.safety_state));
  fallback.Set("approval_required", decision.approval_required);
  result.Set("fallback_decision", std::move(fallback));
}

bool MahoBrowserToolExecutor::EnforceBrowserActionFallback(
    const maho::ai::BrowserActionContract& contract,
    const base::DictValue& arguments,
    BrowserActionFallbackDecision& decision,
    std::string& error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string tool_name(contract.tool_name);
  const FallbackTier requested_tier = InferFallbackTier(tool_name, arguments);
  const std::string action_class = FallbackActionClassForToolName(tool_name);
  BrowserActionFallbackProgress& progress =
      browser_action_fallback_progress_[action_class];
  decision.selected_tier = requested_tier;
  decision.approval_required = maho::ai::IsSensitive(contract);

  if (progress.typed_domain_succeeded &&
      requested_tier != FallbackTier::kTypedDomainApi) {
    decision.attempted_tier = requested_tier;
    decision.selected_tier = FallbackTier::kTypedDomainApi;
    decision.reason = "typed_domain_already_succeeded";
    decision.safety_state = FallbackSafetyState::kBlocked;
    error = "Fallback ladder blocked downgrade after typed/domain success.";
    return false;
  }

  switch (requested_tier) {
    case FallbackTier::kTypedDomainApi:
      decision.attempted_tier = FallbackTier::kTypedDomainApi;
      decision.reason = "typed_domain_available";
      decision.safety_state = FallbackSafetyState::kSafe;
      return true;
    case FallbackTier::kDomRefLocator:
      decision.attempted_tier = FallbackTier::kTypedDomainApi;
      decision.reason = progress.dom_ref_selected ? "dom_ref_selected"
                                                   : "typed_domain_unavailable";
      decision.safety_state = progress.dom_ref_selected
                                  ? FallbackSafetyState::kSafe
                                  : FallbackSafetyState::kDowngraded;
      return true;
    case FallbackTier::kAccessibilitySnapshot:
      decision.attempted_tier = FallbackTier::kDomRefLocator;
      decision.reason = progress.dom_ref_selected ? "dom_ref_missing"
                                                  : "accessibility_selected";
      decision.safety_state = FallbackSafetyState::kDowngraded;
      if (contract.kind == maho::ai::BrowserActionKind::kAction &&
          !progress.accessibility_snapshot_selected) {
        decision.reason = "accessibility_snapshot_required";
        decision.safety_state = FallbackSafetyState::kBlocked;
        error = "Fallback ladder requires browser_accessibility_snapshot before ref action.";
        return false;
      }
      return true;
    case FallbackTier::kScreenshotCua:
      decision.attempted_tier = FallbackTier::kAccessibilitySnapshot;
      decision.reason = ScreenshotCuaApproved(arguments)
                            ? "screenshot_cua_approved"
                            : "screenshot_cua_approval_required";
      decision.safety_state = ScreenshotCuaApproved(arguments)
                                  ? FallbackSafetyState::kEscalationApproved
                                  : FallbackSafetyState::kEscalationApprovalRequired;
      if (!ScreenshotCuaApproved(arguments)) {
        error = "Screenshot/CUA fallback requires explicit approval.";
        return false;
      }
      return true;
  }
  error = "Fallback ladder state is invalid.";
  return false;
}

void MahoBrowserToolExecutor::RecordBrowserActionFallbackSuccess(
    const maho::ai::BrowserActionContract& contract,
    const BrowserActionFallbackDecision& decision) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string tool_name(contract.tool_name);
  BrowserActionFallbackProgress& progress =
      browser_action_fallback_progress_[FallbackActionClassForToolName(tool_name)];
  switch (decision.selected_tier) {
    case FallbackTier::kTypedDomainApi:
      progress.typed_domain_succeeded = true;
      break;
    case FallbackTier::kDomRefLocator:
      progress.dom_ref_selected = true;
      browser_action_fallback_progress_["element_action"].dom_ref_selected = true;
      break;
    case FallbackTier::kAccessibilitySnapshot:
      progress.accessibility_snapshot_selected = true;
      browser_action_fallback_progress_["element_action"]
          .accessibility_snapshot_selected = true;
      break;
    case FallbackTier::kScreenshotCua:
      break;
  }
}

content::WebContents* MahoBrowserToolExecutor::GetActiveWebContents() const {
  if (!browser_ || !browser_->tab_strip_model()) {
    return nullptr;
  }
  return browser_->tab_strip_model()->GetActiveWebContents();
}

maho::MahoMcpBrowserDelegate*
MahoBrowserToolExecutor::GetBrowserActionDelegate() const {
  if (browser_action_delegate_for_testing_) {
    return browser_action_delegate_for_testing_;
  }
  return maho::MahoMcpSession::GetBrowserDelegateForBrowserActions();
}

std::optional<int> MahoBrowserToolExecutor::ResolveBrowserActionTabId(
    maho::MahoMcpBrowserDelegate* delegate,
    const base::DictValue& arguments,
    std::string& error) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const int requested_tab_id = arguments.FindInt("tab_id").value_or(0);
  maho::MahoMcpTargetResolution resolution =
      delegate->ResolveTabTarget(requested_tab_id);
  switch (resolution.error) {
    case maho::MahoMcpTargetError::kNone:
      if (resolution.target.valid) {
        return resolution.target.tab_id;
      }
      error = "No eligible active tab";
      return std::nullopt;
    case maho::MahoMcpTargetError::kTabNotFound:
      error = "Tab not found";
      return std::nullopt;
    case maho::MahoMcpTargetError::kNoEligibleActiveTab:
      error = "No eligible active tab";
      return std::nullopt;
    case maho::MahoMcpTargetError::kNoEligibleActiveBrowser:
      error = "No eligible active browser";
      return std::nullopt;
  }
  error = "No eligible active tab";
  return std::nullopt;
}

std::optional<std::string> MahoBrowserToolExecutor::GetActiveTabOrigin(
    maho::MahoMcpBrowserDelegate* delegate,
    int tab_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (const maho::MahoMcpSession::TabInfo& tab : delegate->GetTabList()) {
    if (tab.id == tab_id || (tab_id == 0 && tab.is_active)) {
      return OriginString(GURL(tab.url));
    }
  }
  return std::nullopt;
}

bool MahoBrowserToolExecutor::AuthorizeBrowserAction(
    const maho::ai::BrowserActionContract& contract,
    const base::DictValue& arguments,
    int tab_id,
    maho::MahoMcpBrowserDelegate* delegate,
    std::string& error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::optional<std::string> active_origin = GetActiveTabOrigin(delegate, tab_id);
  if (!active_origin.has_value()) {
    error = "Active tab origin is unavailable; browser action failed closed.";
    return false;
  }

  std::string destination_origin;
  if (contract.domain_policy ==
      maho::ai::BrowserActionDomainPolicy::kActiveTabOriginOrApprovedDestination) {
    const std::string* url_str = arguments.FindString("url");
    if (!url_str) {
      error = "Invalid params: url required";
      return false;
    }
    GURL destination_url(*url_str);
    std::optional<std::string> maybe_destination = OriginString(destination_url);
    if (!maybe_destination.has_value() ||
        IsBlockedNavigationScheme(destination_url)) {
      error = "Navigation blocked: destination origin is not permitted.";
      return false;
    }
    destination_origin = *maybe_destination;
  }

  if (maho::ai::RequiresApproval(contract)) {
    if (!action_approval_callback_) {
      error = "Browser action requires typed approval authorization.";
      return false;
    }

    BrowserActionAuthorization metadata;
    metadata.tool_name = std::string(contract.tool_name);
    metadata.kind = std::string(maho::ai::BrowserActionKindName(contract.kind));
    metadata.sensitivity = std::string(
        maho::ai::BrowserActionSensitivityName(contract.sensitivity));
    metadata.requires_approval = true;
    metadata.requires_lease = maho::ai::RequiresLease(contract);
    metadata.domain_policy = std::string(
        maho::ai::BrowserActionDomainPolicyName(contract.domain_policy));
    metadata.empty_allowlist_policy = std::string(
        maho::ai::BrowserActionEmptyAllowlistPolicyName(
            contract.empty_allowlist_policy));
    metadata.active_origin = *active_origin;
    metadata.destination_origin = std::move(destination_origin);
    metadata.page_derived_justification =
        arguments.FindBool("page_derived_justification")
            .value_or(
                arguments.FindBool("pageDerivedJustification").value_or(false));

    BrowserActionApprovalDecision decision =
        action_approval_callback_.Run(metadata);
    if (!decision.approved) {
      error = decision.error.empty()
                  ? "Browser action approval was denied."
                  : std::move(decision.error);
      return false;
    }
  }

  if (!EnsureBrowserActionLease(contract, tab_id, error)) {
    return false;
  }
  return true;
}

bool MahoBrowserToolExecutor::EnsureBrowserActionLease(
    const maho::ai::BrowserActionContract& contract,
    int tab_id,
    std::string& error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::ai::RequiresLease(contract)) {
    return true;
  }

  maho::MahoMcpLeaseRegistry* lease_registry =
      browser_action_lease_registry_for_testing_
          ? browser_action_lease_registry_for_testing_.get()
          : maho::MahoMcpSession::GetLeaseRegistryForBrowserActions();
  if (!lease_registry || browser_action_lease_holder_id_.empty()) {
    error = "Browser action requires an active-tab lease registry.";
    return false;
  }

  if (lease_registry->IsHeldBy(tab_id, browser_action_lease_holder_id_)) {
    return true;
  }

  maho::MahoMcpLeaseRegistry::AcquireResult lease = lease_registry->Acquire(
      tab_id, browser_action_lease_holder_id_, base::Seconds(60),
      false);
  if (!lease.ok) {
    error = "Active tab lease required for browser action";
    if (!lease.previous_holder.empty()) {
      error += "; held by " + lease.previous_holder;
    }
    return false;
  }
  return lease_registry->IsHeldBy(tab_id, browser_action_lease_holder_id_);
}
