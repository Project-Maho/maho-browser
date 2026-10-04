// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_JSON_RPC_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_JSON_RPC_H_

#include <optional>
#include <string>
#include <vector>

#include "base/sequence_checker.h"
#include "base/values.h"

namespace maho {

// A parsed JSON-RPC 2.0 message (request or notification).
struct McpJsonRpcMessage {
  McpJsonRpcMessage();
  ~McpJsonRpcMessage();
  McpJsonRpcMessage(McpJsonRpcMessage&&);
  McpJsonRpcMessage& operator=(McpJsonRpcMessage&&);

  std::string method;
  std::optional<base::Value> id;      // Absent for notifications.
  std::optional<base::Value> params;  // Object or array, may be absent.
};

// nd-JSON framer for JSON-RPC 2.0 over a byte stream.
//
// Read side: accumulate bytes, split on '\n', parse each complete line.
// Write side: serialize JSON-RPC response objects terminated by '\n'.
//
// Thread-safety: all methods must be called on the same sequence.
class MahoMcpJsonRpc {
 public:
  MahoMcpJsonRpc();
  ~MahoMcpJsonRpc();

  MahoMcpJsonRpc(const MahoMcpJsonRpc&) = delete;
  MahoMcpJsonRpc& operator=(const MahoMcpJsonRpc&) = delete;

  // Append raw bytes from the socket into the internal buffer.
  void AppendData(const std::string& data);

  // Inert work/retention observations; never used for admission decisions.
  size_t buffered_bytes_for_testing() const { return buffer_.size(); }
  size_t scanned_bytes_for_testing() const { return scanned_bytes_for_testing_; }
  bool frame_limit_exceeded() const { return frame_limit_exceeded_; }

  // Take the next auto-generated error response (e.g., parse error,
  // invalid request detected during framing). Returns std::nullopt if none.
  std::optional<std::string> TakeNextResponse();

  // Take the next successfully parsed message. Returns std::nullopt if
  // no complete messages are available.
  std::optional<McpJsonRpcMessage> TakeNextParsedMessage();

  // Build a JSON-RPC 2.0 success response string (terminated by '\n').
  std::string BuildSuccessResponse(const std::optional<base::Value>& id,
                                   base::Value result);

  // Build a JSON-RPC 2.0 error response string (terminated by '\n').
  std::string BuildErrorResponse(const std::optional<base::Value>& id,
                                 int code,
                                 const std::string& message);

  // Same as above, with a JSON-RPC `error.data` payload for structured
  // diagnostics that clients can consume without parsing the message text.
  std::string BuildErrorResponse(const std::optional<base::Value>& id,
                                 int code,
                                 const std::string& message,
                                 base::Value data);

 private:
  void ProcessLine(const std::string& line);

  std::string buffer_;
  bool frame_limit_exceeded_ = false;
  size_t scanned_bytes_for_testing_ = 0;
  std::vector<std::string> pending_responses_;
  std::vector<McpJsonRpcMessage> pending_messages_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_JSON_RPC_H_
