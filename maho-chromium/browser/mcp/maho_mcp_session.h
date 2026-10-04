// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAHO_BROWSER_MCP_MAHO_MCP_SESSION_H_
#define MAHO_BROWSER_MCP_MAHO_MCP_SESSION_H_

#include <memory>
#include <optional>
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
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "maho/browser/ai/maho_browser_tool_registry.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "maho/browser/ai/maho_mail_tool_authorization.h"
#include "maho/browser/mcp/maho_mcp_accessibility_handler.h"
#include "maho/browser/mcp/maho_mcp_capability_registry.h"
#include "maho/browser/mcp/maho_mcp_firewall.h"
#include "maho/browser/mcp/maho_mcp_input_synthesizer.h"
#include "maho/browser/mcp/maho_mcp_json_rpc.h"
#include "maho/browser/mcp/maho_mcp_lease_registry.h"
#include "maho/browser/mcp/maho_mcp_network_observer.h"
#include "ui/accessibility/ax_node_id_forward.h"

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
// Todo 6: an input/navigation mutation was attempted without an active tab
// lease held by this session. Uniform, non-revealing.
inline constexpr int kMahoMcpErrorLeaseRequired = -32007;
inline constexpr int kMahoMcpErrorApprovalDenied = -32008;
inline constexpr int kMahoMcpErrorStaleReference = -32009;
inline constexpr int kMahoMcpErrorActionNotApplied = -32010;
inline constexpr int kMahoMcpErrorNativeInputUnavailable = -32011;
// Tab-binding contract: an input/navigation mutation arrived without an
// explicit tab_id. Automation controllers must name their target tab so two
// concurrent sessions can never dispatch into whichever tab is focused.
inline constexpr int kMahoMcpErrorTabBindingRequired = -32013;

inline constexpr char kMahoMcpMessageTabNotFound[] = "tab not found";
inline constexpr char kMahoMcpMessageNoEligibleActiveTab[] =
    "no eligible active tab";
inline constexpr char kMahoMcpMessageNoEligibleActiveBrowser[] =
    "no eligible active browser";
inline constexpr char kMahoMcpMessageStaleReference[] =
    "accessibility snapshot target changed; refs are session-scoped - capture "
    "a new snapshot (browser_snapshot) in this session, then retry with the "
    "new ref";
inline constexpr char kMahoMcpMessageActionNotApplied[] =
    "action dispatched but not applied";
inline constexpr char kMahoMcpMessageNativeInputUnavailable[] =
    "native input unavailable";
inline constexpr char kMahoMcpMessageTabBindingRequired[] =
    "tab binding required for this mutation: pass an explicit tab_id";

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

// Real capture-time geometry produced alongside a screenshot PNG. The browser
// delegate fills this at the moment the surface is copied, so a visual frame
// can only ever be built from geometry a real capture observed. Size fields:
// |bitmap_size| is physical pixels of the captured surface, |viewport_css_size|
// is the renderer's visible viewport in CSS pixels, |view_bounds_in_screen| is
// the view bounds in screen DIPs at capture time.
struct MahoMcpCaptureMetrics {
  MahoMcpCaptureMetrics();
  ~MahoMcpCaptureMetrics();
  MahoMcpCaptureMetrics(const MahoMcpCaptureMetrics&);
  MahoMcpCaptureMetrics& operator=(const MahoMcpCaptureMetrics&);
  MahoMcpCaptureMetrics(MahoMcpCaptureMetrics&&) noexcept;
  MahoMcpCaptureMetrics& operator=(MahoMcpCaptureMetrics&&) noexcept;

  int tab_id = 0;
  gfx::Size bitmap_size;
  gfx::SizeF viewport_css_size;
  gfx::Rect view_bounds_in_screen;
  float device_scale_factor = 1.0f;
  gfx::PointF visual_viewport_offset;
  float page_zoom_factor = 1.0f;
  float page_scale_factor = 1.0f;
  // No live lookups are wired for these today (lease epoch is the only live
  // epoch compared by visual-action validation), so captures carry constant 1.
  uint64_t document_epoch = 1;
  uint64_t view_transform_generation = 1;
};

