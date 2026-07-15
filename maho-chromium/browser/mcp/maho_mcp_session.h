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
#include "maho/browser/mcp/maho_mcp_firewall.h"
#include "ui/accessibility/ax_node_id_forward.h"
#include "base/memory/ref_counted.h"

class GURL;

namespace content {
class WebContents;
}

namespace maho {

// R-8 non-revealing MCP target resolution.
//
// JSON-RPC error codes reserved for the private-context boundary. They are
// deliberately uniform across "explicit OTR id" and "unknown id" so that an
// attacker cannot distinguish an existing OTR tab from a non-existent one.
inline constexpr int kMahoMcpErrorTabNotFound = -32004;
inline constexpr int kMahoMcpErrorNoEligibleActiveTab = -32005;
inline constexpr int kMahoMcpErrorNoEligibleActiveBrowser = -32006;

inline constexpr char kMahoMcpMessageTabNotFound[] = "tab not found";
inline constexpr char kMahoMcpMessageNoEligibleActiveTab[] =
    "no eligible active tab";
inline constexpr char kMahoMcpMessageNoEligibleActiveBrowser[] =
    "no eligible active browser";

// Outcome of resolving a tool's target through the private-context policy.
enum class MahoMcpTargetError {
  kNone,
  kTabNotFound,              // explicit OTR or unknown id -> -32004
  kNoEligibleActiveTab,      // omitted/zero, no regular active tab -> -32005
  kNoEligibleActiveBrowser,  // profile/new-tab, no regular browser -> -32006
};

// A generation-bound, revalidatable resolution of an MCP tool target.
// `generation` monotonically increases on tab-strip membership/activation
// changes so a SessionID reused after close/restore fails revalidation.
struct ResolvedMahoMcpTarget {
  bool valid = false;
  int tab_id = 0;
  int browser_id = 0;
  int64_t generation = 0;
};

struct MahoMcpTargetResolution {
  ResolvedMahoMcpTarget target;
  MahoMcpTargetError error = MahoMcpTargetError::kNone;
};

class MahoMcpNetworkCaptureState : public base::RefCountedThreadSafe<MahoMcpNetworkCaptureState> {
 public:
  MahoMcpNetworkCaptureState();

  bool active = false;
  std::string capture_id;
  int tab_id = 0;
  // The resolved, generation-bound target this capture is pinned to. Stop and
  // get-HAR revalidate this same target/generation rather than re-resolving a
  // new active tab.
  ResolvedMahoMcpTarget target;

 private:
  friend class base::RefCountedThreadSafe<MahoMcpNetworkCaptureState>;
  ~MahoMcpNetworkCaptureState();
};

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

  // Set a sink for responses that complete asynchronously (e.g., screenshots
  // that must not block the UI thread). When invoked, the argument is a
  // complete nd-JSON response line. May be called from any sequence — the
  // sender implementation is responsible for hopping to the socket write
  // sequence. Not sequence-checked because it is set once at construction.
  using DeferredResponseSender = base::RepeatingCallback<void(std::string)>;
  void SetDeferredResponseSender(DeferredResponseSender sender) {
    deferred_response_sender_ = std::move(sender);
  }

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
  scoped_refptr<MahoMcpNetworkCaptureState> network_capture_state_;

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

  // R-8: target resolved for the in-flight tool call. Capture binds a copy of
  // it and revalidates (generation + kMCP) before success serialization.
  ResolvedMahoMcpTarget last_resolved_target_;

  DeferredResponseSender deferred_response_sender_;

  SEQUENCE_CHECKER(sequence_checker_);
};

class MahoMcpBrowserDelegate {
 public:
  virtual ~MahoMcpBrowserDelegate() = default;
  virtual std::vector<MahoMcpSession::TabInfo> GetTabList() = 0;
  virtual std::vector<MahoMcpSession::ConsoleMessage> GetConsoleMessages(int tab_id) = 0;
  virtual std::vector<MahoMcpSession::NavigationEvent> GetNavigationEvents(
      int tab_id, int64_t since_ms) = 0;
  virtual std::string GetPageText(int tab_id) = 0;
  virtual base::Value GetAccessibilitySnapshot(
      int tab_id,
      MahoMcpSession::RefTable* out_refs) = 0;

  struct PageContentResult {
    std::string text;
    std::string url;
    std::string title;
  };
  virtual PageContentResult GetPageContent(int tab_id) = 0;

  struct PageContextResult {
    std::string url;
    std::string title;
    std::string content;
  };
  virtual PageContextResult GetPageContext(int tab_id) = 0;

  struct SearchResult {
    int match_count = 0;
    int active_match_index = -1;
  };
  virtual SearchResult SearchInPage(int tab_id, const std::string& query) = 0;

  struct QuerySelectorResult {
    std::string ref_id;
    std::string tag;
  };
  virtual QuerySelectorResult QuerySelector(int tab_id, const std::string& selector) = 0;

