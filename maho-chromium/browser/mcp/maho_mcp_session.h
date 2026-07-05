// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_SESSION_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_SESSION_H_

#include <memory>
#include <string>
#include <vector>

#include "build/build_config.h"

#if !BUILDFLAG(IS_WIN)
#include <sys/types.h>
#endif

#include "base/memory/raw_ptr.h"
#include "base/sequence_checker.h"
#include "maho/browser/mcp/maho_mcp_json_rpc.h"

namespace maho {

#if BUILDFLAG(IS_WIN)
class MahoMcpSessionToken;
#endif

// Per-connection MCP session state machine.
//
// States:
//   kAwaitingInitialize — only "initialize" method accepted.
//   kActive — normal operation; tool calls dispatched.
//   kClosed — terminal; no further processing.
//
// Thread-safety: all methods must be called on the same sequence (IO thread).
//
// Platform notes:
//   - POSIX: peer authentication via SO_PEERCRED / LOCAL_PEERCRED uid check.
//   - Windows: peer authentication via Named Pipe DACL (transport layer) +
//     DPAPI session token validation during initialize handshake.
class MahoMcpSession {
 public:
  enum class State {
    kAwaitingInitialize,
    kActive,
    kClosed,
  };

#if BUILDFLAG(IS_WIN)
  // On Windows, constructs a session that validates the client's auth token
  // against |session_token| during the initialize handshake. The pointer must
  // outlive this session. Pass nullptr to skip token validation (testing only).
  explicit MahoMcpSession(MahoMcpSessionToken* session_token);
#else
  // |peer_uid| is the UID of the connected client (from SO_PEERCRED /
  // LOCAL_PEERCRED). The session will reject the handshake if the peer UID
  // does not match the browser's UID.
  explicit MahoMcpSession(uid_t peer_uid);
#endif
  ~MahoMcpSession();

  MahoMcpSession(const MahoMcpSession&) = delete;
  MahoMcpSession& operator=(const MahoMcpSession&) = delete;

  // Feed raw socket/pipe data. Returns any responses to write back (each is a
  // complete nd-JSON line). May transition state to kClosed.
  std::vector<std::string> ProcessData(const std::string& data);

  State state() const { return state_; }

 private:
  void HandleInitialize(const McpJsonRpcMessage& msg,
                        std::vector<std::string>& responses);
  void HandleActiveMethod(const McpJsonRpcMessage& msg,
                          std::vector<std::string>& responses);
  void HandleToolCall(const McpJsonRpcMessage& msg,
                      std::vector<std::string>& responses);

  State state_ = State::kAwaitingInitialize;
#if BUILDFLAG(IS_WIN)
  raw_ptr<MahoMcpSessionToken> session_token_;
#else
  uid_t peer_uid_;
#endif
  MahoMcpJsonRpc framer_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_SESSION_H_
