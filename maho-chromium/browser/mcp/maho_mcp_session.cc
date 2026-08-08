// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_session.h"

#include "maho/browser/mcp/maho_mcp_capability_registry.h"

#include "build/build_config.h"

#if !BUILDFLAG(IS_WIN)
#include <unistd.h>
#endif

#include <string>
#include <vector>

#include "base/base64.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "base/values.h"
#include "maho/browser/mcp/maho_mcp_accessibility_handler.h"
#include "maho/browser/mcp/maho_mcp_browser_action_handler.h"
#include "maho/browser/mcp/maho_mcp_firewall.h"
#include "maho/browser/mcp/maho_mcp_lease_registry.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/timer/timer.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_WIN)
#include "maho/browser/mcp/maho_mcp_session_token_win.h"
#endif

namespace maho {

MahoMcpBrowserActionHandler::Error::Error() = default;
MahoMcpBrowserActionHandler::Error::Error(int code, std::string message)
    : code(code), message(std::move(message)) {}
MahoMcpBrowserActionHandler::Error::Error(const Error&) = default;
MahoMcpBrowserActionHandler::Error& MahoMcpBrowserActionHandler::Error::operator=(
    const Error&) = default;
MahoMcpBrowserActionHandler::Error::Error(Error&&) noexcept = default;
MahoMcpBrowserActionHandler::Error& MahoMcpBrowserActionHandler::Error::operator=(
    Error&&) noexcept = default;
MahoMcpBrowserActionHandler::Error::~Error() = default;

MahoMcpBrowserActionHandler::Result::Result() = default;
MahoMcpBrowserActionHandler::Result::Result(Result&&) noexcept = default;
MahoMcpBrowserActionHandler::Result&
MahoMcpBrowserActionHandler::Result::operator=(Result&&) noexcept = default;
MahoMcpBrowserActionHandler::Result::~Result() = default;

MahoMcpBrowserDelegate::SameOriginFetchResult::SameOriginFetchResult() =
    default;
MahoMcpBrowserDelegate::SameOriginFetchResult::SameOriginFetchResult(
    const SameOriginFetchResult&) = default;
MahoMcpBrowserDelegate::SameOriginFetchResult&
MahoMcpBrowserDelegate::SameOriginFetchResult::operator=(
    const SameOriginFetchResult&) = default;
MahoMcpBrowserDelegate::SameOriginFetchResult::SameOriginFetchResult(
    SameOriginFetchResult&&) noexcept = default;
MahoMcpBrowserDelegate::SameOriginFetchResult&
MahoMcpBrowserDelegate::SameOriginFetchResult::operator=(
    SameOriginFetchResult&&) noexcept = default;
MahoMcpBrowserDelegate::SameOriginFetchResult::~SameOriginFetchResult() =
    default;

namespace {

constexpr char kMcpProtocolVersion[] = "2025-03-26";
constexpr char kServerName[] = "maho-browser";
constexpr char kServerVersion[] = "0.4.1";
constexpr char kMailReadBrokerUnavailable[] = "Mail read broker unavailable";

MahoMcpBrowserDelegate* g_browser_delegate = nullptr;
MahoMcpLeaseRegistry* g_browser_action_lease_registry = nullptr;

std::string BuildJsonRpcSuccessLine(std::optional<base::Value> id,
                                    base::Value result) {
  MahoMcpFirewall::RedactAll(result);
  base::DictValue response;
  response.Set("jsonrpc", "2.0");
  response.Set("id", id.has_value() ? id->Clone() : base::Value());
  response.Set("result", std::move(result));
  std::string out;
  base::JSONWriter::Write(base::Value(std::move(response)), &out);
  out.push_back('\n');
  return out;
}

std::string BuildJsonRpcErrorLine(std::optional<base::Value> id,
                                  int code,
                                  std::string message) {
  base::DictValue error;
  error.Set("code", code);
  error.Set("message", std::move(message));

  base::DictValue response;
  response.Set("jsonrpc", "2.0");
  response.Set("id", id.has_value() ? id->Clone() : base::Value());
  response.Set("error", std::move(error));
  std::string out;
  base::JSONWriter::Write(base::Value(std::move(response)), &out);
  out.push_back('\n');
  return out;
}

std::optional<base::Value> CloneMessageId(
    const std::optional<base::Value>& id) {
  if (!id.has_value()) {
    return std::nullopt;
  }
  return id->Clone();
}

// T8: build the completion for an async mail read. On broker failure it emits a
// -32000; on success it forwards the durable JSON broker payload verbatim as
// MCP text content so the browser side never has to reparse it.
base::OnceCallback<void(bool, std::string)> MakeMailReadCompletion(
    std::optional<base::Value> msg_id,
    MahoMcpSession::DeferredResponseSender sender) {
  return base::BindOnce(
      [](std::optional<base::Value> id,
         MahoMcpSession::DeferredResponseSender sender, bool ok,
         std::string result_json) {
        if (!ok) {
          sender.Run(BuildJsonRpcErrorLine(
              std::move(id), -32000,
              result_json.empty() ? "Mail read failed"
                                   : std::move(result_json)));
          return;
        }
        base::DictValue text_item;
        text_item.Set("type", "text");
        text_item.Set("text", std::move(result_json));
        base::ListValue content;
        content.Append(std::move(text_item));
        base::DictValue result;
        result.Set("content", std::move(content));
        sender.Run(BuildJsonRpcSuccessLine(std::move(id),
                                           base::Value(std::move(result))));
      },
      std::move(msg_id), std::move(sender));
}

const std::string* FindRequiredStringArgument(
    const base::DictValue* arguments,
    const char* name) {
  if (!arguments) {
    return nullptr;
  }
  const std::string* value = arguments->FindString(name);
  if (!value || value->empty()) {
    return nullptr;
  }
  return value;
}

std::string RequiredStringArgumentMessage(const char* name) {
  return std::string("Invalid params: ") + name + " required";
}

bool ReadOptionalNonNegativeIntArgument(const base::DictValue* arguments,
                                        const char* name,
                                        int default_value,
                                        int* out,
                                        std::string* error_message) {
  const base::Value* raw = arguments ? arguments->Find(name) : nullptr;
  if (!raw) {
    *out = default_value;
    return true;
  }
  std::optional<int> value = arguments->FindInt(name);
  if (!value.has_value()) {
    *error_message = std::string("Invalid params: ") + name +
                     " must be a non-negative integer";
    return false;
  }
  if (*value < 0) {
    *error_message = std::string("Invalid params: ") + name +
                     " must be a non-negative integer";
    return false;
  }
  *out = *value;
  return true;
}

bool AddOptionalStringArgument(const base::DictValue* arguments,
                               const char* name,
                               base::DictValue* out,
                               std::string* error_message) {
  const base::Value* raw = arguments ? arguments->Find(name) : nullptr;
  if (!raw) {
    return true;
  }
  const std::string* value = arguments->FindString(name);
  if (!value) {
    *error_message = std::string("Invalid params: ") + name +
                     " must be a string";
    return false;
  }
  out->Set(name, *value);
  return true;
}

bool BuildMailSearchQueryJson(const base::DictValue* arguments,
                              std::string* query_json,
                              std::string* error_message) {
  const std::string* query = FindRequiredStringArgument(arguments, "query");
  if (!query) {
    *error_message = RequiredStringArgumentMessage("query");
    return false;
  }

  base::DictValue query_object;
  query_object.Set("query", *query);
  if (!AddOptionalStringArgument(arguments, "account_id", &query_object,
                                 error_message)) {
    return false;
  }
  if (!AddOptionalStringArgument(arguments, "folder_id", &query_object,
                                 error_message)) {
    return false;
  }

  int limit = 50;
  int offset = 0;
  if (!ReadOptionalNonNegativeIntArgument(arguments, "limit", 50, &limit,
                                          error_message) ||
      !ReadOptionalNonNegativeIntArgument(arguments, "offset", 0, &offset,
                                          error_message)) {
    return false;
  }
  query_object.Set("limit", limit);
  query_object.Set("offset", offset);

  base::JSONWriter::Write(base::Value(std::move(query_object)), query_json);
  return true;
}

bool TabExists(int tab_id, const std::vector<MahoMcpSession::TabInfo>& fallback_tabs) {
  if (tab_id == 0) return true;
  if (g_browser_delegate) {
    for (const auto& t : g_browser_delegate->GetTabList()) {
      if (t.id == tab_id) return true;
    }
  } else {
    for (const auto& t : fallback_tabs) {
      if (t.id == tab_id) return true;
    }
  }
  return false;
}

// R-8: tools that operate on a specific tab. Their target is resolved once,
// before dispatch, through the private-context boundary so OTR/ineligible
// targets are rejected with a uniform non-revealing error.
bool IsTabTargetTool(std::string_view tool_name) {
  static constexpr std::string_view kTabTargetTools[] = {
      "browser_tab_get",
      "browser_navigate",
      "browser_tab_close",
      "browser_tab_switch",
      "browser_page_content",
      "browser_page_text",
      "browser_page_context",
      "browser_search_in_page",
      "page_query_selector",
      "page_get_text",
      "page_get_attribute",
      "page_wait_for_selector",
      "browser_same_origin_fetch",
      "browser_accessibility_snapshot",
      "browser_click",
      "browser_type",
      "browser_select",
      "browser_console_messages",
      "browser_wait_for_navigation",
      "browser_scroll",
      "browser_hover",
      "browser_key_press",
      "browser_set_viewport_size",
      "browser_screenshot_full",
      "browser_screenshot_element",
      "browser_network_start_capture",
      "browser_network_stop_capture",
      "browser_network_get_har",
      "browser_acquire_lease",
      "browser_heartbeat_lease",
      "browser_release_lease",
      "vault_list_credentials_for_active_page",
      "vault_request_credential_use",
      "vault_fill_credential",
      "vault_fill_totp",
  };
  for (std::string_view name : kTabTargetTools) {
    if (name == tool_name) return true;
  }
  return false;
}

// R-8: tools that create or act on a browser/profile rather than an existing
// tab. They require an eligible regular browser (-32006 otherwise).
bool IsProfileTargetTool(std::string_view tool_name) {
  return tool_name == "browser_tab_new";
}

base::DictValue SerializeTabInfo(const MahoMcpSession::TabInfo& tab_info) {
  base::DictValue tab;
  tab.Set("id", tab_info.id);
  tab.Set("title", tab_info.title);
  tab.Set("url", tab_info.url);
  tab.Set("is_active", tab_info.is_active);
  if (!tab_info.stable_id.empty()) {
    tab.Set("stable_id", tab_info.stable_id);
  }
  tab.Set("targetable", tab_info.targetable);
  tab.Set("tab_strip_index", tab_info.tab_strip_index);
  return tab;
}

// Todo 6: input/navigation mutators that require an active tab lease held by
// this session before any delegate dispatch. Lease-control and read-only tools
// are intentionally excluded so they remain available without a lease.
bool IsLeaseGatedMutation(std::string_view tool_name) {
  return tool_name == "browser_click" || tool_name == "browser_type" ||
         tool_name == "browser_select" || tool_name == "browser_scroll" ||
         tool_name == "browser_key_press" || tool_name == "browser_navigate" ||
         tool_name == "browser_same_origin_fetch" ||
         tool_name == "vault_fill_credential" || tool_name == "vault_fill_totp";
}

bool IsSensitiveReadTool(std::string_view tool_name) {
  return tool_name == "browser_page_content" ||
         tool_name == "browser_page_text" ||
         tool_name == "browser_page_context" ||
         tool_name == "browser_screenshot_full" ||
         tool_name == "browser_screenshot_element" ||
         tool_name == "browser_accessibility_snapshot" ||
         tool_name == "browser_console_messages" ||
         tool_name == "browser_network_get_har" ||
         tool_name == "browser_network_start_capture" ||
         tool_name == "browser_network_stop_capture";
}

int TargetErrorCode(MahoMcpTargetError error) {
  switch (error) {
    case MahoMcpTargetError::kNone:
      return 0;
    case MahoMcpTargetError::kTabNotFound:
      return kMahoMcpErrorTabNotFound;
    case MahoMcpTargetError::kNoEligibleActiveTab:
      return kMahoMcpErrorNoEligibleActiveTab;
    case MahoMcpTargetError::kNoEligibleActiveBrowser:
      return kMahoMcpErrorNoEligibleActiveBrowser;
  }
  return kMahoMcpErrorTabNotFound;
}

const char* TargetErrorMessage(MahoMcpTargetError error) {
  switch (error) {
    case MahoMcpTargetError::kNone:
      return "";
    case MahoMcpTargetError::kTabNotFound:
      return kMahoMcpMessageTabNotFound;
    case MahoMcpTargetError::kNoEligibleActiveTab:
      return kMahoMcpMessageNoEligibleActiveTab;
    case MahoMcpTargetError::kNoEligibleActiveBrowser:
      return kMahoMcpMessageNoEligibleActiveBrowser;
  }
  return kMahoMcpMessageTabNotFound;
}

}  // namespace

MahoMcpNetworkCaptureState::MahoMcpNetworkCaptureState() = default;
MahoMcpNetworkCaptureState::~MahoMcpNetworkCaptureState() = default;

void MahoMcpSession::SetBrowserDelegate(MahoMcpBrowserDelegate* delegate) {
  g_browser_delegate = delegate;
}

MahoMcpBrowserDelegate* MahoMcpSession::GetBrowserDelegateForBrowserActions() {
  return g_browser_delegate;
}

void MahoMcpSession::SetLeaseRegistryForBrowserActions(
    MahoMcpLeaseRegistry* lease_registry) {
  g_browser_action_lease_registry = lease_registry;
}

MahoMcpLeaseRegistry* MahoMcpSession::GetLeaseRegistryForBrowserActions() {
  return g_browser_action_lease_registry;
}

#if BUILDFLAG(IS_WIN)
MahoMcpSession::MahoMcpSession(MahoMcpSessionToken* session_token,
                               MahoMcpLeaseRegistry* lease_registry)
    : session_token_(session_token),
      lease_registry_(lease_registry),
      session_id_(base::Uuid::GenerateRandomV4().AsLowercaseString()),
      capability_registry_(std::make_unique<MahoMcpCapabilityRegistry>()),
      network_capture_state_(
          base::MakeRefCounted<MahoMcpNetworkCaptureState>()) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  TabInfo default_tab;
  default_tab.id = 0;
  default_tab.title = "New Tab";
  default_tab.url = "about:blank";
  default_tab.is_active = true;
  default_tab.targetable = true;
  default_tab.tab_strip_index = 0;
  tabs_.push_back(std::move(default_tab));
  active_tab_id_ = 0;
}
#else
MahoMcpSession::MahoMcpSession(uid_t peer_uid,
                               MahoMcpLeaseRegistry* lease_registry)
    : peer_uid_(peer_uid),
      lease_registry_(lease_registry),
      session_id_(base::Uuid::GenerateRandomV4().AsLowercaseString()),
      capability_registry_(std::make_unique<MahoMcpCapabilityRegistry>()),
      network_capture_state_(
          base::MakeRefCounted<MahoMcpNetworkCaptureState>()) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  TabInfo default_tab;
  default_tab.id = 0;
  default_tab.title = "New Tab";
  default_tab.url = "about:blank";
  default_tab.is_active = true;
  default_tab.targetable = true;
  default_tab.tab_strip_index = 0;
  tabs_.push_back(std::move(default_tab));
  active_tab_id_ = 0;
}
#endif