enum class MahoMcpActivityPhase {
  kReading,
  kActing,
  kCompleted,
  kFailed,
  kDisconnected,
};

// Trusted, browser/AX-derived classification metadata for one input field,
// resolved from the accessibility tree — never from field-name strings the
// model supplies in tool-call arguments. browser_type consults it to refuse
// credential/one-time-code/recovery entry before any text reaches the page.
struct MahoMcpFieldMetadata {
  // False until a delegate has actually inspected the field's AX node. An
  // unclassified field is treated as credential-bearing (fail closed), so a
  // delegate that omits GetFieldMetadata cannot silently type into a secret.
  bool classified = false;
  // AX State::kProtected: the renderer's masked-input bit (type=password).
  bool is_protected = false;
  // AX kAutoComplete token, lowercased (e.g. "current-password",
  // "one-time-code").
  std::string autocomplete;
  // AX accessible name/label, lowercased; supplementary OTP/recovery signal.
  std::string accessible_name;
};

// The browser-side input implementation reports whether a mutation reached
// the renderer and whether its AX postcondition proves the requested result.
// `verified` is absent only for inherently unverifiable actions (for example,
// a generic button click); it is never inferred from dispatch alone.
struct InputActionOutcome {
  bool dispatched = false;
  std::optional<bool> verified;
  std::string method;
  std::string reason;
};

