// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_SESSION_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_SESSION_H_

#include <memory>
#include <string>
#include <vector>

#include <sys/types.h>

#include "base/sequence_checker.h"
#include "maho/browser/mcp/maho_mcp_json_rpc.h"

namespace maho {

// Per-connection MCP session state machine.
//
// States:
//   kAwaitingInitialize — only "initialize" method accepted.
//   kActive — normal operation; tool calls dispatched.
//   kClosed — terminal; no further processing.
//
// Thread-safety: all methods must be called on the same sequence (IO thread).
class MahoMcpSession {
 public:
  enum class State {
    kAwaitingInitialize,
    kActive,
    kClosed,
  };

  // |peer_uid| is the UID of the connected client (from SO_PEERCRED /
  // LOCAL_PEERCRED). The session will reject the handshake if the peer UID
  // does not match the browser's UID.
  explicit MahoMcpSession(uid_t peer_uid);
  ~MahoMcpSession();

  MahoMcpSession(const MahoMcpSession&) = delete;
  MahoMcpSession& operator=(const MahoMcpSession&) = delete;

  // Feed raw socket data. Returns any responses to write back (each is a
  // complete nd-JSON line). May transition state to kClosed.
  std::vector<std::string> ProcessData(const std::string& data);

  State state() const { return state_; }

 private:
  void HandleInitialize(const McpJsonRpcMessage& msg,
                        std::vector<std::string>& responses);
  void HandleActiveMethod(const McpJsonRpcMessage& msg,
                          std::vector<std::string>& responses);

  State state_ = State::kAwaitingInitialize;
  uid_t peer_uid_;
  MahoMcpJsonRpc framer_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_SESSION_H_