MahoMcpSession::~MahoMcpSession() {
  if (g_browser_delegate && network_capture_state_ &&
      network_capture_state_->active &&
      !network_capture_state_->capture_id.empty()) {
    g_browser_delegate->CancelNetworkCapture(network_capture_state_->capture_id);
  }
}

MahoMcpSession::ConsoleMessage::ConsoleMessage() = default;
MahoMcpSession::ConsoleMessage::~ConsoleMessage() = default;
MahoMcpSession::ConsoleMessage::ConsoleMessage(const ConsoleMessage&) = default;
MahoMcpSession::ConsoleMessage::ConsoleMessage(ConsoleMessage&&) noexcept = default;
MahoMcpSession::ConsoleMessage& MahoMcpSession::ConsoleMessage::operator=(const ConsoleMessage&) = default;
MahoMcpSession::ConsoleMessage& MahoMcpSession::ConsoleMessage::operator=(ConsoleMessage&&) noexcept = default;

MahoMcpSession::TabInfo::TabInfo() = default;
MahoMcpSession::TabInfo::~TabInfo() = default;
MahoMcpSession::TabInfo::TabInfo(const TabInfo&) = default;
MahoMcpSession::TabInfo::TabInfo(TabInfo&&) noexcept = default;
MahoMcpSession::TabInfo& MahoMcpSession::TabInfo::operator=(const TabInfo&) = default;
MahoMcpSession::TabInfo& MahoMcpSession::TabInfo::operator=(TabInfo&&) noexcept = default;

MahoMcpBrowserDelegate::BookmarkInfo::BookmarkInfo() = default;
MahoMcpBrowserDelegate::BookmarkInfo::~BookmarkInfo() = default;
MahoMcpBrowserDelegate::BookmarkInfo::BookmarkInfo(const BookmarkInfo&) = default;
MahoMcpBrowserDelegate::BookmarkInfo::BookmarkInfo(BookmarkInfo&&) noexcept = default;
MahoMcpBrowserDelegate::BookmarkInfo& MahoMcpBrowserDelegate::BookmarkInfo::operator=(const BookmarkInfo&) = default;
MahoMcpBrowserDelegate::BookmarkInfo& MahoMcpBrowserDelegate::BookmarkInfo::operator=(BookmarkInfo&&) noexcept = default;

void MahoMcpBrowserDelegate::SameOriginFetch(
    int tab_id,
    const GURL& url,
    const std::string& method,
    const std::string& body,
    const std::string& headers_json,
    SameOriginFetchCallback callback) {
  SameOriginFetchResult result;
  result.error = "Same-origin fetch unavailable";
  std::move(callback).Run(std::move(result));
}

void MahoMcpSession::AddConsoleMessageForTesting(int tab_id, ConsoleMessage message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (message.message.size() > 4096) {
    message.message.resize(4096);
  }
  auto& buffer = console_buffers_[tab_id];
  if (buffer.size() >= 500) {
    buffer.erase(buffer.begin());
  }
  buffer.push_back(std::move(message));
}

bool MahoMcpSession::IsHostBlocked(std::string_view host) const {
  if (host.empty() || blocked_domains_.empty()) {
    return false;
  }
  const std::string host_lc = base::ToLowerASCII(host);
  for (const std::string& entry : blocked_domains_) {
    const std::string entry_lc = base::ToLowerASCII(entry);
    // Exact match.
    if (host_lc == entry_lc) {
      return true;
    }
    // Wildcard match: "*.example.com" matches "sub.example.com" and
    // implicitly "example.com" itself.
    static constexpr std::string_view kWildcard = "*.";
    if (base::StartsWith(entry_lc, kWildcard)) {
      const std::string bare = entry_lc.substr(kWildcard.size());
      if (host_lc == bare) {
        return true;
      }
      const std::string suffix = "." + bare;
      if (host_lc.size() > suffix.size() &&
          base::EndsWith(host_lc, suffix)) {
        return true;
      }
    }
  }
  return false;
}

bool MahoMcpSession::HandleAcquireLease(
    const McpJsonRpcMessage& msg,
    const base::DictValue* arguments,
    std::vector<std::string>& responses) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!lease_registry_) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32001, "Lease feature disabled"));
    return true;
  }
  if (!arguments) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32602, "Invalid params: tab_id required"));
    return true;
  }
  std::optional<int> tab_id = arguments->FindInt("tab_id");
  if (!tab_id.has_value()) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32602, "Invalid params: tab_id must be integer"));
    return true;
  }
  const int ttl_seconds = arguments->FindInt("ttl_seconds").value_or(60);
  const bool force_steal = true;
  auto result = lease_registry_->Acquire(
      static_cast<int64_t>(*tab_id), session_id_,
      base::Seconds(ttl_seconds), force_steal);
  if (!result.ok) {
    base::DictValue data;
    data.Set("previous_holder", result.previous_holder);
    data.Set("tab_id", *tab_id);
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32000, result.message));
    return true;
  }
  base::DictValue out;
  out.Set("acquired", true);
  out.Set("session_id", session_id_);
  out.Set("tab_id", *tab_id);
  out.Set("expires_in_seconds",
          static_cast<int>((result.expires_at - base::TimeTicks::Now())
                               .InSeconds()));
  responses.push_back(framer_.BuildSuccessResponse(
      msg.id, base::Value(std::move(out))));
  return true;
}

bool MahoMcpSession::HandleHeartbeatLease(
    const McpJsonRpcMessage& msg,
    const base::DictValue* arguments,
    std::vector<std::string>& responses) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!lease_registry_) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32001, "Lease feature disabled"));
    return true;
  }
  if (!arguments) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32602, "Invalid params: tab_id required"));
    return true;
  }
  std::optional<int> tab_id = arguments->FindInt("tab_id");
  if (!tab_id.has_value()) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32602, "Invalid params: tab_id must be integer"));
    return true;
  }
  const int ttl_seconds = arguments->FindInt("ttl_seconds").value_or(60);
  base::TimeTicks expires = lease_registry_->Heartbeat(
      static_cast<int64_t>(*tab_id), session_id_,
      base::Seconds(ttl_seconds));
  if (expires.is_null()) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32000,
        "Not lease holder or lease expired"));
    return true;
  }
  base::DictValue out;
  out.Set("heartbeat", true);
  out.Set("expires_in_seconds",
          static_cast<int>((expires - base::TimeTicks::Now()).InSeconds()));
  responses.push_back(framer_.BuildSuccessResponse(
      msg.id, base::Value(std::move(out))));
  return true;
}

bool MahoMcpSession::HandleReleaseLease(
    const McpJsonRpcMessage& msg,
    const base::DictValue* arguments,
    std::vector<std::string>& responses) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!lease_registry_) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32001, "Lease feature disabled"));
    return true;
  }
  if (!arguments) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32602, "Invalid params: tab_id required"));
    return true;
  }
  std::optional<int> tab_id = arguments->FindInt("tab_id");
  if (!tab_id.has_value()) {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32602, "Invalid params: tab_id must be integer"));
    return true;
  }
  const bool released =
      lease_registry_->Release(static_cast<int64_t>(*tab_id), session_id_);
  base::DictValue out;
  out.Set("released", released);
  responses.push_back(framer_.BuildSuccessResponse(
      msg.id, base::Value(std::move(out))));
  return true;
}

std::vector<std::string> MahoMcpSession::ProcessData(const std::string& data) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<std::string> responses;

  if (state_ == State::kClosed) {
    return responses;
  }

#if !BUILDFLAG(IS_WIN)
  // Check peer UID on first data (POSIX only; Windows uses DACL).
  if (peer_uid_ != getuid()) {
    LOG(WARNING) << "MCP: Rejecting connection from UID " << peer_uid_
                 << " (browser UID: " << getuid() << ")";
    state_ = State::kClosed;
    return responses;
  }
#endif

  framer_.AppendData(data);

  // Drain any auto-generated error responses (parse error, invalid request).
  while (auto error_resp = framer_.TakeNextResponse()) {
    responses.push_back(std::move(*error_resp));
  }

  // Process parsed messages.
  while (auto msg = framer_.TakeNextParsedMessage()) {
    if (state_ == State::kClosed) {
      break;
    }

    if (state_ == State::kAwaitingInitialize) {
      if (msg->method != "initialize") {
        // Any method other than "initialize" in this state is an error.
        std::string err = framer_.BuildErrorResponse(
            msg->id, -32600, "Invalid Request: initialize expected");
        responses.push_back(std::move(err));
        state_ = State::kClosed;
        break;
      }
      HandleInitialize(*msg, responses);
    } else if (state_ == State::kActive) {
      HandleActiveMethod(*msg, responses);
    }
  }

  return responses;
}

void MahoMcpSession::HandleInitialize(const McpJsonRpcMessage& msg,
                                       std::vector<std::string>& responses) {
  if (!msg.params.has_value() || !msg.params->is_dict()) {
    std::string err =
        framer_.BuildErrorResponse(msg.id, -32602, "Invalid params");
    responses.push_back(std::move(err));
    state_ = State::kClosed;
    return;
  }

  const auto& params = msg.params->GetDict();

  const std::string* protocol_version = params.FindString("protocolVersion");
  if (!protocol_version || *protocol_version != kMcpProtocolVersion) {
    std::string err = framer_.BuildErrorResponse(
        msg.id, -32602, "Invalid params: unsupported protocolVersion");
    responses.push_back(std::move(err));
    state_ = State::kClosed;
    return;
  }

#if BUILDFLAG(IS_WIN)
  // Validate DPAPI session token on Windows.
  if (session_token_) {
    const std::string* auth_token_b64 = params.FindString("authToken");
    if (!auth_token_b64) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32600, "Authentication required: authToken missing");
      responses.push_back(std::move(err));
      state_ = State::kClosed;
      return;
    }

    std::optional<std::vector<uint8_t>> decoded =
        base::Base64Decode(*auth_token_b64);
    if (!decoded.has_value()) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: authToken is not valid base64");
      responses.push_back(std::move(err));
      state_ = State::kClosed;
      return;
    }

    if (!session_token_->Matches(base::span(*decoded))) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32600, "Authentication failed: token mismatch");
      responses.push_back(std::move(err));
      state_ = State::kClosed;
      return;
    }
  }
