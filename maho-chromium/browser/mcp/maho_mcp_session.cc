// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_session.h"

#include "build/build_config.h"

#if !BUILDFLAG(IS_WIN)
#include <unistd.h>
#endif

#include <string>
#include <vector>

#include "base/base64.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "base/values.h"
#include "maho/browser/mcp/maho_mcp_accessibility_handler.h"
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

namespace {

constexpr char kMcpProtocolVersion[] = "2025-03-26";
constexpr char kServerName[] = "maho-browser";
constexpr char kServerVersion[] = "0.4.1";

MahoMcpBrowserDelegate* g_browser_delegate = nullptr;

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

}  // namespace

MahoMcpNetworkCaptureState::MahoMcpNetworkCaptureState() = default;
MahoMcpNetworkCaptureState::~MahoMcpNetworkCaptureState() = default;

void MahoMcpSession::SetBrowserDelegate(MahoMcpBrowserDelegate* delegate) {
  g_browser_delegate = delegate;
}

#if BUILDFLAG(IS_WIN)
MahoMcpSession::MahoMcpSession(MahoMcpSessionToken* session_token,
                               MahoMcpLeaseRegistry* lease_registry)
    : session_token_(session_token),
      lease_registry_(lease_registry),
      session_id_(base::Uuid::GenerateRandomV4().AsLowercaseString()),
      network_capture_state_(
          base::MakeRefCounted<MahoMcpNetworkCaptureState>()) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  TabInfo default_tab;
  default_tab.id = 0;
  default_tab.title = "New Tab";
  default_tab.url = "about:blank";
  default_tab.is_active = true;
  tabs_.push_back(std::move(default_tab));
  active_tab_id_ = 0;
}
#else
MahoMcpSession::MahoMcpSession(uid_t peer_uid,
                               MahoMcpLeaseRegistry* lease_registry)
    : peer_uid_(peer_uid),
      lease_registry_(lease_registry),
      session_id_(base::Uuid::GenerateRandomV4().AsLowercaseString()),
      network_capture_state_(
          base::MakeRefCounted<MahoMcpNetworkCaptureState>()) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  TabInfo default_tab;
  default_tab.id = 0;
  default_tab.title = "New Tab";
  default_tab.url = "about:blank";
  default_tab.is_active = true;
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

