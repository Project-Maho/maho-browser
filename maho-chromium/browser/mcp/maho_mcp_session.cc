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
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "base/values.h"
#include "maho/browser/mcp/maho_mcp_firewall.h"
#include "maho/browser/mcp/maho_mcp_lease_registry.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_WIN)
#include "maho/browser/mcp/maho_mcp_session_token_win.h"
#endif

namespace maho {

namespace {

constexpr char kMcpProtocolVersion[] = "2025-03-26";
constexpr char kServerName[] = "maho-browser";
constexpr char kServerVersion[] = "0.4.1";

}  // namespace

#if BUILDFLAG(IS_WIN)
MahoMcpSession::MahoMcpSession(MahoMcpSessionToken* session_token,
                               MahoMcpLeaseRegistry* lease_registry)
    : session_token_(session_token),
      lease_registry_(lease_registry),
      session_id_(base::Uuid::GenerateRandomV4().AsLowercaseString()) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}
#else
MahoMcpSession::MahoMcpSession(uid_t peer_uid,
                               MahoMcpLeaseRegistry* lease_registry)
    : peer_uid_(peer_uid),
      lease_registry_(lease_registry),
      session_id_(base::Uuid::GenerateRandomV4().AsLowercaseString()) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}
#endif

MahoMcpSession::~MahoMcpSession() = default;

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

    if (!session_token_->Matches(base::make_span(*decoded))) {
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

  if (*tool_name == "browser_tab_list") {
    base::DictValue result;
    result.Set("tabs", base::ListValue());
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
    base::DictValue result;
    result.Set("id", arguments->FindInt("tab_id").value());
    result.Set("title", "");
    result.Set("url", "");
    result.Set("is_active", false);
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
    base::DictValue tab;
    tab.Set("id", 0);
    tab.Set("title", "New Tab");
    tab.Set("url", arguments && arguments->FindString("url")
                       ? *arguments->FindString("url")
                       : "about:blank");
    tab.Set("is_active", true);
    base::DictValue result;
    result.Set("tab", std::move(tab));
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_history_search") {
    base::DictValue result;
    result.Set("entries", base::ListValue());
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
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
    base::DictValue result;
    result.Set("bookmarks", base::ListValue());
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
    base::DictValue bookmark;
    bookmark.Set("id", 0);
    bookmark.Set("title", *arguments->FindString("title"));
    bookmark.Set("url", *arguments->FindString("url"));
    const std::string* folder = arguments->FindString("folder");
    if (folder) {
      bookmark.Set("folder", *folder);
    }
    base::DictValue result;
    result.Set("bookmark", std::move(bookmark));
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_page_content") {
    base::DictValue result;
    result.Set("text", "");
    result.Set("url", "");
    result.Set("title", "");
    result.Set("redacted", false);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_page_text") {
    base::DictValue result;
    result.Set("text", "");
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
    base::DictValue result;
    result.Set("match_count", 0);
    result.Set("active_match_index", -1);
    auto redacted = MahoMcpFirewall::Wrap(base::Value(std::move(result)));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, redacted.get().Clone());
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_page_context") {
    base::DictValue result;
    result.Set("url", "");
    result.Set("title", "");
    result.Set("content", "");
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
    base::DictValue result;
    result.Set("ref_id", "ref_stub_0");
    result.Set("tag", "div");
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
    base::DictValue result;
    result.Set("text", "");
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
    base::DictValue result;
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
    base::DictValue result;
    result.Set("found", false);
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