class MahoMcpNetworkCaptureState
    : public base::RefCountedThreadSafe<MahoMcpNetworkCaptureState> {
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

class MahoMcpControllerState;

#if BUILDFLAG(IS_WIN)
class MahoMcpSessionToken;
#endif

class MahoMcpBrowserDelegate;

struct MahoMcpFeatureGates {
  bool mail_enabled = false;
  bool routines_enabled = false;
  bool vault_enabled = false;
  bool native_input_enabled = false;
};

// Browser-validated identity class for a transport connection. Known Maho
// labels are selected only when the authenticated peer executable matches.
enum class MahoMcpControllerKind {
  kMahoCli,
  kMahoCliRepl,
  kMahoBrowserMcp,
  kThirdParty,
};

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
  static MahoMcpBrowserDelegate* GetBrowserDelegateForBrowserActions();
  static void SetLeaseRegistryForBrowserActions(
      MahoMcpLeaseRegistry* lease_registry);
  static MahoMcpLeaseRegistry* GetLeaseRegistryForBrowserActions();
  static void RevokeControllerSession(std::string_view session_id);

  // Returns true when |meta| identifies a credential-class field (password,
  // current-password, one-time-code/TOTP, or recovery) per trusted AX signals.
  // Pure and side-effect free so it is unit-tested in isolation.
  static bool IsCredentialField(const MahoMcpFieldMetadata& meta);

  enum class State {
    kAwaitingInitialize,
    kActive,
    kClosed,
  };

  enum class HybridState {
    kFast,
    kVisualPending,
    kVisualActive,
  };

  static const char* HybridStateToString(HybridState state);

  HybridState hybrid_state() const { return hybrid_state_; }
  int hybrid_strikes() const { return hybrid_strikes_; }
  bool hybrid_probe_required() const { return hybrid_probe_required_; }
  const std::string& hybrid_document_url() const { return hybrid_document_url_; }
  int hybrid_target_tab_id() const { return hybrid_target_tab_id_; }

  void RecordActionStrike(bool is_sentinel = false);
  void RecordActionSuccess(bool is_native = false);
  void ApproveVisualFallback();
  void DenyVisualFallback();
  void RecordReplacementDocument(int tab_id, const std::string& new_url);
  void RecordSameDocumentChange();
  bool ExecuteProbe(bool probe_succeeded);
  void ResetHybridState();
  void SetHybridTargetTab(int tab_id, int64_t generation = 0);
  void OnLeaseReleased(int tab_id);

  NativeInputAvailability CheckNativeInputAvailability() const;
  void SetNativeInputAvailabilityOverrideForTesting(
      std::optional<NativeInputAvailability> override_val) {
    native_input_availability_override_ = std::move(override_val);
  }
  void SetNativeInputDispatcherForTesting(NativeInputDispatcher* dispatcher) {
    native_dispatcher_override_ = dispatcher;
  }
  void SetVisualFrameForTesting(VisualFrame frame) {
    last_visual_frame_ = std::move(frame);
  }
  void SetFeatureGatesOverrideForTesting(
      std::optional<MahoMcpFeatureGates> override_gates) {
    feature_gates_override_ = std::move(override_gates);
  }
  NativeInputDispatcher* GetNativeDispatcher() const;
  MahoMcpFeatureGates GetEffectiveFeatureGates() const;

#if BUILDFLAG(IS_WIN)
  // On Windows, constructs a session that validates the client's auth token
  // against |session_token| during the initialize handshake. |lease_registry|
  // may be nullptr for testing; otherwise it must outlive this session.
  explicit MahoMcpSession(MahoMcpSessionToken* session_token,
                          MahoMcpLeaseRegistry* lease_registry = nullptr,
                          std::string peer_executable = std::string(),
                          bool peer_is_trusted_maho = false);
#else
  // |peer_uid| is the UID of the connected client. |lease_registry| may be
  // nullptr for testing; otherwise it must outlive this session.
  explicit MahoMcpSession(uid_t peer_uid,
                          MahoMcpLeaseRegistry* lease_registry = nullptr,
                          std::string peer_executable = std::string(),
                          bool peer_is_trusted_maho = false);
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

  // Unguessable per-connection id used as the lease-holder key. Exposed so the
  // socket/pipe server can notify the lease registry when this connection
  // drops (OnSessionDropped), keeping disconnect semantics truthful.
  const std::string& session_id() const { return session_id_; }
  const std::string& controller_display_label() const {
    return controller_display_label_;
  }
  MahoMcpControllerKind controller_kind() const { return controller_kind_; }
  bool autonomous() const { return autonomous_; }

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
    std::string stable_id;
    std::string title;
    std::string url;
    bool is_active = false;
    bool targetable = false;
    int tab_strip_index = -1;
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

  MahoMcpAccessibilityHandler::ObservationCache& observation_cache() {
    return *observation_cache_;
  }
  const MahoMcpAccessibilityHandler::ObservationCache& observation_cache()
      const {
    return *observation_cache_;
  }

 private:
  // Visual-frame capture path: minted only from a real delegate capture that
  // carried real geometry. See MahoMcpCaptureMetrics and the screenshot_full
  // tool handler.
  void OnFullScreenshotCaptured(
      std::optional<base::Value> message_id,
      DeferredResponseSender deferred_sender,
      std::string b64,
      std::optional<MahoMcpCaptureMetrics> metrics);
  void BindVisualFrameFromCapture(const std::string& png_b64,
                                  const MahoMcpCaptureMetrics& metrics);

  void HandleInitialize(const McpJsonRpcMessage& msg,
                        std::vector<std::string>& responses);
  void HandleActiveMethod(const McpJsonRpcMessage& msg,
                          std::vector<std::string>& responses);
  void HandleControlList(const McpJsonRpcMessage& msg,
                         std::vector<std::string>& responses);
  void HandleControlCall(const McpJsonRpcMessage& msg,
                         std::vector<std::string>& responses);
  void HandleToolCall(const McpJsonRpcMessage& msg,
                      std::vector<std::string>& responses,
                      bool control_plane = false);

  bool IsHostBlocked(std::string_view host) const;

  bool HandleAcquireLease(const McpJsonRpcMessage& msg,
                          const base::DictValue* arguments,
                          int target_tab_id,
                          std::vector<std::string>& responses);
  bool HandleHeartbeatLease(const McpJsonRpcMessage& msg,
                            const base::DictValue* arguments,
                            int target_tab_id,
                            std::vector<std::string>& responses);
  bool HandleReleaseLease(const McpJsonRpcMessage& msg,
                          const base::DictValue* arguments,
                          int target_tab_id,
                          std::vector<std::string>& responses);
  bool HandleTabBorrow(const McpJsonRpcMessage& msg,
                       const base::DictValue* arguments,
                       int target_tab_id,
                       std::vector<std::string>& responses);
  bool HandleTabReturn(const McpJsonRpcMessage& msg,
                       const base::DictValue* arguments,
                       int target_tab_id,
                       std::vector<std::string>& responses);

  State state_ = State::kAwaitingInitialize;
#if BUILDFLAG(IS_WIN)
  raw_ptr<MahoMcpSessionToken> session_token_;
#else
  uid_t peer_uid_;
#endif
  raw_ptr<MahoMcpLeaseRegistry> lease_registry_;
  std::string session_id_;
  scoped_refptr<MahoMcpControllerState> controller_state_;
  std::string peer_executable_;
  bool peer_is_trusted_maho_ = false;
  MahoMcpControllerKind controller_kind_ = MahoMcpControllerKind::kThirdParty;
  bool autonomous_ = false;
  std::string controller_display_label_ = "External MCP";
  MahoMcpJsonRpc framer_;

  std::unique_ptr<MahoMcpCapabilityRegistry> capability_registry_;
  std::set<std::string> blocked_domains_;

  // Per-session ref table for accessibility interactions.
  RefTable ref_to_ax_id_;
  ResolvedMahoMcpTarget ref_table_target_;
  uint64_t ref_snapshot_token_ = 0;

  // Observation cache for V2 snapshots.
  std::shared_ptr<MahoMcpAccessibilityHandler::ObservationCache>
      observation_cache_ =
          std::make_shared<MahoMcpAccessibilityHandler::ObservationCache>();
  uint64_t next_v2_snapshot_token_seq_ = 0;

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

  // Hybrid state machine (Tier 1 fast engine -> Tier 2 visual fallback).
  HybridState hybrid_state_ = HybridState::kFast;
  int hybrid_strikes_ = 0;
  int hybrid_target_tab_id_ = 0;
  int64_t hybrid_target_generation_ = 0;
  std::string hybrid_document_url_;
  bool hybrid_probe_required_ = false;
  std::optional<NativeInputAvailability> native_input_availability_override_;
  raw_ptr<NativeInputDispatcher> native_dispatcher_override_{nullptr};
  std::optional<VisualFrame> last_visual_frame_;
  std::optional<MahoMcpFeatureGates> feature_gates_override_;

  // R-8: target resolved for the in-flight tool call. Capture binds a copy of
  // it and revalidates (generation + kMCP) before success serialization.
  ResolvedMahoMcpTarget last_resolved_target_;
  uint64_t activity_revision_ = 0;

  DeferredResponseSender deferred_response_sender_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoMcpSession> weak_factory_{this};
};

