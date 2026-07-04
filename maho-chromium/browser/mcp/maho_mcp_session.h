// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_SESSION_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_SESSION_H_

#include <memory>
#include <string>
#include <vector>

#if !BUILDFLAG(IS_WIN)
#include <sys/types.h>
#endif

#include "base/sequence_checker.h"
#include "build/build_config.h"
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
//
// Platform notes:
//   - POSIX: peer authentication via SO_PEERCRED / LOCAL_PEERCRED uid check.
//   - Windows: peer authentication handled externally by Named Pipe DACL;
//     the session trusts that the transport layer has already verified the peer.
class MahoMcpSession {
 public:
  enum class State {
    kAwaitingInitialize,
    kActive,
    kClosed,
  };

#if BUILDFLAG(IS_WIN)
  // On Windows, peer authentication is enforced by the Named Pipe DACL.
  // The session trusts that only authenticated peers can connect.
  MahoMcpSession();
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

  State state_ = State::kAwaitingInitialize;
#if !BUILDFLAG(IS_WIN)
  uid_t peer_uid_;
#endif
  MahoMcpJsonRpc framer_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_SESSION_H_
