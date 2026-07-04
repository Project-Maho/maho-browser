// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_session.h"

#include <unistd.h>

#include <string>
#include <vector>

#include "base/logging.h"
#include "base/values.h"

namespace maho {

namespace {

constexpr char kMcpProtocolVersion[] = "2025-03-26";
constexpr char kServerName[] = "maho-browser";
constexpr char kServerVersion[] = "0.4.1";

}  // namespace

MahoMcpSession::MahoMcpSession(uid_t peer_uid) : peer_uid_(peer_uid) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

MahoMcpSession::~MahoMcpSession() = default;

std::vector<std::string> MahoMcpSession::ProcessData(const std::string& data) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<std::string> responses;

  if (state_ == State::kClosed) {
    return responses;
  }

  // Check peer UID on first data.
  if (peer_uid_ != getuid()) {
    LOG(WARNING) << "MCP: Rejecting connection from UID " << peer_uid_
                 << " (browser UID: " << getuid() << ")";
    state_ = State::kClosed;
    return responses;
  }

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
  // Validate params exist.
  if (!msg.params.has_value() || !msg.params->is_dict()) {
    std::string err =
        framer_.BuildErrorResponse(msg.id, -32602, "Invalid params");
    responses.push_back(std::move(err));
    state_ = State::kClosed;
    return;
  }

  const auto& params = msg.params->GetDict();

  // Validate protocolVersion.
  const std::string* protocol_version = params.FindString("protocolVersion");
  if (!protocol_version || *protocol_version != kMcpProtocolVersion) {
    std::string err = framer_.BuildErrorResponse(
        msg.id, -32602, "Invalid params: unsupported protocolVersion");
    responses.push_back(std::move(err));
    state_ = State::kClosed;
    return;
  }

  // Build the initialize response.
  base::Value::Dict server_info;
  server_info.Set("name", kServerName);
  server_info.Set("version", kServerVersion);

  base::Value::Dict tools_cap;
  base::Value::Dict capabilities;
  capabilities.Set("tools", std::move(tools_cap));

  base::Value::Dict result;
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
    // Return empty tool list (Wave 2A adds real tools).
    base::Value::Dict result;
    result.Set("tools", base::Value::List());

    // Only respond if message has an id (not a notification).
    if (msg.id.has_value()) {
      std::string resp =
          framer_.BuildSuccessResponse(msg.id, base::Value(std::move(result)));
      responses.push_back(std::move(resp));
    }
    return;
  }

  if (msg.method == "tools/call") {
    // Stub: tool execution lands in Wave 2A.
    if (msg.id.has_value()) {
      std::string err =
          framer_.BuildErrorResponse(msg.id, -32601, "Method not found");
      responses.push_back(std::move(err));
    }
    return;
  }

  // Unknown method.
  if (msg.id.has_value()) {
    std::string err =
        framer_.BuildErrorResponse(msg.id, -32601, "Method not found");
    responses.push_back(std::move(err));
  }
}

}  // namespace maho