class MahoMcpBrowserDelegate {
 public:
  using ExportArtifactCallback =
      base::OnceCallback<void(bool success, std::string error)>;
  virtual ~MahoMcpBrowserDelegate() = default;
  virtual std::optional<NativeInputAvailability>
  CheckNativeInputAvailability();
  virtual void PublishControlActivity(
      const std::string& activity_id,
      const std::string& controller_session_id,
      const std::string& controller_label,
      const ResolvedMahoMcpTarget& target,
      MahoBrowserToolRegistry::Category category,
      MahoBrowserToolRegistry::Sensitivity sensitivity,
      MahoMcpActivityPhase phase,
      uint64_t revision) {}
  virtual std::vector<MahoMcpSession::TabInfo> GetTabList() = 0;
  virtual std::vector<MahoMcpSession::ConsoleMessage> GetConsoleMessages(
      int tab_id) = 0;
  virtual std::vector<MahoMcpSession::NavigationEvent> GetNavigationEvents(
      int tab_id,
      int64_t since_ms) = 0;
  virtual std::string GetPageText(int tab_id) = 0;
  virtual base::Value GetAccessibilitySnapshot(
      int tab_id,
      MahoMcpSession::RefTable* out_refs) = 0;
  virtual base::Value GetAccessibilitySnapshot(
      int tab_id,
      MahoMcpSession::RefTable* out_refs,
      uint64_t* out_snapshot_token);

  struct AccessibilitySnapshotV2Params {
    AccessibilitySnapshotV2Params();
    ~AccessibilitySnapshotV2Params();
    AccessibilitySnapshotV2Params(const AccessibilitySnapshotV2Params&);
    AccessibilitySnapshotV2Params(AccessibilitySnapshotV2Params&&) noexcept;
    AccessibilitySnapshotV2Params& operator=(
        const AccessibilitySnapshotV2Params&);
    AccessibilitySnapshotV2Params& operator=(
        AccessibilitySnapshotV2Params&&) noexcept;

