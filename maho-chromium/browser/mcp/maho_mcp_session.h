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

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "maho/browser/mcp/maho_mcp_json_rpc.h"
#include "maho/browser/mcp/maho_mcp_lease_registry.h"
#include "maho/browser/mcp/maho_mcp_network_observer.h"
#include "ui/accessibility/ax_node_id_forward.h"

class GURL;

namespace content {
class WebContents;
}

namespace maho {

#if BUILDFLAG(IS_WIN)
class MahoMcpSessionToken;
#endif

class MahoMcpBrowserDelegate;

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
  static void SetBrowserDelegate(MahoMcpBrowserDelegate* delegate);
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

  struct ConsoleMessage {
    ConsoleMessage();
    ~ConsoleMessage();
    ConsoleMessage(const ConsoleMessage&);
    ConsoleMessage(ConsoleMessage&&) noexcept;
    ConsoleMessage& operator=(const ConsoleMessage&);
    ConsoleMessage& operator=(ConsoleMessage&&) noexcept;

    std::string level;
    std::string message;
    std::string source_url;
    int line = 0;
    int64_t timestamp_ms = 0;
  };

  struct TabInfo {
    TabInfo();
    ~TabInfo();
    TabInfo(const TabInfo&);
    TabInfo(TabInfo&&) noexcept;
    TabInfo& operator=(const TabInfo&);
    TabInfo& operator=(TabInfo&&) noexcept;

    int id = 0;
    std::string title;
    std::string url;
    bool is_active = false;
  };

  struct NavigationEvent {
    std::string url;
    std::string title;
    int status_code = 200;
    int64_t timestamp_ms = 0;
  };

  void AddConsoleMessageForTesting(int tab_id, ConsoleMessage message);

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

  // Per-session network capture observer (Wave 4.4).
  std::unique_ptr<MahoMcpNetworkObserver> network_observer_;

  // Map of tab_id (int) -> ring buffer of console messages
  std::unordered_map<int, std::vector<ConsoleMessage>> console_buffers_;

  // Tabs storage
  std::vector<TabInfo> tabs_;
  int active_tab_id_ = 0;

  // Viewport dimensions
  int viewport_width_ = 800;
  int viewport_height_ = 600;

  // Map of tab_id -> scroll_y
  std::unordered_map<int, int> scroll_y_positions_;

  SEQUENCE_CHECKER(sequence_checker_);
};

class MahoMcpBrowserDelegate {
 public:
  virtual ~MahoMcpBrowserDelegate() = default;
  virtual std::vector<MahoMcpSession::TabInfo> GetTabList() = 0;
  virtual std::vector<MahoMcpSession::ConsoleMessage> GetConsoleMessages(int tab_id) = 0;
  virtual std::vector<MahoMcpSession::NavigationEvent> GetNavigationEvents(
      int tab_id, int64_t since_ms) = 0;
  
  virtual bool Scroll(int tab_id,
                       const std::string& direction,
                       int pixels,
                       std::optional<ui::AXNodeID> ax_id) = 0;
  virtual bool Hover(int tab_id, ui::AXNodeID ax_id) = 0;
  virtual bool KeyPress(int tab_id,
                        const std::string& key,
                        const std::vector<std::string>& modifiers) = 0;
  virtual bool ActivateTab(int tab_id) = 0;
  virtual bool Navigate(int tab_id, const GURL& url) = 0;
  virtual bool SetViewportSize(int tab_id, int width, int height) = 0;
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_SESSION_H_