bool MahoMcpSession::IsHostAllowed(std::string_view host) const {
  if (host.empty() || allowed_domains_.empty()) {
    return false;
  }
  const std::string host_lc = base::ToLowerASCII(host);
  for (const std::string& entry : allowed_domains_) {
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
  const bool force_steal =
      arguments->FindBool("force_steal").value_or(false);
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
      t.Set("name", "browser_set_allowed_domains");
      t.Set("description",
            "Set the session-scoped allowlist of domains for "
            "browser_navigate and browser_tab_new(url=...). "
            "Supports wildcards like '*.example.com'.");
      tools.Append(base::Value(std::move(t)));
    }
    {
      base::DictValue t;
      t.Set("name", "browser_acquire_lease");
      t.Set("description",
            "Acquire exclusive lease on a tab. Args: tab_id (int), "
            "ttl_seconds (int, 10-300, default 60), force_steal "
            "(bool, default false).");
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
        base::DictValue tab;
        tab.Set("id", t.id);
        tab.Set("title", t.title);
        tab.Set("url", t.url);
        tab.Set("is_active", t.is_active);
        list.Append(std::move(tab));
      }
    } else {
      for (const auto& t : tabs_) {
        base::DictValue tab;
        tab.Set("id", t.id);
        tab.Set("title", t.title);
        tab.Set("url", t.url);
        tab.Set("is_active", t.is_active);
        list.Append(std::move(tab));
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
          result.Set("id", t.id);
          result.Set("title", t.title);
          result.Set("url", t.url);
          result.Set("is_active", t.is_active);
          found_real = true;
          break;
        }
      }
    }
    if (!found_real) {
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
      result.Set("id", found->id);
      result.Set("title", found->title);
      result.Set("url", found->url);
      result.Set("is_active", found->is_active);
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
    const GURL nav_url(url_str);
    if (!nav_url.is_valid()) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: url is not a valid GURL");
      responses.push_back(std::move(err));
      return;
    }
    // Reject host-less/dangerous schemes outright. data:, javascript:, file:,
    // chrome: and friends carry no host, so they can never be in
    // allowed_domains_; navigating to them would bypass the domain-approval
    // trust boundary that gates automated (MCP client) navigation.
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
    if (!IsHostAllowed(nav_url.host())) {
      base::DictValue data;
      data.Set("host", std::string(nav_url.host()));
      if (allowed_domains_.empty()) {
        data.Set("status", "domain_approval_required");
      } else {
        data.Set("status", "domain_not_allowed");
      }
      base::DictValue error;
      error.Set("code", -32000);
      error.Set("message",
                allowed_domains_.empty()
                    ? "Set allowed domains before navigating "
                      "(call browser_set_allowed_domains)"
                    : "Navigation blocked: host not in allowed_domains");
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
      g_browser_delegate->Navigate(active_tab_id_, nav_url);
    }
    
    // Update active tab URL
    for (auto& t : tabs_) {
      if (t.id == active_tab_id_) {
        t.url = url_str;
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

  if (*tool_name == "browser_set_allowed_domains") {
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
    allowed_domains_ = std::move(new_set);
    base::DictValue result;
    result.Set("count", static_cast<int>(allowed_domains_.size()));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
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
      closed = g_browser_delegate->CloseTab(target_id);
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
      if (!IsHostAllowed(nav_url.host())) {
        std::string err = framer_.BuildErrorResponse(
            msg.id, -32000,
            allowed_domains_.empty()
                ? "Set allowed domains before opening a new tab with url "
                  "(call browser_set_allowed_domains)"
                : "New tab blocked: host not in allowed_domains");
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
    int tab_id = arguments && arguments->FindInt("tab_id") ? arguments->FindInt("tab_id").value() : active_tab_id_;
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
    int tab_id = arguments && arguments->FindInt("tab_id") ? arguments->FindInt("tab_id").value() : active_tab_id_;
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
    int tab_id = arguments->FindInt("tab_id").value_or(active_tab_id_);
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
    int tab_id = arguments && arguments->FindInt("tab_id") ? arguments->FindInt("tab_id").value() : active_tab_id_;
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
    int tab_id = arguments->FindInt("tab_id").value_or(active_tab_id_);
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
    int tab_id = arguments->FindInt("tab_id").value_or(active_tab_id_);
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
    int tab_id = arguments->FindInt("tab_id").value_or(active_tab_id_);
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

  if (*tool_name == "page_wait_for_selector") {
    if (!arguments || !arguments->FindString("selector")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: selector required");
      responses.push_back(std::move(err));
      return;
    }
    int tab_id = arguments->FindInt("tab_id").value_or(active_tab_id_);
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
    int tab_id = arguments && arguments->FindInt("tab_id") ? arguments->FindInt("tab_id").value() : active_tab_id_;
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
    int tab_id = arguments->FindInt("tab_id").value_or(active_tab_id_);
    bool clicked = g_browser_delegate && g_browser_delegate->Click(tab_id, it->second);
    if (!clicked) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000, "Action failed: click could not be dispatched");
      responses.push_back(std::move(err));
      return;
    }
    base::DictValue result;
    result.Set("clicked", clicked);
    result.Set("ref", ref);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_type") {
    if (!arguments || !arguments->FindInt("ref") ||
        !arguments->FindString("text")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: ref and text required");
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
    int tab_id = arguments->FindInt("tab_id").value_or(active_tab_id_);
    const std::string& text = *arguments->FindString("text");
    bool typed = g_browser_delegate &&
                 g_browser_delegate->Type(tab_id, it->second, text);
    if (!typed) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000, "Action failed: type could not be dispatched");
      responses.push_back(std::move(err));
      return;
    }
    base::DictValue result;
    result.Set("typed", typed);
    result.Set("ref", ref);
    result.Set("text", text);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_select") {
    if (!arguments || !arguments->FindInt("ref") ||
        !arguments->FindString("value")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: ref and value required");
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
    int tab_id = arguments->FindInt("tab_id").value_or(active_tab_id_);
    const std::string& value = *arguments->FindString("value");
    bool selected = g_browser_delegate &&
                    g_browser_delegate->Select(tab_id, it->second, value);
    if (!selected) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32000, "Action failed: select could not be dispatched");
      responses.push_back(std::move(err));
      return;
    }
    base::DictValue result;
    result.Set("selected", selected);
    result.Set("ref", ref);
    result.Set("value", value);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_screenshot_full") {
    int tab_id = arguments && arguments->FindInt("tab_id") ? arguments->FindInt("tab_id").value() : active_tab_id_;
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
    int tab_id = arguments->FindInt("tab_id").value_or(active_tab_id_);
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

    // The delegate resolves an omitted (0) or explicit tab on the UI sequence
    // and reports the concrete bound tab id back through the callback. The
    // session no longer queries the tab list synchronously.
    const int requested_tab_id =
        (arguments && arguments->FindInt("tab_id"))
            ? arguments->FindInt("tab_id").value()
            : 0;

    const std::string capture_id =
        base::Uuid::GenerateRandomV4().AsLowercaseString();
    network_capture_state_->active = true;
    network_capture_state_->capture_id = capture_id;
    network_capture_state_->tab_id = requested_tab_id;
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
    int tab_id = arguments && arguments->FindInt("tab_id") ? arguments->FindInt("tab_id").value() : active_tab_id_;
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
    int tab_id = arguments && arguments->FindInt("tab_id") ? arguments->FindInt("tab_id").value() : active_tab_id_;
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
        g_browser_delegate->ActivateTab(target_id);
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
    
    int tab_id = arguments->FindInt("tab_id").value_or(active_tab_id_);
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
    if (!arguments || !arguments->FindString("direction")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: direction required");
      responses.push_back(std::move(err));
      return;
    }
    std::string direction = *arguments->FindString("direction");
    int pixels = arguments->FindInt("pixels").value_or(300);
    std::optional<int> ref = arguments->FindInt("ref");
    
    if (direction != "up" && direction != "down" && direction != "left" && direction != "right") {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: unknown direction");
      responses.push_back(std::move(err));
      return;
    }
    
    int tab_id = arguments->FindInt("tab_id").value_or(active_tab_id_);
    std::optional<ui::AXNodeID> ax_id;
    if (ref.has_value()) {
      auto it = ref_to_ax_id_.find(ref.value());
      if (it == ref_to_ax_id_.end()) {
        std::string err = framer_.BuildErrorResponse(
            msg.id, -32000, "Unknown ref: call browser_accessibility_snapshot first");
        responses.push_back(std::move(err));
        return;
      }
      ax_id = it->second;
    }
    
    if (g_browser_delegate) {
      g_browser_delegate->Scroll(tab_id, direction, pixels, ax_id);
    }
    
    int& current_scroll_y = scroll_y_positions_[tab_id];
    if (direction == "down") {
      current_scroll_y += pixels;
    } else if (direction == "up") {
      current_scroll_y = std::max(0, current_scroll_y - pixels);
    }
    
    base::DictValue result;
    result.Set("scrolled", true);
    result.Set("new_scroll_y", current_scroll_y);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_hover") {
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
    
    int tab_id = arguments->FindInt("tab_id").value_or(active_tab_id_);
    if (g_browser_delegate) {
      g_browser_delegate->Hover(tab_id, it->second);
    }
    
    base::DictValue result;
    result.Set("hovered", true);
    result.Set("ref", ref);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_key_press") {
    if (!arguments || !arguments->FindString("key")) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: key required");
      responses.push_back(std::move(err));
      return;
    }
    std::string key = *arguments->FindString("key");
    const base::ListValue* modifiers = arguments->FindList("modifiers");
    
    bool valid_key = (key.size() == 1) || 
                     (key == "Enter" || key == "Tab" || key == "Escape" || key == "Backspace" ||
                      key == "Delete" || key == "ArrowUp" || key == "ArrowDown" || key == "ArrowLeft" ||
                      key == "ArrowRight" || key == "Home" || key == "End" || key == "PageUp" ||
                      key == "PageDown" || (key[0] == 'F' && key.size() > 1 && key.size() <= 3));
    if (!valid_key) {
      std::string err = framer_.BuildErrorResponse(
          msg.id, -32602, "Invalid params: unknown key");
      responses.push_back(std::move(err));
      return;
    }
    
    std::vector<std::string> mod_list;
    if (modifiers) {
      for (const auto& mod : *modifiers) {
        if (!mod.is_string()) {
          std::string err = framer_.BuildErrorResponse(
              msg.id, -32602, "Invalid params: modifiers must be list of strings");
          responses.push_back(std::move(err));
          return;
        }
        std::string mod_str = mod.GetString();
        if (mod_str != "shift" && mod_str != "ctrl" && mod_str != "alt" && mod_str != "meta") {
          std::string err = framer_.BuildErrorResponse(
              msg.id, -32602, "Invalid params: unknown modifier: " + mod_str);
          responses.push_back(std::move(err));
          return;
        }
        mod_list.push_back(mod_str);
      }
    }
    
    int tab_id = arguments->FindInt("tab_id").value_or(active_tab_id_);
    if (g_browser_delegate) {
      g_browser_delegate->KeyPress(tab_id, key, mod_list);
    }
    
    base::DictValue result;
    result.Set("key_pressed", true);
    result.Set("key", key);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  std::string err = framer_.BuildErrorResponse(
      msg.id, -32601, "Method not found: unknown tool");
  responses.push_back(std::move(err));
}

}  // namespace maho