    int tab_id = 0;
    std::string mode = "interactive";
    bool include_hidden = false;
    std::optional<std::string> scope_selector;
    std::optional<int> scope_ref;
    std::optional<std::string> since_snapshot_token;
    size_t max_bytes = 24000;
    std::optional<int> max_depth;
  };

  struct AccessibilitySnapshotV2Result {
    AccessibilitySnapshotV2Result();
    ~AccessibilitySnapshotV2Result();
    AccessibilitySnapshotV2Result(const AccessibilitySnapshotV2Result&);
    AccessibilitySnapshotV2Result(AccessibilitySnapshotV2Result&&) noexcept;
    AccessibilitySnapshotV2Result& operator=(
        const AccessibilitySnapshotV2Result&);
    AccessibilitySnapshotV2Result& operator=(
        AccessibilitySnapshotV2Result&&) noexcept;

    std::string snapshot_token;
    MahoMcpSession::TabInfo tab;
    std::string tree;
    std::optional<std::string> diff;
    size_t captured_nodes = 0;
    size_t serialized_nodes = 0;
    size_t bytes = 0;
    bool truncated = false;
    std::optional<double> renderer_snapshot_ms;
    std::optional<double> ax_serialize_ms;
    std::optional<size_t> wire_bytes;
    std::optional<double> total_ms;
    std::optional<base::DictValue> bot_challenge;
  };