#endif

  base::DictValue server_info;
  server_info.Set("name", kServerName);
  server_info.Set("version", kServerVersion);

  base::DictValue tools_cap;
  base::DictValue capabilities;
  capabilities.Set("tools", std::move(tools_cap));

  base::DictValue result;
  result.Set("serverInfo", std::move(server_info));
  result.Set("capabilities", std::move(capabilities));
  result.Set("protocolVersion", kMcpProtocolVersion);

  std::string resp =
      framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
  responses.push_back(std::move(resp));
  state_ = State::kActive;
}

void MahoMcpSession::HandleActiveMethod(const McpJsonRpcMessage& msg,
                                         std::vector<std::string>& responses) {
  if (msg.method == "tools/list") {
    base::DictValue result;
    base::ListValue tools;

    // Tab tools (Wave 2.2)
    {
      base::DictValue t;
      t.Set("name", "browser_tab_list");
      t.Set("description", "List all open browser tabs");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_tab_get");
      t.Set("description", "Get details of a specific tab by ID");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_navigate");
      t.Set("description", "Navigate a tab to a URL");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_tab_close");
      t.Set("description", "Close a browser tab by ID");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_tab_new");
      t.Set("description", "Open a new browser tab");
      tools.Append(base::Value(std::move(t)));
    }

    // History + Bookmark tools (Wave 2.3)
    {
      base::DictValue t;
      t.Set("name", "browser_history_search");
      t.Set("description", "Search browser history");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_bookmarks_search");
      t.Set("description", "Search bookmarks");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_bookmark_create");
      t.Set("description", "Create a new bookmark");
      tools.Append(base::Value(std::move(t)));
    }

    // Page content typed extractors (Wave 2.4)
    {
      base::DictValue t;
      t.Set("name", "browser_page_content");
      t.Set("description", "Get full page content with redaction");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_page_text");
      t.Set("description", "Get plain text of the page");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_search_in_page");
      t.Set("description", "Search for text within the page");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_page_context");
      t.Set("description", "Get structured page context");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "page_query_selector");
      t.Set("description", "Query DOM for a CSS selector");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "page_get_text");
      t.Set("description", "Get text content by ref ID");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "page_get_attribute");
      t.Set("description", "Get attribute value by ref ID");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "page_wait_for_selector");
      t.Set("description", "Wait for a selector to appear");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_same_origin_fetch");
      t.Set("description",
            "Fetch an HTTPS URL from the current tab renderer. The URL must "
            "have the exact same origin as the tab. Args: url (string), "
            "method ('GET' or 'POST', optional), body (string, optional), "
            "headers (string-to-string object, optional), "
            "tab_id (int, optional). Response text is capped at 1 MiB.");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_set_blocked_domains");
      t.Set("description",
            "Set the session-scoped disallow list of domains the agent "
            "may not navigate to. Everything else is allowed by default. "
            "Supports wildcards like '*.example.com'.");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_get_blocked_domains");
      t.Set("description",
            "Get the session-scoped disallow list of domains.");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_routines_list");
      t.Set("description",
            "List the routines the browser exposes (built-in recipes + "
            "user-defined custom routines) from the maho_core routines "
            "engine.");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_routines_run");
      t.Set("description",
            "Run a routine by id via the maho_core routines engine. "
            "Args: id (string). Tier-locked routines return a clean error "
            "(Max tier required).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "mail_list_accounts");
      t.Set("description", "List mail accounts available to the mail read broker.");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "mail_list_folders");
      t.Set("description", "List folders for a mail account. Args: account_id (string).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "mail_list_emails");
      t.Set("description",
            "List emails in a folder. Args: account_id (string), "
            "folder_id (string), limit (int, optional, default 50), "
            "offset (int, optional, default 0).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "mail_get_email");
      t.Set("description", "Get a single email by id. Args: email_id (string).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "mail_search_emails");
      t.Set("description",
            "Search emails. Args: query (string), account_id (string, "
            "optional), folder_id (string, optional), limit (int, optional, "
            "default 50), offset (int, optional, default 0).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "mail_list_thread");
      t.Set("description",
            "List a mail thread. Args: account_id (string), message_id (string).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "mail_extract_otp");
      t.Set("description",
            "Temporary OTP extraction placeholder. This build returns a "
            "documented error because OTPs are delivered by the browser "
            "push/arbitration path.");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "mail_add_account");
      t.Set("description", "Connect a new mail account manually. Args: request_json (string).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "mail_test_connection");
      t.Set("description", "Test connection settings for a mail account. Args: params_json (string).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "mail_delete_account");
      t.Set("description", "Delete a mail account by ID. Args: account_id (string).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "mail_start_oauth");
      t.Set("description", "Generate a start URL for OAuth flow. Args: provider (string), client_id (string), redirect_uri (string).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "mail_complete_oauth");
      t.Set("description", "Complete OAuth flow using state and authorization code. Args: state (string), code (string).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "mail_reconnect_account");
      t.Set("description", "Reconnect/refresh an expired session for a mail account. Args: account_id (string).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "mail_import_migration_archive");
      t.Set("description", "Import mail database migration archive. Args: archive_json (string).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "mail_send");
      t.Set("description", "Send an email. Args: request_json (string).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "mail_save_draft");
      t.Set("description", "Save a mail draft. Args: request_json (string).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "mail_update_draft");
      t.Set("description", "Update an existing mail draft. Args: request_json (string).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "mail_queue_email");
      t.Set("description", "Queue an email in outbox to send later. Args: request_json (string).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_acquire_lease");
      t.Set("description",
            "Acquire exclusive lease on a tab. Args: tab_id (int), "
            "ttl_seconds (int, 10-300, default 60). Acquisition always "
            "steals any existing lease (auto-acquire).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_heartbeat_lease");
      t.Set("description",
            "Extend an existing lease. Args: tab_id (int), "
            "ttl_seconds (int, 10-300, default 60). Must be the "
            "current lease holder.");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_release_lease");
      t.Set("description",
            "Release a lease held by this session. Args: tab_id (int).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "vault_list_credentials_for_active_page");
      t.Set("description",
            "List password-vault credential handles for the active page. "
            "Returns opaque handles only; never password material.");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "vault_request_credential_use");
      t.Set("description",
            "Exchange a credential handle for a short-lived fill grant. "
            "Args: handle (string), origin (string).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "vault_fill_credential");
      t.Set("description",
            "Fill a credential field from a short-lived grant. Args: "
            "grant_handle (string), ref (int), tab_id (int, optional). "
            "The secret is injected browser-side and is never returned.");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "vault_fill_totp");
      t.Set("description",
            "Fill a one-time-code field from a short-lived grant. Args: "
            "grant_handle (string), ref (int), tab_id (int, optional). "
            "The code is injected browser-side and is never returned.");
      tools.Append(base::Value(std::move(t)));
    }

    // Screenshot tools (Wave 4.2)
    {
      base::DictValue t;
      t.Set("name", "browser_screenshot_full");
      t.Set("description",
            "Capture full-page screenshot as PNG with password field "
            "redaction. Args: tab_id (int, optional).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_screenshot_element");
      t.Set("description",
            "Capture element screenshot by @ref label as PNG with "
            "password field redaction. Args: ref (int), "
            "tab_id (int, optional).");
      tools.Append(base::Value(std::move(t)));
    }

    // Network capture tools (Wave 4.4)
    {
      base::DictValue t;
      t.Set("name", "browser_network_start_capture");
      t.Set("description",
            "Start capturing network requests for a tab. "
            "Args: tab_id (int, optional).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_network_stop_capture");
      t.Set("description",
            "Stop capturing network requests and return final HAR 1.2. "
            "Args: tab_id (int, optional).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_network_get_har");
      t.Set("description",
            "Get current HAR 1.2 capture without stopping. "
            "Args: tab_id (int, optional).");
      tools.Append(base::Value(std::move(t)));
    }

    // Accessibility tools (Wave 4.1)
    {
      base::DictValue t;
      t.Set("name", "browser_accessibility_snapshot");
      t.Set("description",
            "Get accessibility tree snapshot with @ref labels on "
            "interactive elements. Args: tab_id (int, optional).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_click");
      t.Set("description",
            "Click an element by its @ref label from the accessibility "
            "snapshot. Args: ref (int), tab_id (int, optional).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_type");
      t.Set("description",
            "Type text into an element by its @ref label. "
            "Args: ref (int), text (string), tab_id (int, optional).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_select");
      t.Set("description",
            "Select a value on a <select> element by its @ref label. "
            "Args: ref (int), value (string), tab_id (int, optional).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_console_messages");
      t.Set("description",
            "Retrieve console messages. Args: tab_id (int, optional), "
            "level_filter (list of strings, optional), since_timestamp_ms "
            "(int, optional), limit (int, optional, default 100).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_wait_for_navigation");
      t.Set("description",
            "Wait for navigation to complete or time out. Args: tab_id (int, optional), "
            "timeout_ms (int, optional, default 30000).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_scroll");
      t.Set("description",
            "Scroll the main viewport or an element. Args: tab_id (int, optional), "
            "direction (string: 'up'|'down'|'left'|'right'), pixels (int, optional, default 300), "
            "ref (int, optional).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_hover");
      t.Set("description",
            "Hover mouse over element by ref. Args: ref (int), tab_id (int, optional).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_key_press");
      t.Set("description",
            "Press a keyboard key. Args: tab_id (int, optional), key (string), "
            "modifiers (list of strings, optional).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_tab_switch");
      t.Set("description",
            "Activate a specific tab by ID. Args: tab_id (int).");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_set_viewport_size");
      t.Set("description",
            "Set viewport size. Args: tab_id (int, optional), width_px (int), "
            "height_px (int).");
      tools.Append(base::Value(std::move(t)));
    }

    result.Set("tools", std::move(tools));

    if (msg.id.has_value()) {
      std::string resp =
          framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
      responses.push_back(std::move(resp));
    }
    return;
  }

  if (msg.method == "tools/call") {
    HandleToolCall(msg, responses);
    return;
  }

  // Unknown method.
  if (msg.id.has_value()) {
    std::string err =
        framer_.BuildErrorResponse(msg.id, -32601, "Method not found");
    responses.push_back(std::move(err));
  }
}

void MahoMcpSession::HandleToolCall(const McpJsonRpcMessage& msg,
                                     std::vector<std::string>& responses) {
  if (!msg.id.has_value()) {
    return;
  }

  if (!msg.params.has_value() || !msg.params->is_dict()) {
    std::string err =
        framer_.BuildErrorResponse(msg.id, -32602, "Invalid params");
    responses.push_back(std::move(err));
    return;
  }

  const auto& params = msg.params->GetDict();
  const std::string* tool_name = params.FindString("name");
  if (!tool_name) {
    std::string err = framer_.BuildErrorResponse(
        msg.id, -32602, "Invalid params: missing tool name");
    responses.push_back(std::move(err));
    return;
  }

  // OQ-2 lockdown: explicitly reject evaluate_js.
  if (*tool_name == "evaluate_js") {
    std::string err = framer_.BuildErrorResponse(
        msg.id, -32601, "Method not found: evaluate_js is not available");
    responses.push_back(std::move(err));
    return;
  }

  const base::DictValue* arguments = params.FindDict("arguments");

  // R-8: resolve the tool's target once, before any delegate work, and reject
  // OTR/ineligible targets with a uniform non-revealing error. Never fall back
  // to a cached/zero-active target to rescue a denied context.
  last_resolved_target_ = ResolvedMahoMcpTarget();
  if (g_browser_delegate) {
    MahoMcpTargetResolution resolution;
    bool has_resolution = false;
    if (IsTabTargetTool(*tool_name)) {
      const int requested_tab_id =
          (arguments && arguments->FindInt("tab_id"))
              ? arguments->FindInt("tab_id").value()
              : 0;
      resolution = g_browser_delegate->ResolveTabTarget(requested_tab_id);
      has_resolution = true;
    } else if (IsProfileTargetTool(*tool_name)) {
      resolution = g_browser_delegate->ResolveProfileTarget();
      has_resolution = true;
    }
    if (has_resolution && resolution.error != MahoMcpTargetError::kNone) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, TargetErrorCode(resolution.error),
          TargetErrorMessage(resolution.error)));
      return;
    }
    if (has_resolution) {
      last_resolved_target_ = resolution.target;
    }
  }

  // R-8: once the gate has resolved a tab-target tool against a production
  // delegate, every handler must act on that single stable resolution — never
  // re-resolve an omitted/zero request by passing 0, active_tab_id_, or a raw
  // argument, which could target a different (or OTR) tab after an active
  // switch. Test delegates without a private-context model fall through to the
  // legacy in-memory path below.
  const bool use_resolved_target =
      g_browser_delegate && IsTabTargetTool(*tool_name);
  const int resolved_tab_id =
      use_resolved_target ? last_resolved_target_.tab_id : 0;

  // ----- sandbox ownership gate (B3) -----
  // When the capability registry owns any tabs, every sensitive read must
  // target a sandbox-owned tab. Tab-list metadata remains discoverable.
  if (capability_registry_ && !capability_registry_->ListOwnedTabs().empty() &&
      IsSensitiveReadTool(*tool_name) &&
      !capability_registry_->IsTabOwned(resolved_tab_id)) {
    base::DictValue data;
    data.Set("status", "tab_adoption_required");
    data.Set("tab_id", resolved_tab_id);
    base::DictValue error;
    error.Set("code", -32000);
    error.Set("message", "Tab not owned by this session: adopt it first");
    error.Set("data", std::move(data));
    base::DictValue response;
    response.Set("id", msg.id ? msg.id->Clone() : base::Value());
    response.Set("error", std::move(error));
    std::string out;
    base::JSONWriter::Write(base::Value(std::move(response)), &out);
    out.push_back('\n');
    responses.push_back(std::move(out));
    return;
  }

  // Todo 6: single mutation gate. After stable concrete target resolution and
  // before any delegate dispatch, input/navigation mutators require an active
  // lease held by THIS session on the resolved tab. A session without a lease
  // registry (test wiring) is not gated; lease-control and read-only tools are
  // never classified as mutations, so they remain available.
  if (lease_registry_ && IsLeaseGatedMutation(*tool_name)) {
    const int gate_tab_id =
        use_resolved_target
            ? resolved_tab_id
            : (arguments && arguments->FindInt("tab_id")
                   ? arguments->FindInt("tab_id").value()
                   : active_tab_id_);
    if (!lease_registry_->IsHeldBy(static_cast<int64_t>(gate_tab_id),
                                   session_id_)) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, kMahoMcpErrorLeaseRequired,
          "active tab lease required for this mutation"));
      return;
    }
  }

  auto reject_invalid_mail_params = [&](std::string message) {
    responses.push_back(framer_.BuildErrorResponse(msg.id, -32602, message));
  };
  auto reject_mail_broker_unavailable = [&]() {
    responses.push_back(framer_.BuildErrorResponse(
        msg.id, -32000, kMailReadBrokerUnavailable));
  };
  auto mail_broker_ready = [&]() {
    if (g_browser_delegate && deferred_response_sender_) {
      return true;
    }
    reject_mail_broker_unavailable();
    return false;
  };
  auto make_mail_read_completion = [&]() {
    DeferredResponseSender sender = deferred_response_sender_;
    return MakeMailReadCompletion(CloneMessageId(msg.id), std::move(sender));
  };
  auto append_browser_action_response = [this, &msg, &responses](
                                            MahoMcpBrowserActionHandler::Result
                                                action_result) {
    if (action_result.error.has_value()) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, action_result.error->code, action_result.error->message));
      return;
    }

    auto redacted =
        MahoMcpFirewall::Wrap(base::Value(std::move(action_result.value)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
  };

  if (*tool_name == "ping") {
    base::DictValue result;
    result.Set("pong", true);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_tab_list") {
    base::ListValue list;
    if (g_browser_delegate) {
      for (const auto& t : g_browser_delegate->GetTabList()) {
        list.Append(SerializeTabInfo(t));
      }
    } else {
      for (const auto& t : tabs_) {
        list.Append(SerializeTabInfo(t));
      }
    }
    base::DictValue result;
    result.Set("tabs", std::move(list));
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_tab_get") {
    if (!arguments || !arguments->FindInt("tab_id")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: tab_id required");
      responses.push_back(std::move(err));
      return;
    }
    int target_id = arguments->FindInt("tab_id").value();
    base::DictValue result;
    bool found_real = false;
    if (g_browser_delegate) {
      for (const auto& t : g_browser_delegate->GetTabList()) {
        if (t.id == target_id) {
          result = SerializeTabInfo(t);
          found_real = true;
          break;
        }
      }
    }
    if (!found_real) {
      if (g_browser_delegate) {
        std::string err = framer_.BuildErrorResponse(
            msg.id, kMahoMcpErrorTabNotFound, kMahoMcpMessageTabNotFound);
        responses.push_back(std::move(err));
        return;
      }
      const TabInfo* found = nullptr;
      for (const auto& t : tabs_) {
        if (t.id == target_id) {
          found = &t;
          break;
        }
      }
      if (!found) {
        std::string err = framer_.BuildErrorResponse(
            msg.id, -32000, "Unknown tab_id");
        responses.push_back(std::move(err));
        return;
      }
      result = SerializeTabInfo(*found);
    }
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_navigate") {
    if (!arguments || !arguments->FindString("url")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: url required");
      responses.push_back(std::move(err));
      return;
    }
    // D1: allowed_domains enforcement.
    const std::string& url_str = *arguments->FindString("url");
    GURL nav_url(url_str);
    if (!nav_url.is_valid()) {
      // Agents quote href values verbatim ("/sites", "?page=2"). Resolve
      // relative references against the target tab's current URL instead of
      // rejecting them, matching how a user clicking the link would behave.
      const int base_tab_id =
          use_resolved_target ? resolved_tab_id : active_tab_id_;
      if (g_browser_delegate) {
        for (const auto& t : g_browser_delegate->GetTabList()) {
          if (t.id == base_tab_id) {
            nav_url = GURL(t.url).Resolve(url_str);
            break;
          }
        }
      }
    }
    if (!nav_url.is_valid()) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: url is not a valid GURL");
      responses.push_back(std::move(err));
      return;
    }
    // Reject host-less/dangerous schemes outright. data:, javascript:, file:,
    // chrome: and friends carry no host, so the disallow list can never
    // constrain them; automated (MCP client) navigation must never reach
    // these schemes regardless of the disallow list.
    if (nav_url.SchemeIs("data") || nav_url.SchemeIs("javascript") ||
        nav_url.SchemeIs("file") || nav_url.SchemeIs("chrome") ||
        nav_url.SchemeIs("chrome-untrusted") || nav_url.SchemeIs("blob") ||
        nav_url.SchemeIs("filesystem")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000,
          "Navigation blocked: scheme not permitted for automated navigation");
      responses.push_back(std::move(err));
      return;
    }
    if (IsHostBlocked(nav_url.host())) {
      base::DictValue data;
      data.Set("host", nav_url.host());
      data.Set("status", "domain_blocked");
      base::DictValue error;
      error.Set("code", -32000);
      error.Set("message", "Navigation blocked: host is in the disallow list");
      error.Set("data", std::move(data));
      base::DictValue response;
      response.Set("id", msg.id ? msg.id->Clone() : base::Value());
      response.Set("error", std::move(error));
      std::string out;
      base::JSONWriter::Write(base::Value(std::move(response)), &out);
      out.push_back('\n');
      responses.push_back(std::move(out));
      return;
    }

    if (g_browser_delegate) {
      g_browser_delegate->Navigate(
          use_resolved_target ? resolved_tab_id : active_tab_id_, nav_url);
    }

    // Update active tab URL
    for (auto& t : tabs_) {
      if (t.id == active_tab_id_) {
        t.url = nav_url.spec();
        t.title = nav_url.host();
      }
    }
    base::DictValue result;
    result.Set("navigated", true);
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_set_blocked_domains") {
    if (!arguments) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: domains list required");
      responses.push_back(std::move(err));
      return;
    }
    const base::ListValue* domains = arguments->FindList("domains");
    if (!domains) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: domains must be a list of strings");
      responses.push_back(std::move(err));
      return;
    }
    std::set<std::string> new_set;
    for (const auto& d : *domains) {
      if (!d.is_string()) {
        std::string err = framer_.BuildErrorResponse(
            msg.id, -32602, "Invalid params: domains entries must be strings");
        responses.push_back(std::move(err));
        return;
      }
      new_set.insert(base::ToLowerASCII(d.GetString()));
    }
    blocked_domains_ = std::move(new_set);
    base::DictValue result;
    result.Set("count", static_cast<int>(blocked_domains_.size()));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_get_blocked_domains") {
    base::ListValue domains;
    for (const auto& d : blocked_domains_) {
      domains.Append(d);
    }
    base::DictValue result;
    result.Set("domains", std::move(domains));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  // ----- atomic exact-origin grants (sandbox capability) -----

  if (*tool_name == "browser_grant_exact_origin") {
    if (!arguments || !arguments->FindString("origin")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: origin required");
      responses.push_back(std::move(err));
      return;
    }
    const std::string& origin_str = *arguments->FindString("origin");
    const GURL grant_url(origin_str);
    if (!grant_url.is_valid()) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: url is not a valid GURL");
      responses.push_back(std::move(err));
      return;
    }
    const url::Origin origin = url::Origin::Create(grant_url);
    if (!capability_registry_->IsValidGrantOrigin(origin)) {
      base::DictValue data;
      data.Set("status", "unsupported_origin");
      base::DictValue error;
      error.Set("code", -32000);
      error.Set("message", "Origin not grantable: use an exact http/https origin");
      error.Set("data", std::move(data));
      base::DictValue response;
      response.Set("id", msg.id ? msg.id->Clone() : base::Value());
      response.Set("error", std::move(error));
      std::string out;
      base::JSONWriter::Write(base::Value(std::move(response)), &out);
      out.push_back('\n');
      responses.push_back(std::move(out));
      return;
    }
    bool added = capability_registry_->GrantExactOrigin(origin);
    base::DictValue result;
    result.Set("granted", added);
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_revoke_exact_origin") {
    if (!arguments || !arguments->FindString("origin")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: origin required");
      responses.push_back(std::move(err));
      return;
    }
    const std::string& origin_str = *arguments->FindString("origin");
    const GURL revoke_url(origin_str);
    const url::Origin origin = url::Origin::Create(revoke_url);
    bool removed = capability_registry_->RevokeExactOrigin(origin);
    base::DictValue result;
    result.Set("revoked", removed);
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_list_exact_origins") {
    base::ListValue origins;
    for (const url::Origin& o : capability_registry_->ListGrantedOrigins()) {
      origins.Append(o.Serialize());
    }
    base::DictValue result;
    result.Set("origins", std::move(origins));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  // ----- sandbox tab ownership -----

  if (*tool_name == "browser_adopt_tab") {
    if (!arguments) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: tab_id required");
      responses.push_back(std::move(err));
      return;
    }
    std::optional<int> tab_id = arguments->FindInt("tab_id");
    if (!tab_id.has_value()) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: tab_id must be an integer");
      responses.push_back(std::move(err));
      return;
    }
    auto r = capability_registry_->AdoptTab(*tab_id);
    base::DictValue result;
    result.Set("adopted", r == MahoMcpCapabilityRegistry::AdoptResult::kOk);
    if (r == MahoMcpCapabilityRegistry::AdoptResult::kConflict) {
      result.Set("status", "conflict");
    }
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_release_tab") {
    if (!arguments) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: tab_id required");
      responses.push_back(std::move(err));
      return;
    }
    std::optional<int> tab_id = arguments->FindInt("tab_id");
    if (!tab_id.has_value()) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: tab_id must be an integer");
      responses.push_back(std::move(err));
      return;
    }
    capability_registry_->ReleaseTab(*tab_id);
    base::DictValue result;
    result.Set("released", true);
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_routines_list") {
    base::ListValue routines;
    if (g_browser_delegate) {
      std::optional<base::Value> parsed = base::JSONReader::Read(
          g_browser_delegate->ListRoutines(), base::JSON_PARSE_RFC);
      if (parsed && parsed->is_list()) {
        routines = std::move(*parsed).TakeList();
      }
    }
    base::DictValue result;
    result.Set("routines", std::move(routines));
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_routines_run") {
    if (!arguments || !arguments->FindString("id")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: id required");
      responses.push_back(std::move(err));
      return;
    }
    if (!g_browser_delegate || !deferred_response_sender_) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000, "Routines feature unavailable");
      responses.push_back(std::move(err));
      return;
    }
    const std::string& id = *arguments->FindString("id");
    DeferredResponseSender sender = deferred_response_sender_;
    g_browser_delegate->RunRoutine(
        id,
        base::BindOnce(
            [](std::optional<base::Value> msg_id, DeferredResponseSender sender,
               bool success, std::string content_or_error) {
              if (!success) {
                sender.Run(BuildJsonRpcErrorLine(
                    std::move(msg_id), -32000, std::move(content_or_error)));
                return;
              }
              std::optional<base::Value> parsed = base::JSONReader::Read(
                  content_or_error, base::JSON_PARSE_RFC);
              base::Value result;
              if (parsed) {
                result = std::move(*parsed);
              } else {
                base::DictValue payload;
                payload.Set("content", std::move(content_or_error));
                result = base::Value(std::move(payload));
              }
              sender.Run(
                  BuildJsonRpcSuccessLine(std::move(msg_id), std::move(result)));
            },
            CloneMessageId(msg.id), std::move(sender)));
    return;
  }

  if (*tool_name == "mail_extract_otp") {
    std::string account_id = "";
    std::string folder_id = "";
    std::string query = "";
    int64_t max_age = 300;
    if (arguments) {
      if (const std::string* val = arguments->FindString("account_id")) {
        account_id = *val;
      }
      if (const std::string* val = arguments->FindString("folder_id")) {
        folder_id = *val;
      }
      if (const std::string* val = arguments->FindString("query")) {
        query = *val;
      }
      if (std::optional<int> val = arguments->FindInt("max_age_seconds")) {
        max_age = *val;
      }
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailExtractOtp(
        account_id, folder_id, query, max_age,
        make_mail_read_completion());
    return;
  }

  if (*tool_name == "mail_list_accounts") {
    if (params.Find("arguments") && !arguments) {
      reject_invalid_mail_params("Invalid params: arguments must be an object");
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailListAccounts(make_mail_read_completion());
    return;
  }

  if (*tool_name == "mail_list_folders") {
    const std::string* account_id =
        FindRequiredStringArgument(arguments, "account_id");
    if (!account_id) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("account_id"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailListFolders(*account_id,
                                        make_mail_read_completion());
    return;
  }

  if (*tool_name == "mail_list_emails") {
    const std::string* account_id =
        FindRequiredStringArgument(arguments, "account_id");
    const std::string* folder_id =
        FindRequiredStringArgument(arguments, "folder_id");
    if (!account_id) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("account_id"));
      return;
    }
    if (!folder_id) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("folder_id"));
      return;
    }

    int limit = 50;
    int offset = 0;
    std::string error_message;
    if (!ReadOptionalNonNegativeIntArgument(arguments, "limit", 50, &limit,
                                            &error_message) ||
        !ReadOptionalNonNegativeIntArgument(arguments, "offset", 0, &offset,
                                            &error_message)) {
      reject_invalid_mail_params(std::move(error_message));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailListEmails(*account_id, *folder_id, limit, offset,
                                       make_mail_read_completion());
    return;
  }

  if (*tool_name == "mail_get_email") {
    const std::string* email_id =
        FindRequiredStringArgument(arguments, "email_id");
    if (!email_id) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("email_id"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailGetEmail(*email_id, make_mail_read_completion());
    return;
  }

  if (*tool_name == "mail_search_emails") {
    std::string query_json;
    std::string error_message;
    if (!BuildMailSearchQueryJson(arguments, &query_json, &error_message)) {
      reject_invalid_mail_params(std::move(error_message));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailSearchEmails(query_json,
                                         make_mail_read_completion());
    return;
  }

  if (*tool_name == "mail_list_thread") {
    const std::string* account_id =
        FindRequiredStringArgument(arguments, "account_id");
    const std::string* message_id =
        FindRequiredStringArgument(arguments, "message_id");
    if (!account_id) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("account_id"));
      return;
    }
    if (!message_id) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("message_id"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailListThread(*account_id, *message_id,
                                       make_mail_read_completion());
    return;
  }

  if (*tool_name == "mail_add_account") {
    const std::string* request_json =
        FindRequiredStringArgument(arguments, "request_json");
    if (!request_json) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("request_json"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailAddAccount(*request_json,
                                       make_mail_read_completion());
    return;
  }

  if (*tool_name == "mail_test_connection") {
    const std::string* params_json =
        FindRequiredStringArgument(arguments, "params_json");
    if (!params_json) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("params_json"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailTestConnection(*params_json,
                                           make_mail_read_completion());
    return;
  }

  if (*tool_name == "mail_delete_account") {
    const std::string* account_id =
        FindRequiredStringArgument(arguments, "account_id");
    if (!account_id) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("account_id"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailDeleteAccount(*account_id,
                                          make_mail_read_completion());
    return;
  }

  if (*tool_name == "mail_start_oauth") {
    const std::string* provider =
        FindRequiredStringArgument(arguments, "provider");
    const std::string* client_id =
        FindRequiredStringArgument(arguments, "client_id");
    const std::string* redirect_uri =
        FindRequiredStringArgument(arguments, "redirect_uri");
    if (!provider) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("provider"));
      return;
    }
    if (!client_id) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("client_id"));
      return;
    }
    if (!redirect_uri) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("redirect_uri"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailOAuthStartUrl(*provider, *client_id, *redirect_uri,
                                          make_mail_read_completion());
    return;
  }

  if (*tool_name == "mail_complete_oauth") {
    const std::string* state =
        FindRequiredStringArgument(arguments, "state");
    const std::string* code =
        FindRequiredStringArgument(arguments, "code");
    if (!state) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("state"));
      return;
    }
    if (!code) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("code"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailOAuthComplete(*state, *code,
                                          make_mail_read_completion());
    return;
  }

  if (*tool_name == "mail_reconnect_account") {
    const std::string* account_id =
        FindRequiredStringArgument(arguments, "account_id");
    if (!account_id) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("account_id"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailReconnectAccount(*account_id,
                                             make_mail_read_completion());
    return;
  }

  if (*tool_name == "mail_import_migration_archive") {
    const std::string* archive_json =
        FindRequiredStringArgument(arguments, "archive_json");
    if (!archive_json) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("archive_json"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailImportMigrationArchive(*archive_json,
                                                   make_mail_read_completion());
    return;
  }

  if (*tool_name == "mail_send") {
    const std::string* request_json =
        FindRequiredStringArgument(arguments, "request_json");
    if (!request_json) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("request_json"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailSendEmail(*request_json,
                                      /*already_authorized=*/false,
                                      make_mail_read_completion());
    return;
  }

  if (*tool_name == "mail_save_draft") {
    const std::string* request_json =
        FindRequiredStringArgument(arguments, "request_json");
    if (!request_json) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("request_json"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailSaveDraft(*request_json,
                                      /*already_authorized=*/false,
                                      make_mail_read_completion());
    return;
  }

  if (*tool_name == "mail_update_draft") {
    const std::string* draft_id =
        FindRequiredStringArgument(arguments, "draft_id");
    const std::string* request_json =
        FindRequiredStringArgument(arguments, "request_json");
    if (!draft_id || !request_json) {
      reject_invalid_mail_params("Invalid params: draft_id and request_json required");
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailUpdateDraft(*draft_id, *request_json,
                                        /*already_authorized=*/false,
                                        make_mail_read_completion());
    return;
  }

  if (*tool_name == "mail_queue_email") {
    const std::string* request_json =
        FindRequiredStringArgument(arguments, "request_json");
    if (!request_json) {
      reject_invalid_mail_params(RequiredStringArgumentMessage("request_json"));
      return;
    }
    if (!mail_broker_ready()) {
      return;
    }
    g_browser_delegate->MailQueueEmail(*request_json,
                                       make_mail_read_completion());
    return;
  }

  if (*tool_name == "vault_list_credentials_for_active_page") {
    int tab_id = use_resolved_target ? resolved_tab_id
                                     : (arguments && arguments->FindInt("tab_id")
                                            ? arguments->FindInt("tab_id").value()
                                            : active_tab_id_);
    base::ListValue items;
    if (g_browser_delegate) {
      for (const auto& credential :
           g_browser_delegate->VaultListCredentialsForActivePage(tab_id)) {
        base::DictValue item;
        item.Set("handle", credential.handle);
        item.Set("username_hint", credential.username_hint);
        item.Set("origin", credential.origin);
        items.Append(std::move(item));
      }
    }
    base::DictValue result;
    result.Set("items", std::move(items));
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    responses.push_back(
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone()));
    return;
  }

  if (*tool_name == "vault_request_credential_use") {
    const std::string* handle = FindRequiredStringArgument(arguments, "handle");
    const std::string* origin = FindRequiredStringArgument(arguments, "origin");
    if (!handle) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32602, RequiredStringArgumentMessage("handle")));
      return;
    }
    if (!origin) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32602, RequiredStringArgumentMessage("origin")));
      return;
    }
    int tab_id = use_resolved_target ? resolved_tab_id
                                     : (arguments && arguments->FindInt("tab_id")
                                            ? arguments->FindInt("tab_id").value()
                                            : active_tab_id_);
    std::optional<std::string> grant_handle =
        g_browser_delegate ? g_browser_delegate->VaultRequestCredentialUse(
                                 tab_id, *handle, *origin)
                           : std::nullopt;
    if (!grant_handle) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32000, "Credential grant unavailable"));
      return;
    }
    base::DictValue result;
    result.Set("grant_handle", *grant_handle);
    result.Set("status", "granted");
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    responses.push_back(
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone()));
    return;
  }

  if (*tool_name == "vault_fill_credential" || *tool_name == "vault_fill_totp") {
    const std::string* grant_handle =
        FindRequiredStringArgument(arguments, "grant_handle");
    if (!grant_handle) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32602, RequiredStringArgumentMessage("grant_handle")));
      return;
    }
    if (!arguments || !arguments->FindInt("ref")) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: ref required"));
      return;
    }
    int ref = arguments->FindInt("ref").value();
    auto it = ref_to_ax_id_.find(ref);
    if (it == ref_to_ax_id_.end()) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32000,
          "Unknown ref: call browser_accessibility_snapshot first"));
      return;
    }
    int tab_id = use_resolved_target ? resolved_tab_id
                                     : arguments->FindInt("tab_id").value_or(active_tab_id_);
    bool filled = false;
    if (g_browser_delegate) {
      filled = (*tool_name == "vault_fill_totp")
                   ? g_browser_delegate->VaultFillTotp(tab_id, *grant_handle,
                                                       it->second)
                   : g_browser_delegate->VaultFillCredential(
                         tab_id, *grant_handle, it->second);
    }
    if (!filled) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32000, "Credential fill failed"));
      return;
    }
    base::DictValue result;
    result.Set("filled", true);
    result.Set("ref", ref);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    responses.push_back(
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone()));
    return;
  }

  if (*tool_name == "browser_acquire_lease") {
    HandleAcquireLease(msg, arguments, responses);
    return;
  }

  if (*tool_name == "browser_heartbeat_lease") {
    HandleHeartbeatLease(msg, arguments, responses);
    return;
  }

  if (*tool_name == "browser_release_lease") {
    HandleReleaseLease(msg, arguments, responses);
    return;
  }

  if (*tool_name == "browser_tab_close") {
    if (!arguments || !arguments->FindInt("tab_id")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: tab_id required");
      responses.push_back(std::move(err));
      return;
    }
    int target_id = arguments->FindInt("tab_id").value();
    bool closed = false;
    if (g_browser_delegate) {
      closed = g_browser_delegate->CloseTab(
          use_resolved_target ? resolved_tab_id : target_id);
    } else {
      auto it = std::remove_if(tabs_.begin(), tabs_.end(), [target_id](const TabInfo& t) {
        return t.id == target_id;
      });
      if (it != tabs_.end()) {
        tabs_.erase(it, tabs_.end());
        closed = true;
      }
    }
    if (!closed) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000, "Unknown tab_id");
      responses.push_back(std::move(err));
      return;
    }

    // Sync local list as fallback
    auto it = std::remove_if(tabs_.begin(), tabs_.end(), [target_id](const TabInfo& t) {
      return t.id == target_id;
    });
    if (it != tabs_.end()) {
      tabs_.erase(it, tabs_.end());
    }
    if (active_tab_id_ == target_id) {
      if (!tabs_.empty()) {
        tabs_[0].is_active = true;
        active_tab_id_ = tabs_[0].id;
      } else {
        active_tab_id_ = -1;
      }
    }
    base::DictValue result;
    result.Set("closed", true);
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_tab_new") {
    if (arguments && arguments->FindString("url")) {
      const std::string& url_str = *arguments->FindString("url");
      const GURL nav_url(url_str);
      if (!nav_url.is_valid()) {
        std::string err = framer_.BuildErrorResponse(
            msg.id, -32602, "Invalid params: url is not a valid GURL");
        responses.push_back(std::move(err));
        return;
      }
      if (IsHostBlocked(nav_url.host())) {
        std::string err = framer_.BuildErrorResponse(
            msg.id, -32000,
            "New tab blocked: host is in the disallow list");
        responses.push_back(std::move(err));
        return;
      }
    }
    std::string url_str = arguments && arguments->FindString("url")
                              ? *arguments->FindString("url")
                              : "about:blank";
    const GURL nav_url(url_str);
    int new_id = 0;
    if (g_browser_delegate) {
      new_id = g_browser_delegate->CreateNewTab(nav_url);
    } else {
      for (const auto& t : tabs_) {
        if (t.id >= new_id) {
          new_id = t.id + 1;
        }
      }
    }
    for (auto& t : tabs_) {
      t.is_active = false;
    }
    TabInfo new_tab;
    new_tab.id = new_id;
    new_tab.title = url_str == "about:blank" ? "New Tab" : nav_url.host();
    new_tab.url = url_str;
    new_tab.is_active = true;
    tabs_.push_back(new_tab);
    active_tab_id_ = new_id;

    base::DictValue tab;
    tab.Set("id", new_tab.id);
    tab.Set("title", new_tab.title);
    tab.Set("url", new_tab.url);
    tab.Set("is_active", new_tab.is_active);
    base::DictValue result;
    result.Set("tab", std::move(tab));
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_history_search") {
    const std::string query = (arguments && arguments->FindString("query"))
                                  ? *arguments->FindString("query")
                                  : std::string();
    const int max_results = (arguments && arguments->FindInt("max_results"))
                                ? arguments->FindInt("max_results").value()
                                : 20;
    base::ListValue entries_list;
    if (g_browser_delegate) {
      for (const auto& h : g_browser_delegate->SearchHistory(
               query, static_cast<size_t>(max_results))) {
        base::DictValue entry;
        entry.Set("url", h.url);
        entry.Set("title", h.title);
        entry.Set("visited_at", h.visited_at);
        entries_list.Append(std::move(entry));
      }
    }
    base::DictValue result;
    result.Set("entries", std::move(entries_list));
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    if (deferred_response_sender_) {
      deferred_response_sender_.Run(std::move(resp));
      return;
    }
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_bookmarks_search") {
    if (!arguments || !arguments->FindString("query")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: query required");
      responses.push_back(std::move(err));
      return;
    }
    const std::string& query = *arguments->FindString("query");
    base::ListValue bookmarks_list;
    if (g_browser_delegate) {
      for (const auto& b : g_browser_delegate->SearchBookmarks(query)) {
        base::DictValue bookmark;
        bookmark.Set("id", static_cast<double>(b.id));
        bookmark.Set("title", b.title);
        bookmark.Set("url", b.url);
        if (!b.folder.empty()) {
          bookmark.Set("folder", b.folder);
        }
        bookmarks_list.Append(std::move(bookmark));
      }
    }
    base::DictValue result;
    result.Set("bookmarks", std::move(bookmarks_list));
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_bookmark_create") {
    if (!arguments || !arguments->FindString("title") ||
        !arguments->FindString("url")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: title and url required");
      responses.push_back(std::move(err));
      return;
    }
    const std::string& title = *arguments->FindString("title");
    const std::string& url_str = *arguments->FindString("url");
    const GURL url(url_str);
    if (!url.is_valid()) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: url is not a valid GURL");
      responses.push_back(std::move(err));
      return;
    }
    const std::string folder = arguments->FindString("folder") ? *arguments->FindString("folder") : std::string();
    base::DictValue result;
    if (g_browser_delegate) {
      auto b = g_browser_delegate->CreateBookmark(title, url, folder);
      base::DictValue bookmark;
      bookmark.Set("id", static_cast<double>(b.id));
      bookmark.Set("title", b.title);
      bookmark.Set("url", b.url);
      if (!b.folder.empty()) {
        bookmark.Set("folder", b.folder);
      }
      result.Set("bookmark", std::move(bookmark));
    } else {
      base::DictValue bookmark;
      bookmark.Set("id", 0);
      bookmark.Set("title", title);
      bookmark.Set("url", url_str);
      if (!folder.empty()) {
        bookmark.Set("folder", folder);
      }
      result.Set("bookmark", std::move(bookmark));
    }
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_page_content") {
    int tab_id = use_resolved_target ? resolved_tab_id : (arguments && arguments->FindInt("tab_id") ? arguments->FindInt("tab_id").value() : active_tab_id_);
    base::DictValue result;
    if (g_browser_delegate) {
      auto res = g_browser_delegate->GetPageContent(tab_id);
      result.Set("text", res.text);
      result.Set("url", res.url);
      result.Set("title", res.title);
      result.Set("redacted", false);
    } else {
      result.Set("text", "");
      result.Set("url", "");
      result.Set("title", "");
      result.Set("redacted", false);
    }
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_page_text") {
    int tab_id = use_resolved_target ? resolved_tab_id : (arguments && arguments->FindInt("tab_id") ? arguments->FindInt("tab_id").value() : active_tab_id_);
    std::string text;
    if (g_browser_delegate) {
      text = g_browser_delegate->GetPageText(tab_id);
    }
    base::DictValue result;
    result.Set("text", text);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_search_in_page") {
    if (!arguments || !arguments->FindString("query")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: query required");
      responses.push_back(std::move(err));
      return;
    }
    int tab_id = use_resolved_target ? resolved_tab_id : arguments->FindInt("tab_id").value_or(active_tab_id_);
    const std::string& query = *arguments->FindString("query");
    base::DictValue result;
    if (g_browser_delegate) {
      auto res = g_browser_delegate->SearchInPage(tab_id, query);
      result.Set("match_count", res.match_count);
      result.Set("active_match_index", res.active_match_index);
    } else {
      result.Set("match_count", 0);
      result.Set("active_match_index", -1);
    }
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_page_context") {
    int tab_id = use_resolved_target ? resolved_tab_id : (arguments && arguments->FindInt("tab_id") ? arguments->FindInt("tab_id").value() : active_tab_id_);
    base::DictValue result;
    if (g_browser_delegate) {
      auto res = g_browser_delegate->GetPageContext(tab_id);
      result.Set("url", res.url);
      result.Set("title", res.title);
      result.Set("content", res.content);
    } else {
      result.Set("url", "");
      result.Set("title", "");
      result.Set("content", "");
    }
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "page_query_selector") {
    if (!arguments || !arguments->FindString("selector")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: selector required");
      responses.push_back(std::move(err));
      return;
    }
    int tab_id = use_resolved_target ? resolved_tab_id : arguments->FindInt("tab_id").value_or(active_tab_id_);
    const std::string& selector = *arguments->FindString("selector");
    base::DictValue result;
    if (g_browser_delegate) {
      auto res = g_browser_delegate->QuerySelector(tab_id, selector);
      result.Set("ref_id", res.ref_id);
      result.Set("tag", res.tag);
    } else {
      result.Set("ref_id", "");
      result.Set("tag", "");
    }
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "page_get_text") {
    if (!arguments || !arguments->FindString("ref_id")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: ref_id required");
      responses.push_back(std::move(err));
      return;
    }
    int tab_id = use_resolved_target ? resolved_tab_id : arguments->FindInt("tab_id").value_or(active_tab_id_);
    const std::string& ref_id = *arguments->FindString("ref_id");
    base::DictValue result;
    if (g_browser_delegate) {
      result.Set("text", g_browser_delegate->GetElementText(tab_id, ref_id));
    } else {
      result.Set("text", "");
    }
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "page_get_attribute") {
    if (!arguments || !arguments->FindString("ref_id") ||
        !arguments->FindString("attribute")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: ref_id and attribute required");
      responses.push_back(std::move(err));
      return;
    }
    int tab_id = use_resolved_target ? resolved_tab_id : arguments->FindInt("tab_id").value_or(active_tab_id_);
    const std::string& ref_id = *arguments->FindString("ref_id");
    const std::string& attribute = *arguments->FindString("attribute");
    base::DictValue result;
    if (g_browser_delegate) {
      result.Set("value", g_browser_delegate->GetElementAttribute(tab_id, ref_id, attribute));
    } else {
      result.Set("value", "");
    }
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_same_origin_fetch") {
    const std::string* url_arg = FindRequiredStringArgument(arguments, "url");
    if (!url_arg) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32602, RequiredStringArgumentMessage("url")));
      return;
    }
    const GURL url(*url_arg);
    if (!url.is_valid() || !url.SchemeIs("https") || !url.has_host()) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: url must be an HTTPS URL"));
      return;
    }
    const std::string method =
        arguments && arguments->FindString("method")
            ? base::ToUpperASCII(*arguments->FindString("method"))
            : "GET";
    if (method != "GET" && method != "POST") {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: method must be GET or POST"));
      return;
    }
    const std::string body =
        arguments && arguments->FindString("body")
            ? *arguments->FindString("body")
            : std::string();
    base::DictValue headers;
    if (arguments && arguments->Find("headers")) {
      const base::DictValue* requested_headers =
          arguments->FindDict("headers");
      if (!requested_headers) {
        responses.push_back(framer_.BuildErrorResponse(
            msg.id, -32602,
            "Invalid params: headers must be an object of strings"));
        return;
      }
      for (const auto [name, value] : *requested_headers) {
        if (!value.is_string()) {
          responses.push_back(framer_.BuildErrorResponse(
              msg.id, -32602,
              "Invalid params: header values must be strings"));
          return;
        }
        headers.Set(name, value.GetString());
      }
    }
    if (method == "POST" && !headers.contains("content-type")) {
      headers.Set("content-type", "application/json");
    }
    std::string headers_json;
    base::JSONWriter::Write(base::Value(std::move(headers)), &headers_json);
    const int tab_id = use_resolved_target ? resolved_tab_id : active_tab_id_;
    if (!g_browser_delegate || !deferred_response_sender_) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32000, "Same-origin fetch unavailable"));
      return;
    }
    const GURL page_url(g_browser_delegate->GetPageContent(tab_id).url);
    if (!page_url.is_valid() ||
        page_url.DeprecatedGetOriginAsURL() != url.DeprecatedGetOriginAsURL()) {
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, -32000,
          "Same-origin fetch blocked: URL origin does not match target tab"));
      return;
    }
    std::optional<base::Value> msg_id_copy = CloneMessageId(msg.id);
    DeferredResponseSender sender = deferred_response_sender_;
    g_browser_delegate->SameOriginFetch(
        tab_id, url, method, body, headers_json,
        base::BindOnce(
            [](std::optional<base::Value> id, DeferredResponseSender sender,
               MahoMcpBrowserDelegate::SameOriginFetchResult result) {
              if (!result.success) {
                sender.Run(BuildJsonRpcErrorLine(
                    std::move(id), -32000,
                    result.error.empty() ? "Same-origin fetch failed"
                                         : std::move(result.error)));
                return;
              }
              base::DictValue out;
              out.Set("status", result.status);
              out.Set("url", std::move(result.final_url));
              out.Set("content_type", std::move(result.content_type));
              out.Set("text", std::move(result.text));
              out.Set("truncated", result.truncated);
              auto redacted =
                  MahoMcpFirewall::Wrap(base::Value(std::move(out)));
              base::DictValue response;
              response.Set("jsonrpc", "2.0");
              response.Set("id", id.has_value() ? id->Clone() : base::Value());
              response.Set("result", redacted.get().Clone());
              std::string serialized;
              base::JSONWriter::Write(base::Value(std::move(response)),
                                      &serialized);
              serialized.push_back('\n');
              sender.Run(std::move(serialized));
            },
            std::move(msg_id_copy), std::move(sender)));
    return;
  }

  if (*tool_name == "page_wait_for_selector") {
    if (!arguments || !arguments->FindString("selector")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: selector required");
      responses.push_back(std::move(err));
      return;
    }
    int tab_id = use_resolved_target ? resolved_tab_id : arguments->FindInt("tab_id").value_or(active_tab_id_);
    const std::string& selector = *arguments->FindString("selector");
    int timeout_ms = arguments->FindInt("timeout_ms").value_or(10000);
    base::DictValue result;
    if (g_browser_delegate) {
      result.Set("found", g_browser_delegate->WaitForSelector(tab_id, selector, timeout_ms));
    } else {
      result.Set("found", false);
    }
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_accessibility_snapshot") {
    int tab_id = use_resolved_target ? resolved_tab_id : (arguments && arguments->FindInt("tab_id") ? arguments->FindInt("tab_id").value() : active_tab_id_);
    ref_to_ax_id_.clear();
    base::Value snapshot;
    if (g_browser_delegate) {
      snapshot = g_browser_delegate->GetAccessibilitySnapshot(tab_id, &ref_to_ax_id_);
    } else {
      base::DictValue empty;
      empty.Set("role", "WebArea");
      empty.Set("children", base::ListValue());
      snapshot = base::Value(std::move(empty));
    }
    auto redacted = MahoMcpFirewall::Wrap(std::move(snapshot));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_click") {
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : (arguments ? arguments->FindInt("tab_id").value_or(active_tab_id_)
                                  : active_tab_id_);
    append_browser_action_response(MahoMcpBrowserActionHandler::HandleClick(
        g_browser_delegate, ref_to_ax_id_, tab_id, arguments));
    return;
  }

  if (*tool_name == "browser_type") {
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : (arguments ? arguments->FindInt("tab_id").value_or(active_tab_id_)
                                  : active_tab_id_);
    append_browser_action_response(MahoMcpBrowserActionHandler::HandleType(
        g_browser_delegate, ref_to_ax_id_, tab_id, arguments));
    return;
  }

  if (*tool_name == "browser_select") {
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : (arguments ? arguments->FindInt("tab_id").value_or(active_tab_id_)
                                  : active_tab_id_);
    append_browser_action_response(MahoMcpBrowserActionHandler::HandleSelect(
        g_browser_delegate, ref_to_ax_id_, tab_id, arguments));
    return;
  }

  if (*tool_name == "browser_screenshot_full") {
    int tab_id = use_resolved_target ? resolved_tab_id : (arguments && arguments->FindInt("tab_id") ? arguments->FindInt("tab_id").value() : active_tab_id_);
    if (g_browser_delegate && deferred_response_sender_) {
      std::optional<base::Value> msg_id_copy;
      if (msg.id.has_value()) {
        msg_id_copy = msg.id->Clone();
      }
      DeferredResponseSender sender = deferred_response_sender_;
      g_browser_delegate->CaptureFullPagePngBase64(
          tab_id,
          base::BindOnce(
              [](std::optional<base::Value> id,
                 DeferredResponseSender sender, std::string b64) {
                if (b64.empty()) {
                  b64 = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==";
                }
                base::DictValue result;
                result.Set("content_type", "image/png");
                result.Set("data", std::move(b64));
                auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
                base::DictValue response;
                response.Set("jsonrpc", "2.0");
                response.Set("id", id.has_value() ? id->Clone() : base::Value());
                response.Set("result", redacted.get().Clone());
                std::string out;
                base::JSONWriter::Write(base::Value(std::move(response)), &out);
                out.push_back('\n');
                sender.Run(std::move(out));
              },
              std::move(msg_id_copy), std::move(sender)));
      return;
    }
    std::string b64 =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==";
    base::DictValue result;
    result.Set("content_type", "image/png");
    result.Set("data", b64);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_screenshot_element") {
    if (!arguments || !arguments->FindInt("ref")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: ref required");
      responses.push_back(std::move(err));
      return;
    }
    int ref = arguments->FindInt("ref").value();
    auto it = ref_to_ax_id_.find(ref);
    if (it == ref_to_ax_id_.end()) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000, "Unknown ref: call browser_accessibility_snapshot first");
      responses.push_back(std::move(err));
      return;
    }
    int tab_id = use_resolved_target ? resolved_tab_id : arguments->FindInt("tab_id").value_or(active_tab_id_);
    if (g_browser_delegate && deferred_response_sender_) {
      std::optional<base::Value> msg_id_copy;
      if (msg.id.has_value()) {
        msg_id_copy = msg.id->Clone();
      }
      DeferredResponseSender sender = deferred_response_sender_;
      g_browser_delegate->CaptureElementPngBase64(
          tab_id, it->second,
          base::BindOnce(
              [](std::optional<base::Value> id,
                 DeferredResponseSender sender, int ref_val, std::string b64) {
                if (b64.empty()) {
                  b64 = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==";
                }
                base::DictValue result;
                result.Set("content_type", "image/png");
                result.Set("data", std::move(b64));
                result.Set("ref", ref_val);
                auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
                base::DictValue response;
                response.Set("jsonrpc", "2.0");
                response.Set("id", id.has_value() ? id->Clone() : base::Value());
                response.Set("result", redacted.get().Clone());
                std::string out;
                base::JSONWriter::Write(base::Value(std::move(response)), &out);
                out.push_back('\n');
                sender.Run(std::move(out));
              },
              std::move(msg_id_copy), std::move(sender), ref));
      return;
    }
    std::string b64 =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==";
    base::DictValue result;
    result.Set("content_type", "image/png");
    result.Set("data", b64);
    result.Set("ref", ref);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  // Network capture tools (Wave 4.4)
  if (*tool_name == "browser_network_start_capture") {
    if (!g_browser_delegate) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000, "Network capture unavailable");
      responses.push_back(std::move(err));
      return;
    }
    if (!deferred_response_sender_) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000,
          "Deferred response sender unavailable for network start capture");
      responses.push_back(std::move(err));
      return;
    }

    DeferredResponseSender sender = deferred_response_sender_;
    std::optional<base::Value> msg_id = CloneMessageId(msg.id);
    if (network_capture_state_->active) {
      sender.Run(BuildJsonRpcErrorLine(
          std::move(msg_id), -32000, "Network capture already active"));
      return;
    }

    // R-8: forward the concrete id resolved by the top-level gate (never a raw
    // 0), so an omitted request binds to the same tab the gate authorized
    // rather than whatever is active when StartNetworkCapture runs. The
    // no-delegate test path keeps the legacy argument/0 behavior.
    const int requested_tab_id =
        use_resolved_target
            ? resolved_tab_id
            : ((arguments && arguments->FindInt("tab_id"))
                   ? arguments->FindInt("tab_id").value()
                   : 0);

    const std::string capture_id =
        base::Uuid::GenerateRandomV4().AsLowercaseString();
    network_capture_state_->active = true;
    network_capture_state_->capture_id = capture_id;
    network_capture_state_->tab_id = requested_tab_id;
    network_capture_state_->target = last_resolved_target_;
    scoped_refptr<MahoMcpNetworkCaptureState> state = network_capture_state_;

    g_browser_delegate->StartNetworkCapture(
        capture_id, requested_tab_id,
        base::BindOnce(
            [](std::optional<base::Value> id, DeferredResponseSender sender,
               scoped_refptr<MahoMcpNetworkCaptureState> state,
               std::string capture_id,
               MahoMcpBrowserDelegate::StartNetworkCaptureResult result) {
              if (!result.success) {
                // Clear only if this attempt still owns the shared state; a
                // later start may have replaced it. Safe even if the session
                // was destroyed — |state| is refcounted and outlives it.
                if (state->capture_id == capture_id) {
                  state->active = false;
                  state->capture_id.clear();
                  state->tab_id = 0;
                }
                sender.Run(BuildJsonRpcErrorLine(
                    std::move(id), -32000,
                    "No WebContents available for network capture"));
                return;
              }
              // R-8: the bound target may have gone OTR/closed while the start
              // hopped to the UI sequence. Revalidate before serializing
              // success so a raced capture cannot attach to a denied context.
              if (g_browser_delegate && state->target.valid &&
                  !g_browser_delegate->RevalidateTarget(state->target)) {
                if (state->capture_id == capture_id) {
                  state->active = false;
                  state->capture_id.clear();
                  state->tab_id = 0;
                  g_browser_delegate->CancelNetworkCapture(capture_id);
                }
                sender.Run(BuildJsonRpcErrorLine(
                    std::move(id), kMahoMcpErrorTabNotFound,
                    kMahoMcpMessageTabNotFound));
                return;
              }
              if (state->capture_id == capture_id) {
                state->tab_id = result.tab_id;
              }
              base::DictValue payload;
              payload.Set("capturing", true);
              payload.Set("capture_id", std::move(capture_id));
              payload.Set("tab_id", result.tab_id);
              sender.Run(BuildJsonRpcSuccessLine(
                  std::move(id), base::Value(std::move(payload))));
            },
            std::move(msg_id), std::move(sender), std::move(state),
            capture_id));
    return;
  }

  if (*tool_name == "browser_network_stop_capture") {
    if (!network_capture_state_->active ||
        network_capture_state_->capture_id.empty()) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000, "No active capture to stop");
      responses.push_back(std::move(err));
      return;
    }
    if (!g_browser_delegate) {
      network_capture_state_->active = false;
      network_capture_state_->capture_id.clear();
      network_capture_state_->tab_id = 0;
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000, "Network capture unavailable");
      responses.push_back(std::move(err));
      return;
    }
    // R-8: stop/get-HAR revalidate the SAME generation-bound target instead of
    // resolving a new active tab, so a capture cannot leak after the tab it was
    // bound to is closed/reused or has gone OTR.
    if (network_capture_state_->target.valid &&
        !g_browser_delegate->RevalidateTarget(network_capture_state_->target)) {
      const std::string capture_id = network_capture_state_->capture_id;
      network_capture_state_->active = false;
      network_capture_state_->capture_id.clear();
      network_capture_state_->tab_id = 0;
      network_capture_state_->target = ResolvedMahoMcpTarget();
      g_browser_delegate->CancelNetworkCapture(capture_id);
      responses.push_back(framer_.BuildErrorResponse(
          msg.id, kMahoMcpErrorTabNotFound, kMahoMcpMessageTabNotFound));
      return;
    }
    if (!deferred_response_sender_) {
      const std::string capture_id = network_capture_state_->capture_id;
      network_capture_state_->active = false;
      network_capture_state_->capture_id.clear();
      network_capture_state_->tab_id = 0;
      g_browser_delegate->CancelNetworkCapture(capture_id);
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000,
          "Deferred response sender unavailable for network stop capture");
      responses.push_back(std::move(err));
      return;
    }

    const std::string capture_id = network_capture_state_->capture_id;
    network_capture_state_->active = false;
    network_capture_state_->capture_id.clear();
    network_capture_state_->tab_id = 0;
    DeferredResponseSender sender = deferred_response_sender_;
    g_browser_delegate->StopNetworkCapture(
        capture_id,
        base::BindOnce(
            [](std::optional<base::Value> id, DeferredResponseSender sender,
               std::optional<Redacted<base::DictValue>> har) {
              if (!har.has_value()) {
                sender.Run(BuildJsonRpcErrorLine(
                    std::move(id), -32000,
                    "Network capture not found or already stopped"));
                return;
              }
              sender.Run(BuildJsonRpcSuccessLine(
                  std::move(id), base::Value(std::move(*har).take())));
            },
            CloneMessageId(msg.id), std::move(sender)));
    return;
  }

  if (*tool_name == "browser_network_get_har") {
    if (network_capture_state_->active) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000,
          "Capture is active; call browser_network_stop_capture for HAR");
      responses.push_back(std::move(err));
      return;
    }
    std::string err = framer_.BuildErrorResponse(
        msg.id, -32000, "No capture started");
    responses.push_back(std::move(err));
    return;
  }

  if (*tool_name == "browser_console_messages") {
    int tab_id = use_resolved_target ? resolved_tab_id : (arguments && arguments->FindInt("tab_id") ? arguments->FindInt("tab_id").value() : active_tab_id_);
    int limit = arguments && arguments->FindInt("limit") ? arguments->FindInt("limit").value() : 100;
    std::optional<int64_t> since_timestamp_ms;
    if (arguments && arguments->FindDouble("since_timestamp_ms")) {
      since_timestamp_ms = static_cast<int64_t>(arguments->FindDouble("since_timestamp_ms").value());
    } else if (arguments && arguments->FindInt("since_timestamp_ms")) {
      since_timestamp_ms = static_cast<int64_t>(arguments->FindInt("since_timestamp_ms").value());
    }

    std::set<std::string> level_filter;
    if (arguments && arguments->FindList("level_filter")) {
      for (const auto& item : *arguments->FindList("level_filter")) {
        if (item.is_string()) {
          level_filter.insert(item.GetString());
        }
      }
    }

    base::ListValue msgs;
    bool truncated = false;
    bool exists = TabExists(tab_id, tabs_);
    if (g_browser_delegate && exists) {
      int count = 0;
      for (const auto& msg_entry : g_browser_delegate->GetConsoleMessages(tab_id)) {
        if (!level_filter.empty() && level_filter.find(msg_entry.level) == level_filter.end()) {
          continue;
        }
        if (since_timestamp_ms.has_value() && msg_entry.timestamp_ms <= since_timestamp_ms.value()) {
          continue;
        }
        if (count >= limit) {
          truncated = true;
          break;
        }
        base::DictValue entry;
        entry.Set("level", msg_entry.level);
        entry.Set("message", msg_entry.message);
        entry.Set("source_url", msg_entry.source_url);
        entry.Set("line", msg_entry.line);
        entry.Set("timestamp_ms", static_cast<double>(msg_entry.timestamp_ms));
        msgs.Append(std::move(entry));
        count++;
      }
    } else {
      auto it = console_buffers_.find(tab_id);
      if (it != console_buffers_.end()) {
        int count = 0;
        for (const auto& msg_entry : it->second) {
          if (!level_filter.empty() && level_filter.find(msg_entry.level) == level_filter.end()) {
            continue;
          }
          if (since_timestamp_ms.has_value() && msg_entry.timestamp_ms <= since_timestamp_ms.value()) {
            continue;
          }
          if (count >= limit) {
            truncated = true;
            break;
          }
          base::DictValue entry;
          entry.Set("level", msg_entry.level);
          entry.Set("message", msg_entry.message);
          entry.Set("source_url", msg_entry.source_url);
          entry.Set("line", msg_entry.line);
          entry.Set("timestamp_ms", static_cast<double>(msg_entry.timestamp_ms));
          msgs.Append(std::move(entry));
          count++;
        }
      }
    }

    base::DictValue result;
    result.Set("messages", std::move(msgs));
    result.Set("truncated", truncated);

    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_wait_for_navigation") {
    int tab_id = use_resolved_target ? resolved_tab_id : (arguments && arguments->FindInt("tab_id") ? arguments->FindInt("tab_id").value() : active_tab_id_);
    int64_t since_ms = 0;
    if (arguments) {
      if (arguments->FindDouble("since_timestamp_ms")) {
        since_ms = static_cast<int64_t>(arguments->FindDouble("since_timestamp_ms").value());
      } else if (arguments->FindInt("since_timestamp_ms")) {
        since_ms = static_cast<int64_t>(arguments->FindInt("since_timestamp_ms").value());
      }
    }
    if (since_ms == 0) {
      since_ms = base::Time::Now().InMillisecondsSinceUnixEpoch();
    }
    int timeout_ms = arguments && arguments->FindInt("timeout_ms") ? arguments->FindInt("timeout_ms").value() : 30000;

    base::TimeTicks start = base::TimeTicks::Now();
    base::TimeDelta timeout = base::Milliseconds(timeout_ms);
    std::vector<MahoMcpSession::NavigationEvent> events;
    if (g_browser_delegate) {
      while (base::TimeTicks::Now() - start < timeout) {
        events = g_browser_delegate->GetNavigationEvents(tab_id, since_ms);
        if (!events.empty()) {
          break;
        }
        base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE, run_loop.QuitClosure(), base::Milliseconds(100));
        run_loop.Run();
      }
    }

    base::DictValue result;
    bool got_event = false;
    if (!events.empty()) {
      const auto& ev = events.back();
      result.Set("navigated", true);
      result.Set("url", ev.url);
      result.Set("title", ev.title);
      result.Set("status_code", ev.status_code);
      result.Set("timestamp_ms", static_cast<double>(ev.timestamp_ms));
      got_event = true;
    }

    if (!got_event && g_browser_delegate) {
      int target_tab = (tab_id == 0) ? active_tab_id_ : tab_id;
      for (const auto& t : g_browser_delegate->GetTabList()) {
        if ((tab_id == 0 && t.is_active) || t.id == target_tab) {
          result.Set("navigated", false);
          result.Set("url", t.url);
          result.Set("title", t.title);
          result.Set("status_code", 200);
          result.Set("timestamp_ms",
                     static_cast<double>(
                         base::Time::Now().InMillisecondsSinceUnixEpoch()));
          got_event = true;
          break;
        }
      }
    }
    if (!got_event) {
      const TabInfo* found = nullptr;
      for (const auto& t : tabs_) {
        if (t.id == tab_id) {
          found = &t;
          break;
        }
      }
      if (found) {
        result.Set("navigated", false);
        result.Set("url", found->url);
        result.Set("title", found->title);
        result.Set("status_code", 200);
        result.Set("timestamp_ms",
                   static_cast<double>(
                       base::Time::Now().InMillisecondsSinceUnixEpoch()));
      } else {
        result.Set("timeout", true);
        result.Set("last_url", "");
      }
    }

    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_tab_switch") {
    if (!arguments || !arguments->FindInt("tab_id")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: tab_id required");
      responses.push_back(std::move(err));
      return;
    }
    int target_id = arguments->FindInt("tab_id").value();
    bool found = false;
    int previous_active_id = active_tab_id_;

    if (TabExists(target_id, tabs_)) {
      found = true;
      if (g_browser_delegate) {
        g_browser_delegate->ActivateTab(
            use_resolved_target ? resolved_tab_id : target_id);
      }
    } else {
      for (const auto& t : tabs_) {
        if (t.id == target_id) {
          found = true;
          break;
        }
      }
    }

    if (!found) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000, "Unknown tab_id");
      responses.push_back(std::move(err));
      return;
    }
    for (auto& t : tabs_) {
      t.is_active = (t.id == target_id);
    }
    active_tab_id_ = target_id;
    base::DictValue result;
    result.Set("activated", true);
    result.Set("previous_tab_id", previous_active_id);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_set_viewport_size") {
    if (!arguments || !arguments->FindInt("width_px") || !arguments->FindInt("height_px")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: width_px and height_px required");
      responses.push_back(std::move(err));
      return;
    }
    int width = arguments->FindInt("width_px").value();
    int height = arguments->FindInt("height_px").value();
    if (width < 100 || width > 4096 || height < 100 || height > 4096) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: dimensions out of bounds (100-4096)");
      responses.push_back(std::move(err));
      return;
    }
    viewport_width_ = width;
    viewport_height_ = height;

    int tab_id = use_resolved_target ? resolved_tab_id : arguments->FindInt("tab_id").value_or(active_tab_id_);
    if (g_browser_delegate) {
      g_browser_delegate->SetViewportSize(tab_id, width, height);
    }

    base::DictValue result;
    result.Set("width", width);
    result.Set("height", height);
    result.Set("device_scale_factor", 1.0);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_scroll") {
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : (arguments ? arguments->FindInt("tab_id").value_or(active_tab_id_)
                                  : active_tab_id_);
    append_browser_action_response(MahoMcpBrowserActionHandler::HandleScroll(
        g_browser_delegate, ref_to_ax_id_, scroll_y_positions_, tab_id,
        arguments));
    return;
  }

  if (*tool_name == "browser_hover") {
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : (arguments ? arguments->FindInt("tab_id").value_or(active_tab_id_)
                                  : active_tab_id_);
    append_browser_action_response(MahoMcpBrowserActionHandler::HandleHover(
        g_browser_delegate, ref_to_ax_id_, tab_id, arguments));
    return;
  }

  if (*tool_name == "browser_key_press") {
    int tab_id = use_resolved_target
                     ? resolved_tab_id
                     : (arguments ? arguments->FindInt("tab_id").value_or(active_tab_id_)
                                  : active_tab_id_);
    append_browser_action_response(MahoMcpBrowserActionHandler::HandleKeyPress(
        g_browser_delegate, tab_id, arguments));
    return;
  }

  std::string err = framer_.BuildErrorResponse(
      msg.id, -32601, "Method not found: unknown tool");
  responses.push_back(std::move(err));
}

