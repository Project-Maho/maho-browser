// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_json_rpc.h"

#include <string>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/values.h"
#include "maho/browser/mcp/maho_mcp_firewall.h"

namespace maho {

McpJsonRpcMessage::McpJsonRpcMessage() = default;
McpJsonRpcMessage::~McpJsonRpcMessage() = default;
McpJsonRpcMessage::McpJsonRpcMessage(McpJsonRpcMessage&&) = default;
McpJsonRpcMessage& McpJsonRpcMessage::operator=(McpJsonRpcMessage&&) = default;

namespace {

constexpr size_t kMaxFrameBytes = 1024 * 1024;

std::string SerializeResponse(base::DictValue response) {
  response.Set("jsonrpc", "2.0");
  std::string output;
  base::JSONWriter::Write(base::Value(std::move(response)), &output);
  output.push_back('\n');
  return output;
}

base::Value CloneId(const std::optional<base::Value>& id) {
  if (id.has_value()) {
    return id->Clone();
  }
  return base::Value();
}

}  // namespace

MahoMcpJsonRpc::MahoMcpJsonRpc() {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}
MahoMcpJsonRpc::~MahoMcpJsonRpc() = default;

void MahoMcpJsonRpc::AppendData(const std::string& data) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (frame_limit_exceeded_) {
    return;
  }
  size_t offset = 0;
  while (offset < data.size()) {
    const size_t delimiter = data.find('\n', offset);
    const bool complete = delimiter != std::string::npos;
    const size_t bytes = (complete ? delimiter : data.size()) - offset;
    scanned_bytes_for_testing_ += bytes + (complete ? 1 : 0);
    // Always reserve the delimiter, including while a frame is incomplete.
    if (bytes >= kMaxFrameBytes - buffer_.size()) {
      frame_limit_exceeded_ = true;
      std::string().swap(buffer_);
      pending_responses_.push_back(
          BuildErrorResponse(std::nullopt, -32600, "frame_too_large"));
      return;
    }
    buffer_.append(data, offset, bytes);
    if (!complete) {
      return;
    }
    if (!buffer_.empty()) {
      ProcessLine(buffer_);
      buffer_.clear();
    }
    offset = delimiter + 1;
  }
}

std::optional<std::string> MahoMcpJsonRpc::TakeNextResponse() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (pending_responses_.empty()) {
    return std::nullopt;
  }
  std::string response = std::move(pending_responses_.front());
  pending_responses_.erase(pending_responses_.begin());
  return response;
}

std::optional<McpJsonRpcMessage> MahoMcpJsonRpc::TakeNextParsedMessage() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (pending_messages_.empty()) {
    return std::nullopt;
  }
  McpJsonRpcMessage msg = std::move(pending_messages_.front());
  pending_messages_.erase(pending_messages_.begin());
  return msg;
}

std::string MahoMcpJsonRpc::BuildSuccessResponse(
    const std::optional<base::Value>& id,
    base::Value result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // 4-vector credential firewall (OQ-4): every tool response passes through
  // MahoMcpFirewall::RedactAll before egress. Single chokepoint = no bypass.
  MahoMcpFirewall::RedactAll(result);
  base::DictValue response;
  response.Set("id", CloneId(id));
  response.Set("result", std::move(result));
  return SerializeResponse(std::move(response));
}

std::string MahoMcpJsonRpc::BuildErrorResponse(
    const std::optional<base::Value>& id,
    int code,
    const std::string& message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::DictValue error;
  error.Set("code", code);
  error.Set("message", message);

  base::DictValue response;
  response.Set("id", CloneId(id));
  response.Set("error", std::move(error));
  return SerializeResponse(std::move(response));
}

std::string MahoMcpJsonRpc::BuildErrorResponse(
    const std::optional<base::Value>& id,
    int code,
    const std::string& message,
    base::Value data) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::DictValue error;
  error.Set("code", code);
  error.Set("message", message);
  if (!data.is_none()) {
    error.Set("data", std::move(data));
  }

  base::DictValue response;
  response.Set("id", CloneId(id));
  response.Set("error", std::move(error));
  return SerializeResponse(std::move(response));
}

void MahoMcpJsonRpc::ProcessLine(const std::string& line) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(line, base::JSON_PARSE_RFC);
  if (!parsed.has_value() || !parsed->is_dict()) {
    // -32700 Parse error.
    base::DictValue error_response;
    error_response.Set("id", base::Value());
    base::DictValue error;
    error.Set("code", -32700);
    error.Set("message", "Parse error");
    error_response.Set("error", std::move(error));
    pending_responses_.push_back(SerializeResponse(std::move(error_response)));
    return;
  }

  auto& dict = parsed->GetDict();

  // Validate "jsonrpc" field.
  const std::string* jsonrpc = dict.FindString("jsonrpc");
  if (!jsonrpc || *jsonrpc != "2.0") {
    // Extract id if present for the error response.
    std::optional<base::Value> id;
    if (auto* id_val = dict.Find("id")) {
      id = id_val->Clone();
    }
    base::DictValue error_response;
    error_response.Set("id", CloneId(id));
    base::DictValue error;
    error.Set("code", -32600);
    error.Set("message", "Invalid Request");
    error_response.Set("error", std::move(error));
    pending_responses_.push_back(SerializeResponse(std::move(error_response)));
    return;
  }

  // Extract method.
  const std::string* method = dict.FindString("method");
  if (!method) {
    std::optional<base::Value> id;
    if (auto* id_val = dict.Find("id")) {
      id = id_val->Clone();
    }
    base::DictValue error_response;
    error_response.Set("id", CloneId(id));
    base::DictValue error;
    error.Set("code", -32600);
    error.Set("message", "Invalid Request");
    error_response.Set("error", std::move(error));
    pending_responses_.push_back(SerializeResponse(std::move(error_response)));
    return;
  }

  // Build the parsed message.
  McpJsonRpcMessage msg;
  msg.method = *method;

  // Extract id (may be absent for notifications).
  if (auto* id_val = dict.Find("id")) {
    msg.id = id_val->Clone();
  }

  // Extract params (may be absent).
  if (auto* params_val = dict.Find("params")) {
    msg.params = params_val->Clone();
  }

  pending_messages_.push_back(std::move(msg));
}

}  // namespace maho
