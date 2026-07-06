// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_SESSION_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_SESSION_H_

#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "build/build_config.h"

#if !BUILDFLAG(IS_WIN)
#include <sys/types.h>
#endif

#include "base/memory/raw_ptr.h"
#include "base/sequence_checker.h"
#include "maho/browser/mcp/maho_mcp_json_rpc.h"
#include "maho/browser/mcp/maho_mcp_lease_registry.h"
#include "ui/accessibility/ax_node_id_forward.h"

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
  // against |session_token| during the initialize handshake. |lease_registry|
  // may be nullptr for testing; otherwise it must outlive this session.
  explicit MahoMcpSession(MahoMcpSessionToken* session_token,
                          MahoMcpLeaseRegistry* lease_registry = nullptr);
#else
  // |peer_uid| is the UID of the connected client. |lease_registry| may be
  // nullptr for testing; otherwise it must outlive this session.
  explicit MahoMcpSession(uid_t peer_uid,
                          MahoMcpLeaseRegistry* lease_registry = nullptr);
#endif
  ~MahoMcpSession();

  MahoMcpSession(const MahoMcpSession&) = delete;
  MahoMcpSession& operator=(const MahoMcpSession&) = delete;

  // Feed raw socket/pipe data. Returns any responses to write back (each is a
  // complete nd-JSON line). May transition state to kClosed.
  std::vector<std::string> ProcessData(const std::string& data);

  State state() const { return state_; }

  // Ref table for accessibility snapshot @ref labels.
  // Populated by browser_accessibility_snapshot, consumed by
  // browser_click/browser_type/browser_select.
  using RefTable = std::unordered_map<int, ui::AXNodeID>;
  RefTable& ref_table() { return ref_to_ax_id_; }
  const RefTable& ref_table() const { return ref_to_ax_id_; }

 private:
  void HandleInitialize(const McpJsonRpcMessage& msg,
                        std::vector<std::string>& responses);
  void HandleActiveMethod(const McpJsonRpcMessage& msg,
                          std::vector<std::string>& responses);
  void HandleToolCall(const McpJsonRpcMessage& msg,
                      std::vector<std::string>& responses);

  bool IsHostAllowed(std::string_view host) const;

  bool HandleAcquireLease(const McpJsonRpcMessage& msg,
                          const base::DictValue* arguments,
                          std::vector<std::string>& responses);
  bool HandleHeartbeatLease(const McpJsonRpcMessage& msg,
                            const base::DictValue* arguments,
                            std::vector<std::string>& responses);
  bool HandleReleaseLease(const McpJsonRpcMessage& msg,
                          const base::DictValue* arguments,
                          std::vector<std::string>& responses);

  State state_ = State::kAwaitingInitialize;
#if BUILDFLAG(IS_WIN)
  raw_ptr<MahoMcpSessionToken> session_token_;
#else
  uid_t peer_uid_;
#endif
  raw_ptr<MahoMcpLeaseRegistry> lease_registry_;
  std::string session_id_;
  MahoMcpJsonRpc framer_;

  std::set<std::string> allowed_domains_;

  // Per-session ref table for accessibility interactions.
  RefTable ref_to_ax_id_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_SESSION_H_