std::string MahoMcpBrowserDelegate::ListRoutines() {
  return "[]";
}

void MahoMcpBrowserDelegate::RunRoutine(const std::string& id,
                                        RunRoutineCallback callback) {
  std::move(callback).Run(false, "Routines feature unavailable");
}

void MahoMcpBrowserDelegate::MailListAccounts(MailReadCallback callback) {
  std::move(callback).Run(false, "Mail read broker unavailable");
}

void MahoMcpBrowserDelegate::MailListFolders(const std::string& account_id,
                                             MailReadCallback callback) {
  std::move(callback).Run(false, "Mail read broker unavailable");
}

void MahoMcpBrowserDelegate::MailListEmails(const std::string& account_id,
                                            const std::string& folder_id,
                                            int64_t limit,
                                            int64_t offset,
                                            MailReadCallback callback) {
  std::move(callback).Run(false, "Mail read broker unavailable");
}

void MahoMcpBrowserDelegate::MailGetEmail(const std::string& email_id,
                                          MailReadCallback callback) {
  std::move(callback).Run(false, "Mail read broker unavailable");
}

void MahoMcpBrowserDelegate::MailSearchEmails(const std::string& query_json,
                                              MailReadCallback callback) {
  std::move(callback).Run(false, "Mail read broker unavailable");
}

