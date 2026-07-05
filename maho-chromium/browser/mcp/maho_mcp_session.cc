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
#include "base/logging.h"
#include "base/values.h"

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
MahoMcpSession::MahoMcpSession(MahoMcpSessionToken* session_token)
    : session_token_(session_token) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}
#else
MahoMcpSession::MahoMcpSession(uid_t peer_uid) : peer_uid_(peer_uid) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}
#endif

MahoMcpSession::~MahoMcpSession() = default;

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
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
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
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
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
    base::DictValue result;
    result.Set("navigated", true);
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
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
    base::DictValue tab;
    tab.Set("id", 0);
    tab.Set("title", "New Tab");
    tab.Set("url", arguments && arguments->FindString("url")
                       ? *arguments->FindString("url")
                       : "about:blank");
    tab.Set("is_active", true);
    base::DictValue result;
    result.Set("tab", std::move(tab));
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_history_search") {
    base::DictValue result;
    result.Set("entries", base::ListValue());
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
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
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
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
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_page_content") {
    base::DictValue result;
    result.Set("text", "");
    result.Set("url", "");
    result.Set("title", "");
    result.Set("redacted", false);
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_page_text") {
    base::DictValue result;
    result.Set("text", "");
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
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
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  if (*tool_name == "browser_page_context") {
    base::DictValue result;
    result.Set("url", "");
    result.Set("title", "");
    result.Set("content", "");
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
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
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
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
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
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
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
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
    std::string resp =
        framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
    responses.push_back(std::move(resp));
    return;
  }

  std::string err = framer_.BuildErrorResponse(
      msg.id, -32601, "Method not found: unknown tool");
  responses.push_back(std::move(err));
}

}  // namespace maho