  virtual std::string GetElementText(int tab_id, const std::string& ref_id) = 0;
  virtual std::string GetElementAttribute(int tab_id, const std::string& ref_id, const std::string& attribute) = 0;
  virtual bool WaitForSelector(int tab_id, const std::string& selector, int timeout_ms) = 0;

  virtual int CreateNewTab(const GURL& url) = 0;
  virtual bool CloseTab(int tab_id) = 0;

  struct BookmarkInfo {
    BookmarkInfo();
    ~BookmarkInfo();
    BookmarkInfo(const BookmarkInfo&);
    BookmarkInfo(BookmarkInfo&&) noexcept;
    BookmarkInfo& operator=(const BookmarkInfo&);
    BookmarkInfo& operator=(BookmarkInfo&&) noexcept;

    int64_t id = 0;
    std::string title;
    std::string url;
    std::string folder;
  };
  virtual std::vector<BookmarkInfo> SearchBookmarks(const std::string& query) = 0;
  virtual BookmarkInfo CreateBookmark(const std::string& title, const GURL& url, const std::string& folder) = 0;

  struct HistoryEntry {
    std::string url;
    std::string title;
    double visited_at = 0.0;  // Unix timestamp float (matches maho-core format)
  };
  virtual std::vector<HistoryEntry> SearchHistory(const std::string& query,
                                                  size_t max_results) = 0;

  // Async: base64-encoded PNG passed to `callback`. Empty string on failure.
  // Callback may be invoked on any sequence.
  virtual void CaptureFullPagePngBase64(
      int tab_id,
      base::OnceCallback<void(std::string)> callback) = 0;

  virtual void CaptureElementPngBase64(
      int tab_id,
      ui::AXNodeID ax_id,
      base::OnceCallback<void(std::string)> callback) = 0;

  virtual bool Scroll(int tab_id,
                       const std::string& direction,
                       int pixels,
                       std::optional<ui::AXNodeID> ax_id) = 0;
  virtual bool Click(int tab_id, ui::AXNodeID ax_id) = 0;
  virtual bool Type(int tab_id, ui::AXNodeID ax_id, const std::string& text) = 0;
  virtual bool Select(int tab_id,
                      ui::AXNodeID ax_id,
                      const std::string& value) = 0;
  virtual bool Hover(int tab_id, ui::AXNodeID ax_id) = 0;
  virtual bool KeyPress(int tab_id,
                        const std::string& key,
                        const std::vector<std::string>& modifiers) = 0;
  virtual bool ActivateTab(int tab_id) = 0;
  virtual bool Navigate(int tab_id, const GURL& url) = 0;
  virtual bool SetViewportSize(int tab_id, int width, int height) = 0;

  struct StartNetworkCaptureResult {
    bool success = false;
    int tab_id = 0;
  };
  using StartNetworkCaptureCallback =
      base::OnceCallback<void(StartNetworkCaptureResult)>;
  using StopNetworkCaptureCallback =
      base::OnceCallback<void(std::optional<Redacted<base::DictValue>>)>;

  virtual void StartNetworkCapture(
      const std::string& capture_id,
      int tab_id,
      StartNetworkCaptureCallback callback) = 0;
  virtual void StopNetworkCapture(
      const std::string& capture_id,
      StopNetworkCaptureCallback callback) = 0;
  virtual void CancelNetworkCapture(const std::string& capture_id) = 0;

  // Routines (FIX-4-browser): bridge the SAME maho_core routines engine over
  // MCP. `ListRoutines` returns a JSON array of built-in + custom routines.
  // `RunRoutine` runs by id asynchronously; the callback receives success plus
  // the engine result JSON, or a clean error string on failure (including the
  // Max-tier gate). Default impls keep these optional for test delegates.
  using RunRoutineCallback =
      base::OnceCallback<void(bool success, std::string content_or_error)>;
  virtual std::string ListRoutines();
  virtual void RunRoutine(const std::string& id, RunRoutineCallback callback);

  // R-8 non-revealing target resolution. |requested_tab_id| == 0 means the
  // caller omitted the id / passed 0 (active-tab request). Production
  // implementations filter OTR/ineligible targets through
  // MahoPrivateContextToken::Revalidate(kMCP) so an explicit OTR id is
  // indistinguishable from an unknown id (kTabNotFound). The default
  // implementation echoes the request as valid and is used only by test
  // delegates that do not model private contexts.
  virtual MahoMcpTargetResolution ResolveTabTarget(int requested_tab_id);
  virtual MahoMcpTargetResolution ResolveProfileTarget();
  // Revalidated immediately before a delegate mutation and before serializing
  // a synchronous or deferred success. Returns false when the bound
  // browser/tab/generation no longer authorizes MCP.
  virtual bool RevalidateTarget(const ResolvedMahoMcpTarget& target);
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_SESSION_H_