void MahoMcpBrowserDelegate::MailListThread(const std::string& account_id,
                                            const std::string& message_id,
                                            MailReadCallback callback) {
  std::move(callback).Run(false, "Mail read broker unavailable");
}

void MahoMcpBrowserDelegate::MailAddAccount(const std::string& request_json,
                                            MailReadCallback callback) {
  std::move(callback).Run(false, "Mail onboarding broker unavailable");
}

void MahoMcpBrowserDelegate::MailTestConnection(const std::string& params_json,
                                                MailReadCallback callback) {
  std::move(callback).Run(false, "Mail onboarding broker unavailable");
}

void MahoMcpBrowserDelegate::MailDeleteAccount(const std::string& account_id,
                                               MailReadCallback callback) {
  std::move(callback).Run(false, "Mail onboarding broker unavailable");
}

void MahoMcpBrowserDelegate::MailOAuthStartUrl(const std::string& provider,
                                               const std::string& client_id,
                                               const std::string& redirect_uri,
                                               MailReadCallback callback) {
  std::move(callback).Run(false, "Mail onboarding broker unavailable");
}

void MahoMcpBrowserDelegate::MailOAuthComplete(const std::string& state,
                                               const std::string& code,
                                               MailReadCallback callback) {
  std::move(callback).Run(false, "Mail onboarding broker unavailable");
}