  virtual AccessibilitySnapshotV2Result GetAccessibilitySnapshotV2(
      const AccessibilitySnapshotV2Params& params,
      MahoMcpSession::RefTable* out_refs,
      MahoMcpAccessibilityHandler::ObservationCache& observation_cache,
      uint64_t token_sequence);

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
    int match_count = 0;
  };
  virtual QuerySelectorResult QuerySelector(int tab_id,
                                            const std::string& selector) = 0;

  virtual std::string GetElementText(int tab_id, const std::string& ref_id) = 0;
  virtual std::string GetElementAttribute(int tab_id,
                                          const std::string& ref_id,
                                          const std::string& attribute) = 0;
  virtual bool WaitForSelector(int tab_id,
                               const std::string& selector,
                               int timeout_ms) = 0;

  struct SameOriginFetchResult {
    SameOriginFetchResult();
    SameOriginFetchResult(const SameOriginFetchResult&);
    SameOriginFetchResult& operator=(const SameOriginFetchResult&);
    SameOriginFetchResult(SameOriginFetchResult&&) noexcept;
    SameOriginFetchResult& operator=(SameOriginFetchResult&&) noexcept;
    ~SameOriginFetchResult();

    bool success = false;
    int status = 0;
    std::string final_url;
    std::string content_type;
    std::string text;
    bool truncated = false;
    std::string error;
  };
  using SameOriginFetchCallback =
      base::OnceCallback<void(SameOriginFetchResult)>;
  virtual void SameOriginFetch(int tab_id,
                               const GURL& url,
                               const std::string& method,
                               const std::string& body,
                               const std::string& headers_json,
                               SameOriginFetchCallback callback);

  virtual int CreateNewTab(const GURL& url) = 0;
  virtual bool CloseTab(int tab_id) = 0;

  struct BookmarkInfo {
    BookmarkInfo();
    ~BookmarkInfo();
    BookmarkInfo(const BookmarkInfo&);
    BookmarkInfo(BookmarkInfo&&) noexcept;
    BookmarkInfo& operator=(const BookmarkInfo&);
    BookmarkInfo& operator=(BookmarkInfo&&) noexcept;

    // maho-core bookmark id (a string uuid), empty when nothing was stored.
    std::string id;
    std::string title;
    std::string url;
    std::string folder;
  };
  virtual std::vector<BookmarkInfo> SearchBookmarks(
      const std::string& query) = 0;
  virtual BookmarkInfo CreateBookmark(const std::string& title,
                                      const GURL& url,
                                      const std::string& folder) = 0;

  struct HistoryEntry {
    std::string url;
    std::string title;
    double visited_at = 0.0;  // Unix timestamp float (matches maho-core format)
  };
  virtual std::vector<HistoryEntry> SearchHistory(const std::string& query,
                                                  size_t max_results) = 0;

  // Async: base64-encoded PNG passed to `callback` together with the real
  // capture-time metrics (MahoMcpCaptureMetrics). Empty PNG string on failure
  // (metrics are then meaningless and must be treated as absent). Callback may
  // be invoked on any sequence.
  virtual void CaptureFullPagePngBase64(
      int tab_id,
      base::OnceCallback<void(std::string,
                              std::optional<MahoMcpCaptureMetrics>)>
          callback) = 0;

  virtual void CaptureElementPngBase64(
      int tab_id,
      ui::AXNodeID ax_id,
      base::OnceCallback<void(std::string)> callback) = 0;

  virtual bool Scroll(int tab_id,
                      const std::string& direction,
                      int pixels,
                      std::optional<ui::AXNodeID> ax_id) = 0;
  virtual bool Click(int tab_id, ui::AXNodeID ax_id) = 0;
  virtual bool Type(int tab_id,
                    ui::AXNodeID ax_id,
                    const std::string& text) = 0;
  // Supplies one local file to a chooser the target page has already opened.
  // Production performs path policy validation at the WebContents boundary.
  virtual bool SelectFileForPendingChooser(int tab_id,
                                           const std::string& path);
  virtual void SelectFileForPendingChooserAsync(
      int tab_id,
      const std::string& path,
      base::OnceCallback<void(bool)> callback);
  virtual bool SelectFileForInput(int tab_id,
                                  const std::string& css,
                                  const std::string& path);
  virtual void SelectFileForInputAsync(
      int tab_id,
      const std::string& css,
      const std::string& path,
      base::OnceCallback<void(bool)> callback);
  virtual bool ClickForced(int tab_id, ui::AXNodeID ax_id);
  virtual void ClickForced(
      int tab_id,
      ui::AXNodeID ax_id,
      base::OnceCallback<void(InputActionOutcome)> callback);
  virtual void ClickVerified(
      int tab_id,
      ui::AXNodeID ax_id,
      base::OnceCallback<void(InputActionOutcome)> callback);
  virtual void ClickVerified(
      int tab_id,
      ui::AXNodeID ax_id,
      bool force,
      base::OnceCallback<void(InputActionOutcome)> callback);
  virtual void TypeVerified(
      int tab_id,
      ui::AXNodeID ax_id,
      const std::string& text,
      base::OnceCallback<void(InputActionOutcome)> callback);
  struct LocatorParams {
    LocatorParams();
    ~LocatorParams();
    LocatorParams(const LocatorParams&);
    LocatorParams& operator=(const LocatorParams&);
    LocatorParams(LocatorParams&&) noexcept;
    LocatorParams& operator=(LocatorParams&&) noexcept;

    int tab_id = 0;
    std::optional<int> ref;
    std::optional<std::string> css;
    std::optional<std::string> role;
    std::optional<std::string> name;
    bool exact = true;
    bool force = false;
  };

  struct LocatorResolution {
    LocatorResolution();
    ~LocatorResolution();
    LocatorResolution(const LocatorResolution&);
    LocatorResolution& operator=(const LocatorResolution&);
    LocatorResolution(LocatorResolution&&) noexcept;
    LocatorResolution& operator=(LocatorResolution&&) noexcept;

    bool success = false;
    ui::AXNodeID ax_id = 0;
    std::string error_code;  // locator_not_found, locator_ambiguous,
                             // locator_not_actionable, locator_obscured,
                             // locator_detached, locator_stale
    int matches = 0;
    std::string hint;
  };

  virtual LocatorResolution ResolveLocator(
      int tab_id,
      const LocatorParams& params,
      const MahoMcpSession::RefTable& refs);
  virtual bool WaitForAutoQuiet(int tab_id, int timeout_ms);

  virtual bool RequiresDeferredVerifiedInputResponses() const;
  virtual bool Select(int tab_id,
                      ui::AXNodeID ax_id,
                      const std::string& value) = 0;
  virtual bool Hover(int tab_id, ui::AXNodeID ax_id) = 0;
  virtual bool KeyPress(int tab_id,
                        const std::string& key,
                        const std::vector<std::string>& modifiers) = 0;
  virtual bool ActivateTab(int tab_id) = 0;
  virtual bool Navigate(int tab_id, const GURL& url) = 0;
  virtual bool GoBack(int tab_id);
  virtual bool SetViewportSize(int tab_id, int width, int height) = 0;

  struct VaultCredentialSummary {
    std::string handle;
    std::string username_hint;
    std::string origin;
  };
  virtual std::vector<VaultCredentialSummary> VaultListCredentialsForActivePage(
      int tab_id);
  virtual std::optional<std::string> VaultRequestCredentialUse(
      int tab_id,
      const std::string& handle,
      const std::string& origin);
  virtual bool VaultFillCredential(int tab_id,
                                   const std::string& grant_handle,
                                   ui::AXNodeID ax_id);
  virtual bool VaultFillTotp(int tab_id,
                             const std::string& grant_handle,
                             ui::AXNodeID ax_id);

  struct StartNetworkCaptureResult {
    bool success = false;
    bool target_valid = true;
    int tab_id = 0;
  };
  struct StopNetworkCaptureResult {
    StopNetworkCaptureResult();
    StopNetworkCaptureResult(const StopNetworkCaptureResult&) = delete;
    StopNetworkCaptureResult& operator=(const StopNetworkCaptureResult&) =
        delete;
    StopNetworkCaptureResult(StopNetworkCaptureResult&&) noexcept;
    StopNetworkCaptureResult& operator=(StopNetworkCaptureResult&&) noexcept;
    ~StopNetworkCaptureResult();

    bool target_valid = true;
    std::optional<Redacted<base::DictValue>> har;
  };
  using StartNetworkCaptureCallback =
      base::OnceCallback<void(StartNetworkCaptureResult)>;
  using StopNetworkCaptureCallback =
      base::OnceCallback<void(StopNetworkCaptureResult)>;

  virtual void StartNetworkCapture(const std::string& capture_id,
                                   int tab_id,
                                   const ResolvedMahoMcpTarget& target,
                                   StartNetworkCaptureCallback callback) = 0;
  virtual void StopNetworkCapture(const std::string& capture_id,
                                  const ResolvedMahoMcpTarget& target,
                                  StopNetworkCaptureCallback callback) = 0;
  virtual void CancelNetworkCapture(const std::string& capture_id) = 0;

  // Routines (FIX-4-browser): bridge the SAME maho_core routines engine over
  // MCP. `ListRoutines` returns a JSON array of built-in + custom routines.
  // `RunRoutine` runs by id asynchronously. The error enum is the stable
  // machine contract; `content_or_error` is presentation text or result JSON.
  // Default impls keep these optional for test delegates.
  enum class RoutineRunError {
    kNone,
    kTierLocked,
    kUnavailable,
    kExecutionFailed,
  };
  using RunRoutineCallback =
      base::OnceCallback<void(RoutineRunError error,
                              std::string content_or_error)>;
  virtual std::string ListRoutines();
  virtual void RunRoutine(const std::string& id, RunRoutineCallback callback);

  // Mail read broker (T8). Each read is served asynchronously and returns a
  // JSON-encoded result string so the MCP layer forwards the durable broker
  // payload without deserialising it on the browser side. |ok| is false on a
  // broker failure; |result_json| then carries an optional diagnostic string.
  // The callback may be invoked on any sequence. The default implementations
  // report the broker as unavailable so existing test delegates that do not
  // model a mail broker keep compiling and dispatch cleanly to a -32000.
  using MailReadCallback =
      base::OnceCallback<void(bool ok, std::string result_json)>;
  virtual void MailListAccounts(MailReadCallback callback);
  virtual void MailListFolders(const std::string& account_id,
                               MailReadCallback callback);
  virtual void MailListEmails(const std::string& account_id,
                              const std::string& folder_id,
                              int64_t limit,
                              int64_t offset,
                              MailReadCallback callback);
  virtual void MailGetEmail(const std::string& email_id,
                            MailReadCallback callback);
  virtual void MailSearchEmails(const std::string& query_json,
                                MailReadCallback callback);
  virtual void MailListThread(const std::string& account_id,
                              const std::string& message_id,
                              MailReadCallback callback);
  virtual void MailAddAccount(const std::string& request_json,
                              MailReadCallback callback);
  virtual void MailTestConnection(const std::string& params_json,
                                  MailReadCallback callback);
  virtual void MailDeleteAccount(const std::string& account_id,
                                 MailReadCallback callback);
  virtual void MailOAuthStartUrl(const std::string& provider,
                                 const std::string& client_id,
                                 const std::string& redirect_uri,
                                 MailReadCallback callback);
  virtual void MailOAuthComplete(const std::string& state,
                                 const std::string& code,
                                 MailReadCallback callback);
  virtual void MailReconnectAccount(const std::string& account_id,
                                    MailReadCallback callback);
  virtual void MailImportMigrationArchive(const std::string& archive_json,
                                          MailReadCallback callback);
  virtual void MailExtractOtp(const std::string& account_id,
                              const std::string& folder_id,
                              const std::string& query,
                              int64_t max_age_seconds,
                              MailReadCallback callback);
  virtual void MailSendEmail(const std::string& request_json,
                             bool already_authorized,
                             MailReadCallback callback);
  virtual void MailSaveDraft(const std::string& request_json,
                             bool already_authorized,
                             MailReadCallback callback);
  virtual void MailUpdateDraft(const std::string& draft_id,
                               const std::string& request_json,
                               bool already_authorized,
                               MailReadCallback callback);
  virtual void MailQueueEmail(const std::string& request_json,
                              MailReadCallback callback);
  virtual void MailFlag(const std::string& request_json,
                        MailReadCallback callback);

  // Live Mail authorization inputs for both direct MCP and the desktop-agent
  // executor. Production derives these from the active profile pref/service;
  // tests supply deterministic table states. Approval is intentionally a
  // separate typed operation and is never implied by global allow.
  virtual ai::MailAuthorizationContext GetMailAuthorizationContext();
  virtual bool ConfirmMailToolApproval(std::string_view tool_name,
                                       std::string_view redacted_arguments);
  virtual bool ConfirmCredentialTypingApproval(
      std::string_view tool_name,
      const ResolvedMahoMcpTarget& target);
  virtual bool ConfirmBrowserActionApproval(
      std::string_view tool_name,
      const ResolvedMahoMcpTarget& target);
  virtual MahoMcpFeatureGates GetFeatureGates();

  // AI Agent goal delegation: delegate a goal/prompt to the browser-owned
  // Maho AI agent via MahoAiIngressCoordinator.
  struct DelegateGoalResult {
    DelegateGoalResult();
    ~DelegateGoalResult();
    DelegateGoalResult(const DelegateGoalResult&);
    DelegateGoalResult(DelegateGoalResult&&) noexcept;
    DelegateGoalResult& operator=(const DelegateGoalResult&);
    DelegateGoalResult& operator=(DelegateGoalResult&&) noexcept;

    bool accepted = false;
    std::string request_id;
    std::string status = "queued";
    int browser_id = 0;
    int error_code = 0;
    std::string error_message;
  };
  virtual DelegateGoalResult DelegateGoal(
      const std::string& goal,
      std::optional<int> browser_id,
      std::optional<std::string> request_id,
      std::optional<std::string> context_intent);

  virtual std::string ListArtifacts(std::string_view session_id);
  virtual void ExportArtifact(std::string artifact_id,
                              base::FilePath destination,
                              ExportArtifactCallback callback);

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
  virtual bool IsTabWindowActive(int tab_id);
  virtual bool RevalidateRefSnapshot(const ResolvedMahoMcpTarget& target,
                                     uint64_t snapshot_token);

  // Returns trusted AX-derived classification metadata for the field bound to
  // |ax_id| in |tab_id|. The default returns empty (non-sensitive) metadata so
  // test delegates that do not model private contexts keep ordinary typing
  // functional; production overrides read the live accessibility tree.
  virtual MahoMcpFieldMetadata GetFieldMetadata(int tab_id, ui::AXNodeID ax_id);
};

}  // namespace maho

#endif  // MAHO_BROWSER_MCP_MAHO_MCP_SESSION_H_