void MahoMcpBrowserDelegate::MailReconnectAccount(const std::string& account_id,
                                                  MailReadCallback callback) {
  std::move(callback).Run(false, "Mail onboarding broker unavailable");
}

void MahoMcpBrowserDelegate::MailImportMigrationArchive(const std::string& archive_json,
                                                        MailReadCallback callback) {
  std::move(callback).Run(false, "Mail onboarding broker unavailable");
}

void MahoMcpBrowserDelegate::MailExtractOtp(const std::string& account_id,
                                            const std::string& folder_id,
                                            const std::string& query,
                                            int64_t max_age_seconds,
                                            MailReadCallback callback) {
  std::move(callback).Run(false, "Mail onboarding broker unavailable");
}

void MahoMcpBrowserDelegate::MailSendEmail(const std::string& request_json,
                                           bool /*already_authorized*/,
                                           MailReadCallback callback) {
  std::move(callback).Run(false, "Mail write broker unavailable");
}

void MahoMcpBrowserDelegate::MailSaveDraft(const std::string& request_json,
                                           bool /*already_authorized*/,
                                           MailReadCallback callback) {
  std::move(callback).Run(false, "Mail write broker unavailable");
}

void MahoMcpBrowserDelegate::MailUpdateDraft(const std::string& draft_id,
                                             const std::string& request_json,
                                             bool /*already_authorized*/,
                                             MailReadCallback callback) {
  std::move(callback).Run(false, "Mail write broker unavailable");
}

void MahoMcpBrowserDelegate::MailQueueEmail(const std::string& request_json,
                                            MailReadCallback callback) {
  std::move(callback).Run(false, "Mail write broker unavailable");
}

std::vector<MahoMcpBrowserDelegate::VaultCredentialSummary>
MahoMcpBrowserDelegate::VaultListCredentialsForActivePage(int tab_id) {
  return {};
}

std::optional<std::string> MahoMcpBrowserDelegate::VaultRequestCredentialUse(
    int tab_id,
    const std::string& handle,
    const std::string& origin) {
  return std::nullopt;
}

bool MahoMcpBrowserDelegate::VaultFillCredential(
    int tab_id,
    const std::string& grant_handle,
    ui::AXNodeID ax_id) {
  return false;
}

bool MahoMcpBrowserDelegate::VaultFillTotp(int tab_id,
                                           const std::string& grant_handle,
                                           ui::AXNodeID ax_id) {
  return false;
}

MahoMcpTargetResolution MahoMcpBrowserDelegate::ResolveTabTarget(
    int requested_tab_id) {
  MahoMcpTargetResolution resolution;
  resolution.target.valid = true;
  resolution.target.tab_id = requested_tab_id;
  return resolution;
}

MahoMcpTargetResolution MahoMcpBrowserDelegate::ResolveProfileTarget() {
  MahoMcpTargetResolution resolution;
  resolution.target.valid = true;
  return resolution;
}

bool MahoMcpBrowserDelegate::RevalidateTarget(
    const ResolvedMahoMcpTarget& target) {
  return true;
}

MahoMcpFieldMetadata MahoMcpBrowserDelegate::GetFieldMetadata(
    int tab_id,
    ui::AXNodeID ax_id) {
  return MahoMcpFieldMetadata();
}

// static
bool MahoMcpSession::IsCredentialField(const MahoMcpFieldMetadata& meta) {
  return MahoMcpBrowserActionHandler::IsCredentialField(meta);
}

}  // namespace maho
