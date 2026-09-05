// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_session.h"

#include <deque>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "build/build_config.h"

#if !BUILDFLAG(IS_WIN)
#include <unistd.h>
#endif

#include "base/json/json_reader.h"
#include "base/json/string_escape.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/task_environment.h"
#include "base/values.h"
#include "maho/browser/mcp/maho_mcp_capability_registry.h"
#include "maho/browser/mcp/maho_mcp_firewall.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

// Constructs a session the way a same-user client connection would appear on
// each platform: POSIX authenticates by peer UID, Windows by an auth token
// presented during the initialize handshake. A null token skips token
// validation, which is the Windows analogue of the matching-UID case.
std::unique_ptr<MahoMcpSession>
MakeTestSession(MahoMcpLeaseRegistry *lease_registry = nullptr) {
#if BUILDFLAG(IS_WIN)
  return std::make_unique<MahoMcpSession>(
      static_cast<MahoMcpSessionToken *>(nullptr), lease_registry);
#else
  return std::make_unique<MahoMcpSession>(getuid(), lease_registry);
#endif
}

std::unique_ptr<MahoMcpSession>
MakeTrustedCliTestSession(MahoMcpLeaseRegistry* lease_registry = nullptr) {
#if BUILDFLAG(IS_WIN)
  return std::make_unique<MahoMcpSession>(
      static_cast<MahoMcpSessionToken*>(nullptr), lease_registry,
      "C:\\Program Files\\Maho\\maho.exe", true);
#elif BUILDFLAG(IS_APPLE)
  return std::make_unique<MahoMcpSession>(
      getuid(), lease_registry, "/Applications/Maho.app/Contents/Helpers/maho",
      true);
#else
  return std::make_unique<MahoMcpSession>(getuid(), lease_registry,
                                          "/usr/bin/maho", true);
#endif
}

std::string TrustedBrowserMcpExecutableForTesting() {
#if BUILDFLAG(IS_WIN)
  return "C:\\Program Files\\Maho\\maho-browser-mcp.exe";
#elif BUILDFLAG(IS_APPLE)
  return "/Applications/Maho.app/Contents/Helpers/maho-browser-mcp";
#else
  return "/usr/bin/maho-browser-mcp";
#endif
}

// ---------------------------------------------------------------------------
// R-8: non-revealing, generation-bound target resolution (task-8-mcp-session).
// ---------------------------------------------------------------------------

// A delegate that models the private-context boundary: it can mark tabs
// ineligible (OTR/closed), report no eligible active tab or browser, and fail
// revalidation when its generation advances. It also records how many times
// each resolution entry point is called so tests can prove resolve-once.
//
// Defined above the primary fixture so MahoMcpSessionTest can install it for
// the R-8 target-resolution tests without a forward reference.
class ResolvingFakeDelegate : public MahoMcpBrowserDelegate {
public:
  MahoMcpFeatureGates GetFeatureGates() override {
    return {.mail_enabled = true,
            .routines_enabled = true,
            .vault_enabled = true};
  }
  int active_tab_id = 7;
  bool has_eligible_active = true;
  bool has_eligible_browser = true;
  std::set<int> eligible_tabs = {7, 11};
  int64_t generation = 1;
  bool issue_unique_generations = false;
  int64_t next_generation = 1;
  uint64_t navigation_epoch = 1;

  int resolve_tab_calls = 0;
  int resolve_profile_calls = 0;
  int revalidate_calls = 0;
  std::string last_navigated_url;
  bool approve_browser_actions = true;
  bool advance_generation_on_approval = false;
  bool advance_navigation_epoch_on_approval = false;
  int browser_action_approval_calls = 0;
  int browser_type_call_count = 0;
  int browser_type_tab_id = 0;
  ui::AXNodeID browser_type_ax_id = 0;
  int browser_click_call_count = 0;
  int browser_click_tab_id = 0;
  ui::AXNodeID browser_click_ax_id = 0;
  bool click_succeeds = true;
  bool type_succeeds = true;

  struct MockLocatorNode {
    ui::AXNodeID ax_id = 0;
    std::string role;
    std::string name;
    std::string css;
    bool attached = true;
    bool visible = true;
    bool enabled = true;
  };
  std::vector<MockLocatorNode> mock_locator_nodes;
  int file_upload_call_count = 0;
  int file_upload_tab_id = 0;
  std::string file_upload_path;
  bool file_upload_succeeds = true;

  struct PublishedActivity {
    std::string activity_id;
    ResolvedMahoMcpTarget target;
    MahoBrowserToolRegistry::Category category;
    MahoBrowserToolRegistry::Sensitivity sensitivity;
    MahoMcpActivityPhase phase;
    uint64_t revision;
  };
  std::vector<PublishedActivity> published_activities;

  struct PendingStart {
    std::string capture_id;
    int requested_tab_id = 0;
    StartNetworkCaptureCallback callback;
  };
  std::deque<PendingStart> pending_starts;
  std::deque<StopNetworkCaptureCallback> pending_stops;
  std::vector<std::string> canceled_capture_ids;
  int same_origin_fetch_calls = 0;
  RoutineRunError routine_run_error = RoutineRunError::kNone;
  std::string routine_run_payload = R"({"id":"routine-run"})";
  GURL last_fetch_url;
  std::string last_fetch_method;
  std::string last_fetch_body;
  std::string last_fetch_headers_json;

  void PublishControlActivity(
      const std::string& activity_id,
      const std::string& controller_session_id,
      const std::string& controller_label,
      const ResolvedMahoMcpTarget& target,
      MahoBrowserToolRegistry::Category category,
      MahoBrowserToolRegistry::Sensitivity sensitivity,
      MahoMcpActivityPhase phase,
      uint64_t revision) override {
    published_activities.push_back(
        {activity_id, target, category, sensitivity, phase, revision});
  }

  MahoMcpTargetResolution ResolveTabTarget(int requested_tab_id) override {
    ++resolve_tab_calls;
    MahoMcpTargetResolution resolution;
    if (requested_tab_id == 0) {
      if (!has_eligible_active) {
        resolution.error = MahoMcpTargetError::kNoEligibleActiveTab;
        return resolution;
      }
      resolution.target.valid = true;
      resolution.target.tab_id = active_tab_id;
      resolution.target.browser_id = 100;
      resolution.target.generation = issue_unique_generations
                                         ? ++next_generation
                                         : generation;
      return resolution;
    }
    if (eligible_tabs.find(requested_tab_id) == eligible_tabs.end()) {
      resolution.error = MahoMcpTargetError::kTabNotFound;
      return resolution;
    }
    resolution.target.valid = true;
    resolution.target.tab_id = requested_tab_id;
    resolution.target.browser_id = 100;
    resolution.target.generation = issue_unique_generations
                                       ? ++next_generation
                                       : generation;
    return resolution;
  }

  bool ConfirmBrowserActionApproval(
      std::string_view tool_name,
      const ResolvedMahoMcpTarget& target) override {
    ++browser_action_approval_calls;
    if (advance_generation_on_approval) {
      ++generation;
    }
    if (advance_navigation_epoch_on_approval) {
      ++navigation_epoch;
    }
    return approve_browser_actions;
  }

  MahoMcpTargetResolution ResolveProfileTarget() override {
    ++resolve_profile_calls;
    MahoMcpTargetResolution resolution;
    if (!has_eligible_browser) {
      resolution.error = MahoMcpTargetError::kNoEligibleActiveBrowser;
      return resolution;
    }
    resolution.target.valid = true;
    resolution.target.browser_id = 100;
    resolution.target.generation = generation;
    return resolution;
  }

  bool RevalidateTarget(const ResolvedMahoMcpTarget &target) override {
    ++revalidate_calls;
    if (!target.valid) {
      return false;
    }
    if (!issue_unique_generations && target.generation != generation) {
      return false;
    }
    if (target.tab_id != 0 &&
        eligible_tabs.find(target.tab_id) == eligible_tabs.end()) {
      return false;
    }
    return true;
  }

  std::vector<MahoMcpSession::TabInfo> GetTabList() override {
    std::vector<MahoMcpSession::TabInfo> tabs;
    for (int id : eligible_tabs) {
      MahoMcpSession::TabInfo t;
      t.id = id;
      t.title = "tab";
      t.url = "https://example.com/";
      t.is_active = (id == active_tab_id);
      tabs.push_back(t);
    }
    return tabs;
  }

  void StartNetworkCapture(const std::string &capture_id, int tab_id,
                           const ResolvedMahoMcpTarget &target,
                           StartNetworkCaptureCallback callback) override {
    pending_starts.push_back(
        PendingStart{capture_id, tab_id,
                     base::BindOnce(
                         [](ResolvingFakeDelegate *delegate,
                            ResolvedMahoMcpTarget target,
                            StartNetworkCaptureCallback callback,
                            StartNetworkCaptureResult result) {
                           result.target_valid =
                               result.target_valid &&
                               delegate->RevalidateTarget(target);
                           std::move(callback).Run(std::move(result));
                         },
                         base::Unretained(this), target, std::move(callback))});
  }
  void StopNetworkCapture(const std::string &capture_id,
                          const ResolvedMahoMcpTarget &target,
                          StopNetworkCaptureCallback callback) override {
    pending_stops.push_back(base::BindOnce(
        [](ResolvingFakeDelegate *delegate, ResolvedMahoMcpTarget target,
           StopNetworkCaptureCallback callback, StopNetworkCaptureResult result) {
          result.target_valid =
              result.target_valid && delegate->RevalidateTarget(target);
          std::move(callback).Run(std::move(result));
        },
        base::Unretained(this), target, std::move(callback)));
  }
  void CancelNetworkCapture(const std::string &capture_id) override {
    canceled_capture_ids.push_back(capture_id);
  }

  std::vector<MahoMcpSession::ConsoleMessage>
  GetConsoleMessages(int tab_id) override {
    return {};
  }
  std::vector<MahoMcpSession::NavigationEvent>
  GetNavigationEvents(int tab_id, int64_t since_ms) override {
    return {};
  }
  std::string GetPageText(int tab_id) override { return std::string(); }
  base::Value GetAccessibilitySnapshot(
      int tab_id,
      MahoMcpSession::RefTable* out_refs) override {
    return base::Value(base::DictValue());
  }
  base::Value GetAccessibilitySnapshot(
      int tab_id,
      MahoMcpSession::RefTable* out_refs,
      uint64_t* out_snapshot_token) override {
    if (out_snapshot_token) {
      *out_snapshot_token = navigation_epoch;
    }
    return base::Value(base::DictValue());
  }
  bool RevalidateRefSnapshot(const ResolvedMahoMcpTarget& target,
                             uint64_t snapshot_token) override {
    return target.valid && snapshot_token == navigation_epoch;
  }
  AccessibilitySnapshotV2Result GetAccessibilitySnapshotV2(
      const AccessibilitySnapshotV2Params& params,
      MahoMcpSession::RefTable* out_refs,
      MahoMcpAccessibilityHandler::ObservationCache& observation_cache,
      uint64_t token_sequence) override {
    ui::AXTreeUpdate update;
    update.root_id = 1;
    if (browser_click_call_count > 0) {
      update.nodes.resize(4);
      update.nodes[0].id = 1;
      update.nodes[0].role = ax::mojom::Role::kRootWebArea;
      update.nodes[0].child_ids = {2, 3, 4};
      update.nodes[1].id = 2;
      update.nodes[1].role = ax::mojom::Role::kHeading;
      update.nodes[1].SetName("Dashboard");
      update.nodes[2].id = 3;
      update.nodes[2].role = ax::mojom::Role::kButton;
      update.nodes[2].SetName("Logout");
      update.nodes[3].id = 4;
      update.nodes[3].role = ax::mojom::Role::kStaticText;
      update.nodes[3].SetName("Action completed");
    } else {
      update.nodes.resize(3);
      update.nodes[0].id = 1;
      update.nodes[0].role = ax::mojom::Role::kRootWebArea;
      update.nodes[0].child_ids = {2, 3};
      update.nodes[1].id = 2;
      update.nodes[1].role = ax::mojom::Role::kHeading;
      update.nodes[1].SetName("Dashboard");
      update.nodes[2].id = 3;
      update.nodes[2].role = ax::mojom::Role::kButton;
      update.nodes[2].SetName("Logout");
    }

    ui::AXTree tree(update);
    MahoMcpAccessibilityHandler::SnapshotV2Options opts;
    if (params.mode == "compact") {
      opts.mode = MahoMcpAccessibilityHandler::SnapshotMode::kCompact;
    } else if (params.mode == "full") {
      opts.mode = MahoMcpAccessibilityHandler::SnapshotMode::kFull;
    } else {
      opts.mode = MahoMcpAccessibilityHandler::SnapshotMode::kInteractive;
    }
    opts.include_hidden = params.include_hidden;
    opts.scope_selector = params.scope_selector;
    opts.scope_ref = params.scope_ref;
    opts.since_snapshot_token = params.since_snapshot_token;
    opts.max_bytes = params.max_bytes;
    opts.max_depth = params.max_depth;

    MahoMcpAccessibilityHandler::SnapshotV2Result::TabMetadata tab_meta;
    tab_meta.id = (params.tab_id != 0) ? params.tab_id : 7;
    tab_meta.url = "https://example.com/page";
    tab_meta.title = "Test Page";

    MahoMcpAccessibilityHandler::RefTable local_refs;
    std::string frame_sig = "1:0";
    auto v2_res = MahoMcpAccessibilityHandler::BuildSnapshotV2(
        tree, opts, local_refs, tab_meta, navigation_epoch, frame_sig,
        observation_cache, token_sequence);
    if (out_refs) {
      *out_refs = local_refs;
    }
    AccessibilitySnapshotV2Result result;
    result.snapshot_token = v2_res.snapshot_token;
    result.tab.id = tab_meta.id;
    result.tab.url = tab_meta.url;
    result.tab.title = tab_meta.title;
    result.tree = std::move(v2_res.tree);
    result.diff = std::move(v2_res.diff);
    result.captured_nodes = v2_res.stats.captured_nodes;
    result.serialized_nodes = v2_res.stats.serialized_nodes;
    result.bytes = v2_res.stats.bytes;
    result.truncated = v2_res.stats.truncated;
    return result;
  }
  PageContentResult GetPageContent(int tab_id) override {
    PageContentResult result;
    result.url = "https://example.com/page";
    return result;
  }
  PageContextResult GetPageContext(int tab_id) override { return {}; }
  SearchResult SearchInPage(int tab_id, const std::string &query) override {
    return {};
  }
  QuerySelectorResult QuerySelector(int tab_id,
                                    const std::string &selector) override {
    return {};
  }
  std::string GetElementText(int tab_id, const std::string &ref_id) override {
    return std::string();
  }
  std::string GetElementAttribute(int tab_id, const std::string &ref_id,
                                  const std::string &attribute) override {
    return std::string();
  }
  bool WaitForSelector(int tab_id, const std::string &selector,
                       int timeout_ms) override {
    return false;
  }
  void SameOriginFetch(int tab_id, const GURL &url, const std::string &method,
                       const std::string &body, const std::string &headers_json,
                       SameOriginFetchCallback callback) override {
    ++same_origin_fetch_calls;
    last_fetch_url = url;
    last_fetch_method = method;
    last_fetch_body = body;
    last_fetch_headers_json = headers_json;
    SameOriginFetchResult result;
    result.success = true;
    result.status = 200;
    result.final_url = url.spec();
    result.content_type = "application/json";
    result.text = R"({"ok":true})";
    std::move(callback).Run(std::move(result));
  }
  int CreateNewTab(const GURL &url) override { return 99; }
  bool CloseTab(int tab_id) override { return true; }
  std::vector<BookmarkInfo> SearchBookmarks(const std::string &query) override {
    return {};
  }
  BookmarkInfo CreateBookmark(const std::string &title, const GURL &url,
                              const std::string &folder) override {
    return BookmarkInfo();
  }
  std::vector<HistoryEntry> SearchHistory(const std::string &query,
                                          size_t max_results) override {
    return {};
  }
  void CaptureFullPagePngBase64(
      int tab_id, base::OnceCallback<void(std::string)> callback) override {
    std::move(callback).Run(std::string());
  }
  void CaptureElementPngBase64(
      int tab_id, ui::AXNodeID ax_id,
      base::OnceCallback<void(std::string)> callback) override {
    std::move(callback).Run(std::string());
  }
  bool Scroll(int tab_id, const std::string &direction, int pixels,
              std::optional<ui::AXNodeID> ax_id) override {
    return false;
  }
  bool Click(int tab_id, ui::AXNodeID ax_id) override {
    ++browser_click_call_count;
    browser_click_tab_id = tab_id;
    browser_click_ax_id = ax_id;
    return click_succeeds;
  }
  void ClickVerified(
      int tab_id,
      ui::AXNodeID ax_id,
      base::OnceCallback<void(InputActionOutcome)> callback) override {
    ++browser_click_call_count;
    browser_click_tab_id = tab_id;
    browser_click_ax_id = ax_id;
    InputActionOutcome outcome;
    outcome.dispatched = click_succeeds;
    outcome.verified = click_succeeds;
    outcome.method = "trusted_input";
    if (!click_succeeds) {
      outcome.reason = "click_failed";
    }
    std::move(callback).Run(std::move(outcome));
  }
  bool Type(int tab_id, ui::AXNodeID ax_id, const std::string &text) override {
    ++browser_type_call_count;
    browser_type_tab_id = tab_id;
    browser_type_ax_id = ax_id;
    return type_succeeds;
  }
  void TypeVerified(
      int tab_id,
      ui::AXNodeID ax_id,
      const std::string& text,
      base::OnceCallback<void(InputActionOutcome)> callback) override {
    ++browser_type_call_count;
    browser_type_tab_id = tab_id;
    browser_type_ax_id = ax_id;
    InputActionOutcome outcome;
    outcome.dispatched = type_succeeds;
    outcome.verified = type_succeeds;
    outcome.method = "trusted_input";
    if (!type_succeeds) {
      outcome.reason = "type_failed";
    }
    std::move(callback).Run(std::move(outcome));
  }

  LocatorResolution ResolveLocator(
      int tab_id,
      const LocatorParams& params,
      const MahoMcpSession::RefTable& refs) override {
    LocatorResolution res;

    if (params.ref.has_value()) {
      auto it = refs.find(*params.ref);
      if (it == refs.end()) {
        res.error_code = "locator_not_found";
        res.hint = "Ref not found in session table";
        return res;
      }
      ui::AXNodeID target_ax_id = it->second;
      for (const auto& node : mock_locator_nodes) {
        if (node.ax_id == target_ax_id) {
          if (!node.attached) {
            res.error_code = "locator_detached";
            res.hint = "Element is detached from DOM";
            return res;
          }
          if (!node.enabled) {
            res.error_code = "locator_not_actionable";
            res.hint = "Element is disabled";
            return res;
          }
          if (!node.visible) {
            res.error_code = "locator_obscured";
            res.hint = "Element is obscured or not visible";
            return res;
          }
          break;
        }
      }
      res.success = true;
      res.ax_id = target_ax_id;
      return res;
    }

    if (params.css.has_value()) {
      std::vector<const MockLocatorNode*> matches;
      for (const auto& node : mock_locator_nodes) {
        if (node.css == *params.css) {
          matches.push_back(&node);
        }
      }
      if (matches.empty()) {
        res.error_code = "locator_not_found";
        res.hint = "No element matches selector: " + *params.css;
        return res;
      }
      if (matches.size() > 1) {
        res.error_code = "locator_ambiguous";
        res.matches = static_cast<int>(matches.size());
        res.hint = "Multiple elements match selector";
        return res;
      }
      const auto* target = matches[0];
      if (!target->attached) {
        res.error_code = "locator_detached";
        res.hint = "Element is detached from DOM";
        return res;
      }
      if (!target->enabled) {
        res.error_code = "locator_not_actionable";
        res.hint = "Element is disabled";
        return res;
      }
      if (!target->visible) {
        res.error_code = "locator_obscured";
        res.hint = "Element is obscured or not visible";
        return res;
      }
      res.success = true;
      res.ax_id = target->ax_id;
      return res;
    }

    if (params.role.has_value() && params.name.has_value()) {
      std::vector<const MockLocatorNode*> matches;
      for (const auto& node : mock_locator_nodes) {
        bool role_match =
            base::EqualsCaseInsensitiveASCII(node.role, *params.role);
        bool name_match =
            params.exact
                ? base::EqualsCaseInsensitiveASCII(node.name, *params.name)
                : (base::ToLowerASCII(node.name).find(
                       base::ToLowerASCII(*params.name)) != std::string::npos);
        if (role_match && name_match) {
          matches.push_back(&node);
        }
      }
      if (matches.empty()) {
        res.error_code = "locator_not_found";
        res.hint = "No element matches role and name";
        return res;
      }
      if (matches.size() > 1) {
        res.error_code = "locator_ambiguous";
        res.matches = static_cast<int>(matches.size());
        res.hint = "Multiple elements match role and name";
        return res;
      }
      const auto* target = matches[0];
      if (!target->attached) {
        res.error_code = "locator_detached";
        res.hint = "Element is detached from DOM";
        return res;
      }
      if (!target->enabled) {
        res.error_code = "locator_not_actionable";
        res.hint = "Element is disabled";
        return res;
      }
      if (!target->visible) {
        res.error_code = "locator_obscured";
        res.hint = "Element is obscured or not visible";
        return res;
      }
      res.success = true;
      res.ax_id = target->ax_id;
      return res;
    }

    res.error_code = "locator_not_found";
    res.hint = "Invalid locator parameters";
    return res;
  }

  bool WaitForAutoQuiet(int tab_id, int timeout_ms) override {
    return true;
  }
  bool SelectFileForPendingChooser(int tab_id,
                                   const std::string& path) override {
    ++file_upload_call_count;
    file_upload_tab_id = tab_id;
    file_upload_path = path;
    return file_upload_succeeds;
  }
  bool Select(int tab_id, ui::AXNodeID ax_id,
              const std::string &value) override {
    return false;
  }
  bool Hover(int tab_id, ui::AXNodeID ax_id) override { return false; }
  bool KeyPress(int tab_id, const std::string &key,
                const std::vector<std::string> &modifiers) override {
    return false;
  }
  bool ActivateTab(int tab_id) override { return true; }
  bool Navigate(int tab_id, const GURL &url) override {
    last_navigated_url = url.spec();
    return true;
  }
  bool SetViewportSize(int tab_id, int width, int height) override {
    return true;
  }
  void RunRoutine(const std::string& id,
                  RunRoutineCallback callback) override {
    std::move(callback).Run(routine_run_error, routine_run_payload);
  }
};

class MahoMcpSessionTest : public testing::Test {
protected:
  void SetUp() override {
    // Create session as a same-user connection (see MakeTestSession).
    session_ = MakeTestSession();
  }

  void TearDown() override {
    // Safe no-op for tests that never installed a resolving delegate: the
    // members below stay null and SetBrowserDelegate(nullptr) is idempotent.
    MahoMcpSession::SetBrowserDelegate(nullptr);
    session_.reset();
    delegate_.reset();
  }

  // Installs a ResolvingFakeDelegate + lease registry and initializes a fresh
  // session for the R-8 target-resolution tests.
  void InitResolvingSession(bool browser_mcp = false,
                            bool autonomous = false) {
    session_.reset();
    delegate_ = std::make_unique<ResolvingFakeDelegate>();
    MahoMcpSession::SetBrowserDelegate(delegate_.get());
    lease_registry_ = std::make_unique<MahoMcpLeaseRegistry>();
#if BUILDFLAG(IS_WIN)
    session_ = browser_mcp
                   ? std::make_unique<MahoMcpSession>(
                         static_cast<MahoMcpSessionToken*>(nullptr),
                         lease_registry_.get(),
                         TrustedBrowserMcpExecutableForTesting(), true)
                   : MakeTrustedCliTestSession(lease_registry_.get());
#else
    session_ = browser_mcp
                   ? std::make_unique<MahoMcpSession>(
                         getuid(), lease_registry_.get(),
                         TrustedBrowserMcpExecutableForTesting(), true)
                   : MakeTrustedCliTestSession(lease_registry_.get());
#endif
    session_->SetDeferredResponseSender(base::BindRepeating(
        &MahoMcpSessionTest::CaptureDeferred, base::Unretained(this)));
    auto responses = session_->ProcessData(base::StringPrintf(
        R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
        R"("params":{"protocolVersion":"2025-03-26",)"
        R"("controllerKind":"%s",)"
        R"("autonomous":%s,)"
        R"("clientInfo":{"name":"t","version":"0.1.0"}}})"
        "\n", browser_mcp ? "maho-browser-mcp" : "maho-cli",
        autonomous ? "true" : "false"));
    EXPECT_EQ(responses.size(), 1u);
    EXPECT_EQ(session_->state(), MahoMcpSession::State::kActive);
  }

  std::vector<std::string> CallToolRaw(const std::string &name,
                                       const std::string &args, int id) {
    return session_->ProcessData(
        base::StringPrintf(R"({"jsonrpc":"2.0","method":"tools/call","id":%d,)"
                           R"("params":{"name":"%s","arguments":%s}})"
                           "\n",
                           id, name.c_str(), args.c_str()));
  }

  std::vector<std::string> CallControlRaw(const std::string& name,
                                          const std::string& args,
                                          int id) {
    return CallControlRawOn(session_.get(), name, args, id);
  }

  std::vector<std::string> InitializeCliRaw(MahoMcpSession* target,
                                            bool autonomous,
                                            int id = 1) {
    return target->ProcessData(base::StringPrintf(
        R"({"jsonrpc":"2.0","method":"initialize","id":%d,)"
        R"("params":{"protocolVersion":"2025-03-26",)"
        R"("controllerKind":"maho-cli","autonomous":%s,)"
        R"("clientInfo":{"name":"t","version":"0.1.0"}}})"
        "\n",
        id, autonomous ? "true" : "false"));
  }

  std::vector<std::string> CallControlRawOn(MahoMcpSession* target,
                                             const std::string& name,
                                             const std::string& args,
                                             int id) {
    return target->ProcessData(base::StringPrintf(
        R"({"jsonrpc":"2.0","method":"maho/control/call","id":%d,)"
        R"("params":{"name":"%s","arguments":%s}})"
        "\n",
        id, name.c_str(), args.c_str()));
  }

  base::Value ParseLine(const std::string &line) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(line, base::JSON_PARSE_RFC);
    EXPECT_TRUE(parsed.has_value());
    return parsed.has_value() ? std::move(*parsed) : base::Value();
  }

  base::Value ParseExecutionOutput(const std::string& line) {
    base::Value response = ParseLine(line);
    const auto* result = response.GetDict().FindDict("result");
    EXPECT_TRUE(result) << line;
    if (!result) {
      return base::Value(base::Value::Type::DICT);
    }
    const std::string* output_json = result->FindString("outputJson");
    EXPECT_TRUE(output_json) << line;
    if (!output_json) {
      return base::Value(base::Value::Type::DICT);
    }
    std::optional<base::Value> output =
        base::JSONReader::Read(*output_json, base::JSON_PARSE_RFC);
    EXPECT_TRUE(output.has_value()) << *output_json;
    return output.has_value() ? std::move(*output)
                              : base::Value(base::Value::Type::DICT);
  }

  void CaptureDeferred(std::string line) {
    deferred_responses_.push_back(std::move(line));
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::unique_ptr<MahoMcpLeaseRegistry> lease_registry_;
  std::unique_ptr<ResolvingFakeDelegate> delegate_;
  std::unique_ptr<MahoMcpSession> session_;
  std::vector<std::string> deferred_responses_;
};

TEST_F(MahoMcpSessionTest, RejectsToolCallBeforeInitialize) {
  std::string input =
      R"({"jsonrpc":"2.0","method":"tools/call","id":1,"params":{"name":"test"}})"
      "\n";

  auto responses = session_->ProcessData(input);
  ASSERT_FALSE(responses.empty());

  std::optional<base::Value> parsed =
      base::JSONReader::Read(responses[0], base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());

  const auto *error = parsed->GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32600);
  EXPECT_TRUE(error->FindString("message")->find("initialize expected") !=
              std::string::npos);

  EXPECT_EQ(session_->state(), MahoMcpSession::State::kClosed);
}

TEST_F(MahoMcpSessionTest, SessionIdentityLivesForOneConnection) {
  const std::string first_id = session_->session_id();
  EXPECT_FALSE(first_id.empty());

  auto responses = session_->ProcessData(
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("clientInfo":{"name":"test-client","version":"0.1.0"}}})"
      "\n");
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(session_->session_id(), first_id);

  auto other = MakeTestSession();
  EXPECT_FALSE(other->session_id().empty());
  EXPECT_NE(other->session_id(), first_id);
}

TEST_F(MahoMcpSessionTest, KnownControllersRequireMatchingPeerExecutable) {
  struct Case {
    const char *executable;
    const char *hint;
    const char *expected_label;
    const char *expected_kind;
  };
  const std::string cli_executable =
#if BUILDFLAG(IS_WIN)
      "C:\\Program Files\\Maho\\maho.exe";
#elif BUILDFLAG(IS_APPLE)
      "/Applications/Maho.app/Contents/Helpers/maho";
#else
      "/usr/bin/maho";
#endif
  const std::string browser_mcp_executable =
      TrustedBrowserMcpExecutableForTesting();
  const Case cases[] = {
      {cli_executable.c_str(), "maho-cli", "Maho CLI",
       "maho-cli"},
      {cli_executable.c_str(), "maho-cli-repl",
       "Maho CLI REPL", "maho-cli-repl"},
      {browser_mcp_executable.c_str(),
       "maho-browser-mcp", "Maho Browser MCP", "maho-browser-mcp"},
      {"/usr/local/bin/arbitrary-host", "third-party", "External MCP",
       "third-party"},
  };
  for (const Case &test : cases) {
#if BUILDFLAG(IS_WIN)
    auto candidate = std::make_unique<MahoMcpSession>(
        static_cast<MahoMcpSessionToken *>(nullptr), nullptr, test.executable,
        true);
#else
    auto candidate = std::make_unique<MahoMcpSession>(
        getuid(), nullptr, test.executable, true);
#endif
    auto responses = candidate->ProcessData(base::StringPrintf(
        R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
        R"("params":{"protocolVersion":"2025-03-26",)"
        R"("controllerKind":"%s",)"
        R"("clientInfo":{"name":"caller display","version":"1"}}})"
        "\n",
        test.hint));
    ASSERT_EQ(responses.size(), 1u);
    base::Value response = ParseLine(responses[0]);
    const base::DictValue *result = response.GetDict().FindDict("result");
    ASSERT_TRUE(result);
    const base::DictValue *info = result->FindDict("sessionInfo");
    ASSERT_TRUE(info);
    ASSERT_TRUE(info->FindString("displayLabel"));
    ASSERT_TRUE(info->FindString("controllerKind"));
    ASSERT_TRUE(info->FindString("id"));
    EXPECT_EQ(*info->FindString("displayLabel"), test.expected_label);
    EXPECT_EQ(*info->FindString("controllerKind"), test.expected_kind);
    EXPECT_EQ(*info->FindString("id"), candidate->session_id());
  }
}

TEST_F(MahoMcpSessionTest,
       AutonomousTrustedCliInitializeAcknowledgesModeAndAgentLabel) {
  auto agent = MakeTrustedCliTestSession();
  auto responses = InitializeCliRaw(agent.get(), true, 901);
  ASSERT_EQ(responses.size(), 1u);
  base::Value response = ParseLine(responses[0]);
  const base::DictValue* result = response.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const base::DictValue* info = result->FindDict("sessionInfo");
  ASSERT_TRUE(info);
  ASSERT_TRUE(info->FindString("controllerKind"));
  ASSERT_TRUE(info->FindString("displayLabel"));
  EXPECT_EQ(*info->FindString("controllerKind"), "maho-cli");
  EXPECT_EQ(*info->FindString("displayLabel"), "Maho Agent");
  EXPECT_EQ(info->FindBool("autonomous"), true);
  EXPECT_TRUE(agent->autonomous());

  auto third_party = MakeTestSession();
  responses = third_party->ProcessData(
      R"({"jsonrpc":"2.0","method":"initialize","id":902,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("controllerKind":"third-party","autonomous":true,)"
      R"("clientInfo":{"name":"external","version":"1"}}})"
      "\n");
  ASSERT_EQ(responses.size(), 1u);
  response = ParseLine(responses[0]);
  result = response.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  info = result->FindDict("sessionInfo");
  ASSERT_TRUE(info);
  ASSERT_TRUE(info->FindString("controllerKind"));
  ASSERT_TRUE(info->FindString("displayLabel"));
  EXPECT_EQ(*info->FindString("controllerKind"), "third-party");
  EXPECT_EQ(*info->FindString("displayLabel"), "External MCP");
  EXPECT_EQ(info->FindBool("autonomous"), true);
}

TEST_F(MahoMcpSessionTest, InitializeRejectsNonBooleanAutonomous) {
  auto responses = session_->ProcessData(
      R"({"jsonrpc":"2.0","method":"initialize","id":903,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("autonomous":"yes",)"
      R"("clientInfo":{"name":"test","version":"1"}}})"
      "\n");
  ASSERT_EQ(responses.size(), 1u);
  base::Value response = ParseLine(responses[0]);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code"), -32602);
  ASSERT_TRUE(error->FindString("message"));
  EXPECT_EQ(*error->FindString("message"),
            "Invalid params: autonomous must be boolean");
  EXPECT_EQ(session_->state(), MahoMcpSession::State::kClosed);
  EXPECT_FALSE(session_->autonomous());
}

TEST_F(MahoMcpSessionTest, DevelopmentBundleCliIsTrusted) {
#if BUILDFLAG(IS_APPLE)
  auto development_cli = std::make_unique<MahoMcpSession>(
      getuid(), nullptr,
      "/Users/developer/maho/out/Default/Maho.app/Contents/Helpers/maho",
      true);
  const auto responses = development_cli->ProcessData(
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("controllerKind":"maho-cli",)"
      R"("clientInfo":{"name":"Maho CLI","version":"1"}}})"
      "\n");
  ASSERT_EQ(responses.size(), 1u);
  const base::Value response = ParseLine(responses[0]);
  const base::DictValue* info =
      response.GetDict().FindDict("result")->FindDict("sessionInfo");
  ASSERT_TRUE(info);
  ASSERT_TRUE(info->FindString("controllerKind"));
  EXPECT_EQ(*info->FindString("controllerKind"), "maho-cli");
#endif
}

TEST_F(MahoMcpSessionTest, MatchingPathWithoutAuthenticatedPeerIsRejected) {
#if BUILDFLAG(IS_WIN)
  auto untrusted = std::make_unique<MahoMcpSession>(
      static_cast<MahoMcpSessionToken*>(nullptr), nullptr,
      "C:\\Program Files\\Maho\\maho.exe");
#elif BUILDFLAG(IS_APPLE)
  auto untrusted = std::make_unique<MahoMcpSession>(
      getuid(), nullptr, "/Applications/Maho.app/Contents/Helpers/maho");
#else
  auto untrusted =
      std::make_unique<MahoMcpSession>(getuid(), nullptr, "/usr/bin/maho");
#endif
  const auto responses = untrusted->ProcessData(
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("controllerKind":"maho-cli",)"
      R"("clientInfo":{"name":"Maho CLI","version":"1"}}})"
      "\n");
  ASSERT_EQ(responses.size(), 1u);
  const base::Value response = ParseLine(responses[0]);
  const base::DictValue* info =
      response.GetDict().FindDict("result")->FindDict("sessionInfo");
  ASSERT_TRUE(info);
  ASSERT_TRUE(info->FindString("controllerKind"));
  EXPECT_EQ(*info->FindString("controllerKind"), "third-party");
}

TEST_F(MahoMcpSessionTest, SpoofedDisplayAndHintHaveNoAuthorityEffect) {
#if BUILDFLAG(IS_WIN)
  auto untrusted = std::make_unique<MahoMcpSession>(
      static_cast<MahoMcpSessionToken *>(nullptr), nullptr,
      "C:\\Tools\\arbitrary-host.exe");
#else
  auto untrusted = std::make_unique<MahoMcpSession>(
      getuid(), nullptr, "/usr/local/bin/arbitrary-host");
#endif
  auto responses = untrusted->ProcessData(
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("controllerKind":"maho-cli",)"
      R"("clientInfo":{"name":"Maho CLI","version":"1"}}})"
      "\n");
  ASSERT_EQ(responses.size(), 1u);
  base::Value response = ParseLine(responses[0]);
  const base::DictValue *info =
      response.GetDict().FindDict("result")->FindDict("sessionInfo");
  ASSERT_TRUE(info);
  ASSERT_TRUE(info->FindString("displayLabel"));
  ASSERT_TRUE(info->FindString("controllerKind"));
  EXPECT_EQ(*info->FindString("displayLabel"), "External MCP");
  EXPECT_EQ(*info->FindString("controllerKind"), "third-party");
}

TEST_F(MahoMcpSessionTest, TrustedBasenameOutsideInstallLocationIsRejected) {
#if BUILDFLAG(IS_WIN)
  auto untrusted = std::make_unique<MahoMcpSession>(
      static_cast<MahoMcpSessionToken*>(nullptr), nullptr,
      "C:\\Users\\Public\\maho.exe", true);
#else
  auto untrusted = std::make_unique<MahoMcpSession>(
      getuid(), nullptr, "/tmp/maho", true);
#endif
  const auto responses = untrusted->ProcessData(
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("controllerKind":"maho-cli",)"
      R"("clientInfo":{"name":"Maho CLI","version":"1"}}})"
      "\n");
  ASSERT_EQ(responses.size(), 1u);
  const base::Value response = ParseLine(responses[0]);
  const base::DictValue* info =
      response.GetDict().FindDict("result")->FindDict("sessionInfo");
  ASSERT_TRUE(info);
  EXPECT_EQ(*info->FindString("displayLabel"), "External MCP");
  EXPECT_EQ(*info->FindString("controllerKind"), "third-party");
}

TEST_F(MahoMcpSessionTest, InitializeDiagnosticsExcludePrivateCapabilityIds) {
  const auto responses = session_->ProcessData(
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("clientInfo":{"name":"test-client","version":"1"}}})"
      "\n");
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].find("control.origin.grant"), std::string::npos);
  EXPECT_EQ(responses[0].find("controlPlane"), std::string::npos);
  EXPECT_EQ(responses[0].find("desktopAgent"), std::string::npos);
}

TEST_F(MahoMcpSessionTest, InitializeSucceedsForSameUid) {
  std::string input =
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("clientInfo":{"name":"test-client","version":"0.1.0"}}})"
      "\n";

  auto responses = session_->ProcessData(input);
  ASSERT_EQ(responses.size(), 1u);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(responses[0], base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());
  ASSERT_TRUE(parsed->is_dict());

  const auto &dict = parsed->GetDict();
  EXPECT_EQ(*dict.FindString("jsonrpc"), "2.0");
  EXPECT_FALSE(dict.FindDict("error"));

  const auto *result = dict.FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_EQ(session_->state(), MahoMcpSession::State::kActive);
}

TEST_F(MahoMcpSessionTest, InitializeReturnsCapabilities) {
  std::string input =
      R"({"jsonrpc":"2.0","method":"initialize","id":"init-1",)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("clientInfo":{"name":"test-client","version":"0.1.0"}}})"
      "\n";

  auto responses = session_->ProcessData(input);
  ASSERT_EQ(responses.size(), 1u);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(responses[0], base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());

  const auto *result = parsed->GetDict().FindDict("result");
  ASSERT_TRUE(result);

  // Verify serverInfo.
  const auto *server_info = result->FindDict("serverInfo");
  ASSERT_TRUE(server_info);
  EXPECT_EQ(*server_info->FindString("name"), "maho-browser");
  EXPECT_EQ(*server_info->FindString("version"), "0.4.1");

  // Verify protocolVersion.
  EXPECT_EQ(*result->FindString("protocolVersion"), "2025-03-26");

  // Verify capabilities contains tools.
  const auto *capabilities = result->FindDict("capabilities");
  ASSERT_TRUE(capabilities);
  EXPECT_TRUE(capabilities->FindDict("tools"));
}

TEST_F(MahoMcpSessionTest, ToolsListAfterInitialize) {
  // First: initialize.
  std::string init_input =
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("clientInfo":{"name":"test-client","version":"0.1.0"}}})"
      "\n";
  session_->ProcessData(init_input);
  ASSERT_EQ(session_->state(), MahoMcpSession::State::kActive);

  // Then: tools/list.
  std::string tools_input = R"({"jsonrpc":"2.0","method":"tools/list","id":2})"
                            "\n";
  auto responses = session_->ProcessData(tools_input);
  ASSERT_EQ(responses.size(), 1u);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(responses[0], base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());

  const auto *result = parsed->GetDict().FindDict("result");
  ASSERT_TRUE(result);
  // tools/list returns a "tools" array (empty for now — Wave 2A adds tools).
  EXPECT_TRUE(result->FindList("tools"));
}

TEST_F(MahoMcpSessionTest, ControlListExposesOnlyControlPlaneCapabilities) {
  auto delegate = std::make_unique<ResolvingFakeDelegate>();
  MahoMcpSession::SetBrowserDelegate(delegate.get());
  session_ = MakeTrustedCliTestSession();
  const std::string init =
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("controllerKind":"maho-cli",)"
      R"("clientInfo":{"name":"maho-cli","version":"0.1.0"}}})"
      "\n";
  ASSERT_EQ(session_->ProcessData(init).size(), 1u);

  const auto responses = session_->ProcessData(
      R"({"jsonrpc":"2.0","method":"maho/control/list","id":2})"
      "\n");
  ASSERT_EQ(responses.size(), 1u);
  const auto parsed = ParseLine(responses[0]);
  const auto* result = parsed.GetDict().FindDict("result");
  ASSERT_TRUE(result) << responses[0];
  const auto* tools = result->FindList("tools");
  ASSERT_TRUE(tools);
  ASSERT_EQ(tools->size(), 21u);
  const std::set<std::string> expected = {
      "artifact.export",            "artifact.list",
      "vault_list_credentials_for_active_page",
      "browser_acquire_lease",      "browser_adopt_tab",
      "browser_grant_exact_origin", "browser_heartbeat_lease",
      "browser_list_exact_origins", "browser_release_lease",
      "browser_release_tab",        "browser_revoke_exact_origin",
      "mail_add_account",           "mail_complete_oauth",
      "mail_delete_account",        "mail_import_migration_archive",
      "mail_reconnect_account",     "mail_start_oauth",
      "mail_test_connection",       "vault_fill_credential",
      "vault_fill_totp",            "vault_request_credential_use",
  };
  std::set<std::string> actual;
  for (const auto& tool : *tools) {
    const auto* name = tool.GetDict().FindString("name");
    ASSERT_TRUE(name);
    actual.insert(*name);
  }
  EXPECT_EQ(actual, expected);
}

TEST_F(MahoMcpSessionTest, ControlListRejectsUntrustedController) {
  const std::string init =
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("clientInfo":{"name":"third-party","version":"0.1.0"}}})"
      "\n";
  ASSERT_EQ(session_->ProcessData(init).size(), 1u);
  const auto responses = session_->ProcessData(
      R"({"jsonrpc":"2.0","method":"maho/control/list","id":2})"
      "\n");
  ASSERT_EQ(responses.size(), 1u);
  const auto parsed = ParseLine(responses[0]);
  EXPECT_EQ(parsed.GetDict().FindDict("error")->FindInt("code").value(),
            -32003);
}

TEST_F(MahoMcpSessionTest, PublicToolCallRejectsControlPlaneCapability) {
  const std::string init =
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("clientInfo":{"name":"test-client","version":"0.1.0"}}})"
      "\n";
  ASSERT_EQ(session_->ProcessData(init).size(), 1u);

  const auto responses = session_->ProcessData(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_acquire_lease","arguments":{"tab_id":1}}})"
      "\n");
  ASSERT_EQ(responses.size(), 1u);
  const auto parsed = ParseLine(responses[0]);
  const auto* error = parsed.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32601);
}

TEST_F(MahoMcpSessionTest, RevokedControllerCannotReacquireAuthority) {
  const auto initialized = session_->ProcessData(
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("clientInfo":{"name":"test-client","version":"0.1.0"}}})"
      "\n");
  ASSERT_EQ(initialized.size(), 1u);
  ASSERT_EQ(session_->state(), MahoMcpSession::State::kActive);

  MahoMcpSession::RevokeControllerSession(session_->session_id());

  const auto responses = session_->ProcessData(
      R"({"jsonrpc":"2.0","method":"tools/list","id":2})"
      "\n");
  ASSERT_EQ(responses.size(), 1u);
  const auto response = ParseLine(responses.front());
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(*error->FindInt("code"), -32004);
  EXPECT_EQ(*error->FindString("message"), "Controller session was revoked");
}

TEST_F(MahoMcpSessionTest, AuthorityChangingControlCallRequiresApproval) {
  InitResolvingSession();
  delegate_->approve_browser_actions = false;

  const auto responses = CallControlRaw(
      "browser_grant_exact_origin", R"({"origin":"https://example.com"})",
      2);
  ASSERT_EQ(responses.size(), 1u);
  const base::Value response = ParseLine(responses[0]);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code"), kMahoMcpErrorApprovalDenied);
  EXPECT_EQ(delegate_->browser_action_approval_calls, 1);
}

TEST_F(MahoMcpSessionTest, JsonRpcErrorsAreRedactedAtEgress) {
  const auto initialized = session_->ProcessData(
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("clientInfo":{"name":"test-client","version":"1"}}})"
      "\n");
  ASSERT_EQ(initialized.size(), 1u);
  const auto responses = session_->ProcessData(
      R"({"jsonrpc":"2.0","method":"unknown","id":"password=S3NTINEL-error-secret"})"
      "\n");
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].find("S3NTINEL-error-secret"), std::string::npos);
  EXPECT_NE(responses[0].find("[REDACTED]"), std::string::npos);
}

TEST_F(MahoMcpSessionTest, RoutineTierLockUsesStructuredErrorData) {
  InitResolvingSession();
  delegate_->routine_run_error = MahoMcpBrowserDelegate::RoutineRunError::kTierLocked;
  delegate_->routine_run_payload =
      "This wording deliberately contains no subscription tier name";

  EXPECT_TRUE(
      CallToolRaw("browser_routines_run", R"({"id":"morning_briefing"})", 2)
          .empty());
  ASSERT_EQ(deferred_responses_.size(), 1u);
  const base::Value response = ParseLine(deferred_responses_[0]);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  const std::string* status =
      error->FindStringByDottedPath("data.status");
  ASSERT_TRUE(status);
  EXPECT_EQ(*status, "routine_tier_locked");
  const std::string* message = error->FindString("message");
  ASSERT_TRUE(message);
  EXPECT_EQ(*message, delegate_->routine_run_payload);
}

TEST_F(MahoMcpSessionTest, RequiredBrowserActionDenialStopsBeforeDispatch) {
  delegate_ = std::make_unique<ResolvingFakeDelegate>();
  delegate_->approve_browser_actions = false;
  MahoMcpSession::SetBrowserDelegate(delegate_.get());
  const std::string init =
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("clientInfo":{"name":"test-client","version":"0.1.0"}}})"
      "\n";
  ASSERT_EQ(session_->ProcessData(init).size(), 1u);

  const auto responses =
      CallToolRaw("browser_navigate", R"({"url":"https://example.com"})", 2);
  ASSERT_EQ(responses.size(), 1u);
  const auto parsed = ParseLine(responses[0]);
  const auto* error = parsed.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), kMahoMcpErrorApprovalDenied);
  EXPECT_EQ(delegate_->browser_action_approval_calls, 1);
  EXPECT_TRUE(delegate_->last_navigated_url.empty());
}

#if !BUILDFLAG(IS_WIN)
// Peer-UID authentication is POSIX-only; Windows rejects mismatched clients
// through MahoMcpSessionToken instead (see
// maho_mcp_session_token_win_unittest.cc).
TEST_F(MahoMcpSessionTest, RejectsPeerUidMismatch) {
  auto foreign_session = std::make_unique<MahoMcpSession>(getuid() + 1);

  std::string input =
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("clientInfo":{"name":"test-client","version":"0.1.0"}}})"
      "\n";

  auto responses = foreign_session->ProcessData(input);
  EXPECT_EQ(foreign_session->state(), MahoMcpSession::State::kClosed);
}
#endif // !BUILDFLAG(IS_WIN)

// D1 disallow list: helper to drive a session through initialize
// followed by an arbitrary payload, returning the LAST response parsed.
class MahoMcpSessionAllowedDomainsTest : public MahoMcpSessionTest {
protected:
  void InitializeSession() {
    std::string init = R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
                       R"("params":{"protocolVersion":"2025-03-26",)"
                       R"("clientInfo":{"name":"t","version":"0.1.0"}}})"
                       "\n";
    (void)session_->ProcessData(init);
    ASSERT_EQ(session_->state(), MahoMcpSession::State::kActive);
  }

  base::Value CallTool(const std::string &payload) {
    auto responses = session_->ProcessData(payload);
    EXPECT_FALSE(responses.empty());
    if (responses.empty())
      return base::Value();
    std::optional<base::Value> parsed =
        base::JSONReader::Read(responses.back(), base::JSON_PARSE_RFC);
    if (!parsed.has_value()) {
      return base::Value();
    }
    const auto* result = parsed->GetDict().FindDict("result");
    const std::string* output_json =
        result ? result->FindString("outputJson") : nullptr;
    if (!output_json) {
      return std::move(*parsed);
    }
    std::optional<base::Value> output =
        base::JSONReader::Read(*output_json, base::JSON_PARSE_RFC);
    EXPECT_TRUE(output.has_value()) << *output_json;
    if (!output) {
      return base::Value();
    }
    parsed->GetDict().Set("result", std::move(*output));
    return std::move(*parsed);
  }
};

TEST_F(MahoMcpSessionAllowedDomainsTest, NavigateAllowedWhenEmptyBlocklist) {
  InitializeSession();
  base::Value resp =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
               R"("params":{"name":"browser_navigate",)"
               R"("arguments":{"url":"https://example.com/"}}})"
               "\n");
  const auto *result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->FindBool("navigated").value(), true);
}

TEST_F(MahoMcpSessionAllowedDomainsTest,
       SetBlockedDomainsThenNavigateElsewhereOk) {
  InitializeSession();
  base::Value set_resp =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
               R"("params":{"name":"browser_set_blocked_domains",)"
               R"("arguments":{"domains":["example.com"]}}})"
               "\n");
  const auto *set_result = set_resp.GetDict().FindDict("result");
  ASSERT_TRUE(set_result);
  EXPECT_EQ(set_result->FindInt("count").value(), 1);

  base::Value nav_resp =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
               R"("params":{"name":"browser_navigate",)"
               R"("arguments":{"url":"https://example.org/foo"}}})"
               "\n");
  const auto *nav_result = nav_resp.GetDict().FindDict("result");
  ASSERT_TRUE(nav_result);
  EXPECT_EQ(nav_result->FindBool("navigated").value(), true);
}

TEST_F(MahoMcpSessionAllowedDomainsTest, WildcardMatchesSubdomainAndBare) {
  InitializeSession();
  (void)CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
                 R"("params":{"name":"browser_set_blocked_domains",)"
                 R"("arguments":{"domains":["*.github.com"]}}})"
                 "\n");

  base::Value r1 =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
               R"("params":{"name":"browser_navigate",)"
               R"("arguments":{"url":"https://api.github.com/repos"}}})"
               "\n");
  ASSERT_FALSE(r1.GetDict().FindDict("result"));

  base::Value r2 = CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":4,)"
                            R"("params":{"name":"browser_navigate",)"
                            R"("arguments":{"url":"https://github.com/"}}})"
                            "\n");
  ASSERT_FALSE(r2.GetDict().FindDict("result"));
}

TEST_F(MahoMcpSessionAllowedDomainsTest, BlockedHostRejectedAfterSet) {
  InitializeSession();
  (void)CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
                 R"("params":{"name":"browser_set_blocked_domains",)"
                 R"("arguments":{"domains":["example.com"]}}})"
                 "\n");

  base::Value resp =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
               R"("params":{"name":"browser_navigate",)"
               R"("arguments":{"url":"https://example.com/"}}})"
               "\n");
  const auto *error = resp.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32000);
  const auto *data = error->FindDict("data");
  ASSERT_TRUE(data);
  EXPECT_EQ(*data->FindString("status"), "domain_blocked");
}

TEST_F(MahoMcpSessionAllowedDomainsTest, TabNewWithUrlAllowedByDefault) {
  InitializeSession();
  base::Value resp =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
               R"("params":{"name":"browser_tab_new",)"
               R"("arguments":{"url":"https://example.com/"}}})"
               "\n");
  ASSERT_TRUE(resp.GetDict().FindDict("result"));
}

TEST_F(MahoMcpSessionAllowedDomainsTest, TabNewWithUrlBlockedWhenDisallowed) {
  InitializeSession();
  (void)CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
                 R"("params":{"name":"browser_set_blocked_domains",)"
                 R"("arguments":{"domains":["example.com"]}}})"
                 "\n");
  base::Value resp =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
               R"("params":{"name":"browser_tab_new",)"
               R"("arguments":{"url":"https://example.com/"}}})"
               "\n");
  const auto *error = resp.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32000);
}

TEST_F(MahoMcpSessionAllowedDomainsTest, TabNewWithoutUrlUnaffected) {
  InitializeSession();
  base::Value resp =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
               R"("params":{"name":"browser_tab_new",)"
               R"("arguments":{}}})"
               "\n");
  ASSERT_TRUE(resp.GetDict().FindDict("result"));
}

class MahoMcpSessionNewFeaturesTest : public MahoMcpSessionAllowedDomainsTest {
protected:
  void SetUp() override {
    MahoMcpSessionAllowedDomainsTest::SetUp();
    InitializeSession();
  }
};

class FakeAsyncNetworkDelegate : public MahoMcpBrowserDelegate {
public:
  struct PendingStart {
    std::string capture_id;
    int requested_tab_id = 0;
    int resolved_tab_id = 0;
    bool exists = false;
    StartNetworkCaptureCallback callback;
  };

  struct PendingStop {
    std::string capture_id;
    StopNetworkCaptureCallback callback;
  };

  FakeAsyncNetworkDelegate() {
    tabs_.push_back(MakeTab(7, true));
    tabs_.push_back(MakeTab(11, false));
  }

  std::vector<MahoMcpSession::TabInfo> GetTabList() override {
    ++get_tab_list_calls_;
    return tabs_;
  }

  std::vector<MahoMcpSession::ConsoleMessage>
  GetConsoleMessages(int tab_id) override {
    return {};
  }

  std::vector<MahoMcpSession::NavigationEvent>
  GetNavigationEvents(int tab_id, int64_t since_ms) override {
    return {};
  }

  std::string GetPageText(int tab_id) override { return std::string(); }

  base::Value
  GetAccessibilitySnapshot(int tab_id,
                           MahoMcpSession::RefTable *out_refs) override {
    return base::Value(base::DictValue());
  }

  void CaptureFullPagePngBase64(
      int tab_id, base::OnceCallback<void(std::string)> callback) override {
    std::move(callback).Run(std::string());
  }

  bool Scroll(int tab_id, const std::string &direction, int pixels,
              std::optional<ui::AXNodeID> ax_id) override {
    return false;
  }
  bool Click(int tab_id, ui::AXNodeID ax_id) override { return false; }
  bool Type(int tab_id, ui::AXNodeID ax_id, const std::string &text) override {
    return false;
  }
  bool Select(int tab_id, ui::AXNodeID ax_id,
              const std::string &value) override {
    return false;
  }
  bool Hover(int tab_id, ui::AXNodeID ax_id) override { return false; }
  bool KeyPress(int tab_id, const std::string &key,
                const std::vector<std::string> &modifiers) override {
    return false;
  }
  bool ActivateTab(int tab_id) override { return false; }
  bool Navigate(int tab_id, const GURL &url) override { return false; }
  bool SetViewportSize(int tab_id, int width, int height) override {
    return false;
  }

  PageContentResult GetPageContent(int tab_id) override { return {}; }
  PageContextResult GetPageContext(int tab_id) override { return {}; }
  SearchResult SearchInPage(int tab_id, const std::string &query) override {
    return {};
  }
  QuerySelectorResult QuerySelector(int tab_id,
                                    const std::string &selector) override {
    return {};
  }
  std::string GetElementText(int tab_id, const std::string &ref_id) override {
    return std::string();
  }
  std::string GetElementAttribute(int tab_id, const std::string &ref_id,
                                  const std::string &attribute) override {
    return std::string();
  }
  int CreateNewTab(const GURL &url) override { return 0; }
  bool CloseTab(int tab_id) override { return false; }
  std::vector<BookmarkInfo> SearchBookmarks(const std::string &query) override {
    return {};
  }
  BookmarkInfo CreateBookmark(const std::string &title, const GURL &url,
                              const std::string &folder) override {
    return BookmarkInfo();
  }
  void CaptureElementPngBase64(
      int tab_id, ui::AXNodeID ax_id,
      base::OnceCallback<void(std::string)> callback) override {
    std::move(callback).Run(std::string());
  }

  bool WaitForSelector(int tab_id, const std::string &selector,
                       int timeout_ms) override {
    ++wait_selector_call_count_;
    last_wait_selector_tab_id_ = tab_id;
    last_wait_selector_selector_ = selector;
    last_wait_selector_timeout_ms_ = timeout_ms;
    return wait_selector_return_;
  }

  void set_wait_selector_return(bool value) { wait_selector_return_ = value; }
  int wait_selector_call_count() const { return wait_selector_call_count_; }
  int last_wait_selector_tab_id() const { return last_wait_selector_tab_id_; }
  const std::string &last_wait_selector_selector() const {
    return last_wait_selector_selector_;
  }
  int last_wait_selector_timeout_ms() const {
    return last_wait_selector_timeout_ms_;
  }

  std::vector<HistoryEntry> SearchHistory(const std::string &query,
                                          size_t max_results) override {
    ++search_history_call_count_;
    last_search_history_query_ = query;
    last_search_history_max_results_ = max_results;
    return search_history_return_;
  }

  void set_search_history_return(std::vector<HistoryEntry> entries) {
    search_history_return_ = std::move(entries);
  }
  int search_history_call_count() const { return search_history_call_count_; }
  const std::string &last_search_history_query() const {
    return last_search_history_query_;
  }
  size_t last_search_history_max_results() const {
    return last_search_history_max_results_;
  }

  void StartNetworkCapture(const std::string &capture_id, int tab_id,
                           const ResolvedMahoMcpTarget &target,
                           StartNetworkCaptureCallback callback) override {
    int resolved = tab_id;
    if (resolved == 0) {
      for (const auto &t : tabs_) {
        if (t.is_active) {
          resolved = t.id;
          break;
        }
      }
    }
    bool exists = false;
    for (const auto &t : tabs_) {
      if (t.id == resolved) {
        exists = true;
        break;
      }
    }
    pending_starts_.push_back(PendingStart{capture_id, tab_id, resolved, exists,
                                           std::move(callback)});
  }

  void StopNetworkCapture(const std::string &capture_id,
                          const ResolvedMahoMcpTarget &target,
                          StopNetworkCaptureCallback callback) override {
    pending_stops_.push_back(PendingStop{capture_id, std::move(callback)});
  }

  void CancelNetworkCapture(const std::string &capture_id) override {
    canceled_capture_ids_.push_back(capture_id);
  }

  void SetActiveTab(int tab_id) {
    for (auto &tab : tabs_) {
      tab.is_active = tab.id == tab_id;
    }
  }

  PendingStart TakeStart() {
    PendingStart start = std::move(pending_starts_.front());
    pending_starts_.pop_front();
    return start;
  }

  PendingStop TakeStop() {
    PendingStop stop = std::move(pending_stops_.front());
    pending_stops_.pop_front();
    return stop;
  }

  size_t pending_start_count() const { return pending_starts_.size(); }
  size_t pending_stop_count() const { return pending_stops_.size(); }
  int get_tab_list_calls() const { return get_tab_list_calls_; }
  const std::vector<std::string> &canceled_capture_ids() const {
    return canceled_capture_ids_;
  }

  void SetTabsForTesting(std::vector<MahoMcpSession::TabInfo> tabs) {
    tabs_ = std::move(tabs);
  }

private:
  static MahoMcpSession::TabInfo MakeTab(int id, bool active) {
    MahoMcpSession::TabInfo tab;
    tab.id = id;
    tab.title = base::StringPrintf("tab %d", id);
    tab.url = base::StringPrintf("https://example.com/%d", id);
    tab.is_active = active;
    return tab;
  }

  std::vector<MahoMcpSession::TabInfo> tabs_;
  std::deque<PendingStart> pending_starts_;
  std::deque<PendingStop> pending_stops_;
  std::vector<std::string> canceled_capture_ids_;
  int get_tab_list_calls_ = 0;

  bool wait_selector_return_ = false;
  int wait_selector_call_count_ = 0;
  int last_wait_selector_tab_id_ = -1;
  std::string last_wait_selector_selector_;
  int last_wait_selector_timeout_ms_ = -1;

  std::vector<HistoryEntry> search_history_return_;
  int search_history_call_count_ = 0;
  std::string last_search_history_query_;
  size_t last_search_history_max_results_ = 0;
};

TEST_F(MahoMcpSessionTest, BrowserTabListSerializesVisibleSidebarInventory) {
  // Given one live tab and two unloaded sidebar entries.
  auto delegate = std::make_unique<FakeAsyncNetworkDelegate>();

  MahoMcpSession::TabInfo live_tab;
  live_tab.id = 1;
  live_tab.stable_id = "stable-live";
  live_tab.title = "Live tab";
  live_tab.url = "https://example.com/live";
  live_tab.is_active = true;
  live_tab.targetable = true;
  live_tab.tab_strip_index = 0;

  MahoMcpSession::TabInfo suspended_tab;
  suspended_tab.id = 0;
  suspended_tab.stable_id = "stable-suspended";
  suspended_tab.title = "Suspended sidebar tab";
  suspended_tab.url = "https://example.com/suspended";
  suspended_tab.is_active = false;
  suspended_tab.targetable = false;
  suspended_tab.tab_strip_index = -1;

  MahoMcpSession::TabInfo sidebar_only_tab;
  sidebar_only_tab.id = 0;
  sidebar_only_tab.stable_id = "stable-sidebar-only";
  sidebar_only_tab.title = "Sidebar-only tab";
  sidebar_only_tab.url = "https://example.com/sidebar-only";
  sidebar_only_tab.is_active = false;
  sidebar_only_tab.targetable = false;
  sidebar_only_tab.tab_strip_index = -1;

  delegate->SetTabsForTesting({live_tab, suspended_tab, sidebar_only_tab});
  MahoMcpSession::SetBrowserDelegate(delegate.get());

  session_->ProcessData(
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("clientInfo":{"name":"test-client","version":"0.1.0"}}})"
      "\n");
  ASSERT_EQ(session_->state(), MahoMcpSession::State::kActive);

  // When the sidebar inventory crosses the MCP response boundary.
  auto responses = CallToolRaw("browser_tab_list", "{}", 2);
  ASSERT_EQ(responses.size(), 1u);
  base::Value parsed = ParseLine(responses[0]);
  const auto *result = parsed.GetDict().FindDict("result");
  ASSERT_TRUE(result) << responses[0];
  const std::string* output_json = result->FindString("outputJson");
  ASSERT_TRUE(output_json) << responses[0];
  std::optional<base::Value> output =
      base::JSONReader::Read(*output_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(output.has_value()) << *output_json;
  const auto* tabs = output->GetDict().FindList("tabs");
  ASSERT_TRUE(tabs) << responses[0];
  const auto* receipt = result->FindDict("receipt");
  ASSERT_TRUE(receipt);
  EXPECT_EQ(*receipt->FindString("capabilityId"), "tab.list");
  ASSERT_EQ(tabs->size(), 3u);

  // Then unloaded entries never impersonate the live tab or become active.
  const auto &live = (*tabs)[0].GetDict();
  EXPECT_EQ(live.FindInt("id").value(), 1);
  EXPECT_EQ(*live.FindString("stable_id"), "stable-live");
  EXPECT_EQ(live.FindBool("is_active"), true);
  EXPECT_EQ(live.FindBool("targetable").value(), true);
  EXPECT_EQ(live.FindInt("tab_strip_index").value(), 0);

  const auto &suspended = (*tabs)[1].GetDict();
  EXPECT_EQ(suspended.FindInt("id").value(), 0);
  EXPECT_EQ(*suspended.FindString("stable_id"), "stable-suspended");
  EXPECT_EQ(suspended.FindBool("is_active"), false);
  EXPECT_EQ(suspended.FindBool("targetable").value(), false);
  EXPECT_EQ(suspended.FindInt("tab_strip_index").value(), -1);

  const auto &sidebar_only = (*tabs)[2].GetDict();
  EXPECT_EQ(sidebar_only.FindInt("id").value(), 0);
  EXPECT_EQ(*sidebar_only.FindString("stable_id"), "stable-sidebar-only");
  EXPECT_EQ(sidebar_only.FindBool("is_active"), false);
  EXPECT_EQ(sidebar_only.FindBool("targetable").value(), false);
  EXPECT_EQ(sidebar_only.FindInt("tab_strip_index").value(), -1);
}

class FakeMailReadDelegate : public FakeAsyncNetworkDelegate {
public:
  ai::MailAuthorizationContext authorization_context{
      true, true, false, true, ai::MailGlobalPolicy::kPrompt};
  int approval_count = 0;

  ai::MailAuthorizationContext GetMailAuthorizationContext() override {
    return authorization_context;
  }

  bool ConfirmMailToolApproval(std::string_view,
                               std::string_view redacted_arguments) override {
    ++approval_count;
    EXPECT_EQ(redacted_arguments, "[REDACTED]");
    return true;
  }

  enum class MailReadMethod {
    kListAccounts,
    kListFolders,
    kListEmails,
    kGetEmail,
    kSearchEmails,
    kListThread,
  };

  struct PendingMailRead {
    MailReadMethod method = MailReadMethod::kListAccounts;
    std::string account_id;
    std::string folder_id;
    std::string email_id;
    std::string query_json;
    std::string message_id;
    int64_t limit = 0;
    int64_t offset = 0;
    MailReadCallback callback;
  };

  void MailListAccounts(MailReadCallback callback) override {
    PendingMailRead pending;
    pending.method = MailReadMethod::kListAccounts;
    pending.callback = std::move(callback);
    pending_mail_reads_.push_back(std::move(pending));
  }

  void MailListFolders(const std::string &account_id,
                       MailReadCallback callback) override {
    PendingMailRead pending;
    pending.method = MailReadMethod::kListFolders;
    pending.account_id = account_id;
    pending.callback = std::move(callback);
    pending_mail_reads_.push_back(std::move(pending));
  }

  void MailListEmails(const std::string &account_id,
                      const std::string &folder_id, int64_t limit,
                      int64_t offset, MailReadCallback callback) override {
    PendingMailRead pending;
    pending.method = MailReadMethod::kListEmails;
    pending.account_id = account_id;
    pending.folder_id = folder_id;
    pending.limit = limit;
    pending.offset = offset;
    pending.callback = std::move(callback);
    pending_mail_reads_.push_back(std::move(pending));
  }

  void MailGetEmail(const std::string &email_id,
                    MailReadCallback callback) override {
    PendingMailRead pending;
    pending.method = MailReadMethod::kGetEmail;
    pending.email_id = email_id;
    pending.callback = std::move(callback);
    pending_mail_reads_.push_back(std::move(pending));
  }

  void MailSearchEmails(const std::string &query_json,
                        MailReadCallback callback) override {
    PendingMailRead pending;
    pending.method = MailReadMethod::kSearchEmails;
    pending.query_json = query_json;
    pending.callback = std::move(callback);
    pending_mail_reads_.push_back(std::move(pending));
  }

  void MailListThread(const std::string &account_id,
                      const std::string &message_id,
                      MailReadCallback callback) override {
    PendingMailRead pending;
    pending.method = MailReadMethod::kListThread;
    pending.account_id = account_id;
    pending.message_id = message_id;
    pending.callback = std::move(callback);
    pending_mail_reads_.push_back(std::move(pending));
  }

  PendingMailRead TakePendingMailRead() {
    PendingMailRead pending = std::move(pending_mail_reads_.front());
    pending_mail_reads_.pop_front();
    return pending;
  }

  size_t pending_mail_read_count() const { return pending_mail_reads_.size(); }

private:
  std::deque<PendingMailRead> pending_mail_reads_;
};

class MahoMcpSessionMailReadTest : public testing::Test {
protected:
  void SetUp() override {
    delegate_ = std::make_unique<FakeMailReadDelegate>();
    MahoMcpSession::SetBrowserDelegate(delegate_.get());
    session_ = MakeTestSession();
    session_->SetDeferredResponseSender(base::BindRepeating(
        &MahoMcpSessionMailReadTest::CaptureDeferred, base::Unretained(this)));
    auto responses = session_->ProcessData(
        R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
        R"("params":{"protocolVersion":"2025-03-26",)"
        R"("clientInfo":{"name":"test-client","version":"0.1.0"}}})"
        "\n");
    ASSERT_EQ(responses.size(), 1u);
    ASSERT_EQ(session_->state(), MahoMcpSession::State::kActive);
  }

  void TearDown() override {
    MahoMcpSession::SetBrowserDelegate(nullptr);
    session_.reset();
    delegate_.reset();
  }

  std::vector<std::string> CallToolRaw(const std::string &name,
                                       const std::string &args, int id) {
    return session_->ProcessData(
        base::StringPrintf(R"({"jsonrpc":"2.0","method":"tools/call","id":%d,)"
                           R"("params":{"name":"%s","arguments":%s}})"
                           "\n",
                           id, name.c_str(), args.c_str()));
  }

  base::Value ParseLine(const std::string &line) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(line, base::JSON_PARSE_RFC);
    EXPECT_TRUE(parsed.has_value());
    return parsed.has_value() ? std::move(*parsed) : base::Value();
  }

  void CaptureDeferred(std::string line) {
    deferred_responses_.push_back(std::move(line));
  }

  void ExpectImmediateError(const std::vector<std::string> &responses,
                            int expected_code) {
    ASSERT_EQ(responses.size(), 1u);
    base::Value response = ParseLine(responses.front());
    const auto *error = response.GetDict().FindDict("error");
    ASSERT_TRUE(error);
    EXPECT_EQ(error->FindInt("code").value(), expected_code);
  }

  void ExpectSingleDeferredTextPayload(const std::string &expected_json) {
    ASSERT_EQ(deferred_responses_.size(), 1u);
    base::Value response = ParseLine(deferred_responses_.back());
    const auto *result = response.GetDict().FindDict("result");
    ASSERT_TRUE(result);
    const std::string* output_json = result->FindString("outputJson");
    ASSERT_TRUE(output_json);
    std::optional<base::Value> output =
        base::JSONReader::Read(*output_json, base::JSON_PARSE_RFC);
    ASSERT_TRUE(output.has_value());
    const auto* content = output->GetDict().FindList("content");
    ASSERT_TRUE(content);
    ASSERT_EQ(content->size(), 1u);
    const auto *item = (*content)[0].GetIfDict();
    ASSERT_TRUE(item);
    EXPECT_EQ(*item->FindString("type"), "text");
    const std::string *text = item->FindString("text");
    ASSERT_TRUE(text);
    EXPECT_EQ(*text, expected_json);
    EXPECT_TRUE(
        base::JSONReader::Read(*text, base::JSON_PARSE_RFC).has_value());
    deferred_responses_.clear();
  }

  std::unique_ptr<FakeMailReadDelegate> delegate_;
  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<MahoMcpSession> session_;
  std::vector<std::string> deferred_responses_;
};

TEST_F(MahoMcpSessionMailReadTest,
       MailRequiredArgumentValidationRejectsMissingOrWrongTypes) {
  struct Case {
    const char *tool;
    const char *args;
  };
  const std::vector<Case> cases = {
      {"mail_list_accounts", "17"},
      {"mail_list_folders", R"({})"},
      {"mail_list_folders", R"({"account_id":17})"},
      {"mail_list_emails", R"({"folder_id":"inbox"})"},
      {"mail_list_emails", R"({"account_id":"acct"})"},
      {"mail_list_emails", R"({"account_id":17,"folder_id":"inbox"})"},
      {"mail_list_emails", R"({"account_id":"acct","folder_id":17})"},
      {"mail_list_emails",
       R"({"account_id":"acct","folder_id":"inbox","limit":"many"})"},
      {"mail_get_email", R"({})"},
      {"mail_get_email", R"({"email_id":17})"},
      {"mail_search_emails", R"({})"},
      {"mail_search_emails", R"({"query":17})"},
      {"mail_search_emails", R"({"query":"q","offset":"later"})"},
      {"mail_list_thread", R"({"message_id":"msg"})"},
      {"mail_list_thread", R"({"account_id":"acct"})"},
      {"mail_list_thread", R"({"account_id":17,"message_id":"msg"})"},
      {"mail_list_thread", R"({"account_id":"acct","message_id":17})"},
  };

  int id = 100;
  for (const Case &c : cases) {
    auto responses = CallToolRaw(c.tool, c.args, id++);
    ExpectImmediateError(responses, -32602);
    EXPECT_EQ(delegate_->pending_mail_read_count(), 0u) << c.tool;
    EXPECT_TRUE(deferred_responses_.empty()) << c.tool;
  }
}

TEST_F(MahoMcpSessionMailReadTest,
       MailReadToolsReturnDeferredTextContentOnSuccess) {
  {
    auto responses = CallToolRaw("mail_list_accounts", R"({})", 200);
    EXPECT_TRUE(responses.empty());
    ASSERT_EQ(delegate_->pending_mail_read_count(), 1u);
    auto pending = delegate_->TakePendingMailRead();
    EXPECT_EQ(pending.method,
              FakeMailReadDelegate::MailReadMethod::kListAccounts);
    std::move(pending.callback).Run(true, R"({"accounts":[{"id":"acct"}]})");
    ExpectSingleDeferredTextPayload(R"({"accounts":[{"id":"acct"}]})");
  }

  {
    auto responses =
        CallToolRaw("mail_list_folders", R"({"account_id":"acct"})", 201);
    EXPECT_TRUE(responses.empty());
    ASSERT_EQ(delegate_->pending_mail_read_count(), 1u);
    auto pending = delegate_->TakePendingMailRead();
    EXPECT_EQ(pending.method,
              FakeMailReadDelegate::MailReadMethod::kListFolders);
    EXPECT_EQ(pending.account_id, "acct");
    std::move(pending.callback).Run(true, R"({"folders":[{"id":"inbox"}]})");
    ExpectSingleDeferredTextPayload(R"({"folders":[{"id":"inbox"}]})");
  }

  {
    auto responses =
        CallToolRaw("mail_list_emails",
                    R"({"account_id":"acct","folder_id":"inbox"})", 202);
    EXPECT_TRUE(responses.empty());
    ASSERT_EQ(delegate_->pending_mail_read_count(), 1u);
    auto pending = delegate_->TakePendingMailRead();
    EXPECT_EQ(pending.method,
              FakeMailReadDelegate::MailReadMethod::kListEmails);
    EXPECT_EQ(pending.account_id, "acct");
    EXPECT_EQ(pending.folder_id, "inbox");
    EXPECT_EQ(pending.limit, 50);
    EXPECT_EQ(pending.offset, 0);
    std::move(pending.callback).Run(true, R"({"emails":[{"id":"email"}]})");
    ExpectSingleDeferredTextPayload(R"({"emails":[{"id":"email"}]})");
  }

  {
    auto responses =
        CallToolRaw("mail_get_email", R"({"email_id":"email"})", 203);
    EXPECT_TRUE(responses.empty());
    ASSERT_EQ(delegate_->pending_mail_read_count(), 1u);
    auto pending = delegate_->TakePendingMailRead();
    EXPECT_EQ(pending.method, FakeMailReadDelegate::MailReadMethod::kGetEmail);
    EXPECT_EQ(pending.email_id, "email");
    std::move(pending.callback).Run(true, R"({"id":"email","subject":"Hi"})");
    ExpectSingleDeferredTextPayload(R"({"id":"email","subject":"Hi"})");
  }

  {
    auto responses = CallToolRaw(
        "mail_search_emails",
        R"({"query":"receipt","account_id":"acct","folder_id":"inbox",)"
        R"("limit":7,"offset":3})",
        204);
    EXPECT_TRUE(responses.empty());
    ASSERT_EQ(delegate_->pending_mail_read_count(), 1u);
    auto pending = delegate_->TakePendingMailRead();
    EXPECT_EQ(pending.method,
              FakeMailReadDelegate::MailReadMethod::kSearchEmails);

    std::optional<base::Value> search_query =
        base::JSONReader::Read(pending.query_json, base::JSON_PARSE_RFC);
    ASSERT_TRUE(search_query.has_value());
    const auto &query_dict = search_query->GetDict();
    EXPECT_EQ(*query_dict.FindString("query"), "receipt");
    EXPECT_EQ(*query_dict.FindString("account_id"), "acct");
    EXPECT_EQ(*query_dict.FindString("folder_id"), "inbox");
    EXPECT_EQ(query_dict.FindInt("limit").value(), 7);
    EXPECT_EQ(query_dict.FindInt("offset").value(), 3);

    std::move(pending.callback).Run(true, R"({"emails":[{"id":"match"}]})");
    ExpectSingleDeferredTextPayload(R"({"emails":[{"id":"match"}]})");
  }

  {
    auto responses =
        CallToolRaw("mail_list_thread",
                    R"({"account_id":"acct","message_id":"message"})", 205);
    EXPECT_TRUE(responses.empty());
    ASSERT_EQ(delegate_->pending_mail_read_count(), 1u);
    auto pending = delegate_->TakePendingMailRead();
    EXPECT_EQ(pending.method,
              FakeMailReadDelegate::MailReadMethod::kListThread);
    EXPECT_EQ(pending.account_id, "acct");
    EXPECT_EQ(pending.message_id, "message");
    std::move(pending.callback).Run(true, R"({"thread":[{"id":"message"}]})");
    ExpectSingleDeferredTextPayload(R"({"thread":[{"id":"message"}]})");
  }
}

TEST_F(MahoMcpSessionMailReadTest, MailDelegateFailureReturnsDeferredError) {
  auto responses =
      CallToolRaw("mail_get_email", R"({"email_id":"email"})", 300);

  EXPECT_TRUE(responses.empty());
  ASSERT_EQ(delegate_->pending_mail_read_count(), 1u);
  auto pending = delegate_->TakePendingMailRead();
  std::move(pending.callback).Run(false, "broker offline");

  ASSERT_EQ(deferred_responses_.size(), 1u);
  base::Value response = ParseLine(deferred_responses_.back());
  const auto *error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32000);
  EXPECT_EQ(*error->FindString("message"), "broker offline");
}

TEST_F(MahoMcpSessionMailReadTest,
       MailReadConsentRevocationCancelsPendingReadResult) {
  auto responses =
      CallToolRaw("mail_get_email", R"({"email_id":"secret-id"})", 301);
  EXPECT_TRUE(responses.empty());
  auto pending = delegate_->TakePendingMailRead();
  delegate_->authorization_context.read_allowed = false;
  std::move(pending.callback).Run(true, R"({"subject":"must-not-leak"})");

  ASSERT_EQ(deferred_responses_.size(), 1u);
  base::Value response = ParseLine(deferred_responses_.back());
  const auto *error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32008);
  EXPECT_EQ(*error->FindString("message"), "mail_read_consent_required");
  EXPECT_EQ(response.DebugString().find("must-not-leak"), std::string::npos);
}

TEST_F(MahoMcpSessionMailReadTest,
       GlobalAllowDoesNotBypassReadConsentAndDenialIsStable) {
  delegate_->authorization_context.read_allowed = false;
  delegate_->authorization_context.global_policy = ai::MailGlobalPolicy::kAllow;
  auto responses =
      CallToolRaw("mail_get_email", R"({"email_id":"secret-id"})", 302);
  ExpectImmediateError(responses, -32008);
  base::Value response = ParseLine(responses.front());
  EXPECT_EQ(*response.GetDict().FindDict("error")->FindString("message"),
            "mail_read_consent_required");
  EXPECT_EQ(delegate_->pending_mail_read_count(), 0u);
}

TEST_F(MahoMcpSessionMailReadTest,
       MailExtractOtpReturnsDeferredBrokerErrorOnly) {
  auto responses = CallToolRaw("mail_extract_otp", R"({})", 400);

  EXPECT_TRUE(responses.empty());
  ASSERT_EQ(deferred_responses_.size(), 1u);
  base::Value response = ParseLine(deferred_responses_.front());
  const auto *error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32000);
  EXPECT_EQ(*error->FindString("message"), "Mail onboarding broker unavailable");
  EXPECT_EQ(delegate_->pending_mail_read_count(), 0u);
}

class MahoMcpSessionNetworkCaptureTest : public testing::Test {
protected:
  void SetUp() override {
    delegate_ = std::make_unique<FakeAsyncNetworkDelegate>();
    MahoMcpSession::SetBrowserDelegate(delegate_.get());
    session_ = MakeTestSession();
    session_->SetDeferredResponseSender(
        base::BindRepeating(&MahoMcpSessionNetworkCaptureTest::CaptureDeferred,
                            base::Unretained(this)));
    auto responses = session_->ProcessData(
        R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
        R"("params":{"protocolVersion":"2025-03-26",)"
        R"("clientInfo":{"name":"test-client","version":"0.1.0"}}})"
        "\n");
    ASSERT_EQ(responses.size(), 1u);
    ASSERT_EQ(session_->state(), MahoMcpSession::State::kActive);
  }

  void TearDown() override {
    MahoMcpSession::SetBrowserDelegate(nullptr);
    session_.reset();
    delegate_.reset();
  }

  std::vector<std::string> CallToolRaw(const std::string &name,
                                       const std::string &args, int id) {
    return session_->ProcessData(
        base::StringPrintf(R"({"jsonrpc":"2.0","method":"tools/call","id":%d,)"
                           R"("params":{"name":"%s","arguments":%s}})"
                           "\n",
                           id, name.c_str(), args.c_str()));
  }

  base::Value ParseLine(const std::string &line) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(line, base::JSON_PARSE_RFC);
    EXPECT_TRUE(parsed.has_value());
    return std::move(*parsed);
  }

  base::Value ParseExecutionOutput(const std::string& line) {
    base::Value response = ParseLine(line);
    const auto* result = response.GetDict().FindDict("result");
    EXPECT_TRUE(result) << line;
    if (!result) {
      return base::Value(base::Value::Type::DICT);
    }
    const std::string* output_json = result->FindString("outputJson");
    EXPECT_TRUE(output_json) << line;
    if (!output_json) {
      return base::Value(base::Value::Type::DICT);
    }
    std::optional<base::Value> output =
        base::JSONReader::Read(*output_json, base::JSON_PARSE_RFC);
    EXPECT_TRUE(output.has_value()) << *output_json;
    return output.has_value() ? std::move(*output)
                              : base::Value(base::Value::Type::DICT);
  }

  base::DictValue HarWithSecretUrlAndHeaders() {
    base::DictValue request_headers;
    request_headers.Set("Authorization", "Bearer secret-token");
    request_headers.Set("Accept", "application/json");

    base::DictValue response_headers;
    response_headers.Set("Set-Cookie", "sid=secret");
    response_headers.Set("Content-Type", "application/json");

    base::DictValue request;
    request.Set("method", "GET");
    request.Set("url", "https://example.com/api?access_token=secret");
    request.Set("headers", std::move(request_headers));

    base::DictValue response;
    response.Set("status", 200);
    response.Set("headers", std::move(response_headers));

    base::DictValue entry;
    entry.Set("request", std::move(request));
    entry.Set("response", std::move(response));

    base::ListValue entries;
    entries.Append(std::move(entry));

    base::DictValue har;
    har.Set("version", "1.2");
    har.Set("entries", std::move(entries));
    return har;
  }

  void CaptureDeferred(std::string line) {
    deferred_responses_.push_back(std::move(line));
  }

  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<FakeAsyncNetworkDelegate> delegate_;
  std::unique_ptr<MahoMcpSession> session_;
  std::vector<std::string> deferred_responses_;
};

TEST_F(MahoMcpSessionNetworkCaptureTest, NetworkStartBindsExplicitTabId) {
  auto responses =
      CallToolRaw("browser_network_start_capture", R"({"tab_id":11})", 20);

  EXPECT_TRUE(responses.empty());
  ASSERT_EQ(delegate_->pending_start_count(), 1u);
  auto start = delegate_->TakeStart();
  EXPECT_EQ(start.requested_tab_id, 11);
  EXPECT_FALSE(start.capture_id.empty());

  MahoMcpBrowserDelegate::StartNetworkCaptureResult result;
  result.success = true;
  result.tab_id = start.resolved_tab_id;
  std::move(start.callback).Run(result);
  ASSERT_EQ(deferred_responses_.size(), 1u);
  base::Value output = ParseExecutionOutput(deferred_responses_.back());
  EXPECT_EQ(output.GetDict().FindBool("capturing"), true);
  EXPECT_EQ(output.GetDict().FindInt("tab_id"), 11);
}

TEST_F(MahoMcpSessionNetworkCaptureTest,
       RevokedControllerCancelsActiveCaptureAndRejectsLaterCalls) {
  auto responses =
      CallToolRaw("browser_network_start_capture", R"({"tab_id":11})", 21);
  EXPECT_TRUE(responses.empty());
  ASSERT_EQ(delegate_->pending_start_count(), 1u);
  auto start = delegate_->TakeStart();

  MahoMcpBrowserDelegate::StartNetworkCaptureResult result;
  result.success = true;
  result.tab_id = start.resolved_tab_id;
  std::move(start.callback).Run(result);
  ASSERT_EQ(deferred_responses_.size(), 1u);

  MahoMcpSession::RevokeControllerSession(session_->session_id());
  ASSERT_EQ(delegate_->canceled_capture_ids().size(), 1u);
  EXPECT_EQ(delegate_->canceled_capture_ids().front(), start.capture_id);

  responses = session_->ProcessData(
      R"({"jsonrpc":"2.0","method":"tools/list","id":22})"
      "\n");
  ASSERT_EQ(responses.size(), 1u);
  const base::Value response = ParseLine(responses.front());
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(*error->FindInt("code"), -32004);
}

TEST_F(MahoMcpSessionNetworkCaptureTest,
       RevokedControllerRejectsLateCaptureStartCompletion) {
  auto responses =
      CallToolRaw("browser_network_start_capture", R"({"tab_id":11})", 23);
  EXPECT_TRUE(responses.empty());
  ASSERT_EQ(delegate_->pending_start_count(), 1u);
  auto start = delegate_->TakeStart();

  MahoMcpSession::RevokeControllerSession(session_->session_id());
  ASSERT_EQ(delegate_->canceled_capture_ids().size(), 1u);
  EXPECT_EQ(delegate_->canceled_capture_ids().front(), start.capture_id);

  MahoMcpBrowserDelegate::StartNetworkCaptureResult result;
  result.success = true;
  result.tab_id = start.resolved_tab_id;
  std::move(start.callback).Run(result);

  ASSERT_EQ(deferred_responses_.size(), 1u);
  const base::Value response = ParseLine(deferred_responses_.front());
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(*error->FindInt("code"), -32004);
}

TEST_F(MahoMcpSessionNetworkCaptureTest,
       RevokedControllerRejectsCompletionAfterSessionDestruction) {
  auto responses =
      CallToolRaw("browser_network_start_capture", R"({"tab_id":11})", 24);
  EXPECT_TRUE(responses.empty());
  ASSERT_EQ(delegate_->pending_start_count(), 1u);
  auto start = delegate_->TakeStart();

  MahoMcpSession::RevokeControllerSession(session_->session_id());
  session_.reset();

  MahoMcpBrowserDelegate::StartNetworkCaptureResult result;
  result.success = true;
  result.tab_id = start.resolved_tab_id;
  std::move(start.callback).Run(result);

  ASSERT_EQ(delegate_->canceled_capture_ids().size(), 1u);
  EXPECT_EQ(delegate_->canceled_capture_ids().front(), start.capture_id);
  ASSERT_EQ(deferred_responses_.size(), 1u);
  const base::Value response = ParseLine(deferred_responses_.front());
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(*error->FindInt("code"), -32004);
}

TEST_F(MahoMcpSessionNetworkCaptureTest,
       NetworkStartActiveTabResolvedByDelegate) {
  auto responses = CallToolRaw("browser_network_start_capture", R"({})", 21);

  EXPECT_TRUE(responses.empty());
  ASSERT_EQ(delegate_->pending_start_count(), 1u);
  auto start = delegate_->TakeStart();
  // The session forwards an unresolved (0) tab id; the delegate resolves the
  // active tab and reports the concrete bound id back.
  EXPECT_EQ(start.requested_tab_id, 0);
  EXPECT_EQ(start.resolved_tab_id, 7);

  // Changing the active tab after start must not move the bound tab.
  delegate_->SetActiveTab(11);

  MahoMcpBrowserDelegate::StartNetworkCaptureResult result;
  result.success = true;
  result.tab_id = start.resolved_tab_id;
  std::move(start.callback).Run(result);
  ASSERT_EQ(deferred_responses_.size(), 1u);
  base::Value output = ParseExecutionOutput(deferred_responses_.back());
  EXPECT_EQ(output.GetDict().FindInt("tab_id"), 7);
}

TEST_F(MahoMcpSessionNetworkCaptureTest,
       NetworkStartDoesNotQueryTabListSynchronously) {
  const int before = delegate_->get_tab_list_calls();
  auto responses = CallToolRaw("browser_network_start_capture", R"({})", 40);

  EXPECT_TRUE(responses.empty());
  EXPECT_EQ(delegate_->get_tab_list_calls(), before);
  EXPECT_EQ(delegate_->pending_start_count(), 1u);
}

TEST_F(MahoMcpSessionNetworkCaptureTest, NetworkStartDuplicateReturnsError) {
  ASSERT_TRUE(
      CallToolRaw("browser_network_start_capture", R"({"tab_id":7})", 41)
          .empty());
  ASSERT_EQ(delegate_->pending_start_count(), 1u);

  ASSERT_TRUE(
      CallToolRaw("browser_network_start_capture", R"({"tab_id":11})", 42)
          .empty());
  EXPECT_EQ(delegate_->pending_start_count(), 1u);
  ASSERT_EQ(deferred_responses_.size(), 1u);
  base::Value response = ParseLine(deferred_responses_.back());
  const auto *error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(*error->FindString("message"), "Network capture already active");
}

TEST_F(MahoMcpSessionNetworkCaptureTest,
       NetworkStartFailureThenSuccessfulRetry) {
  ASSERT_TRUE(
      CallToolRaw("browser_network_start_capture", R"({"tab_id":999})", 43)
          .empty());
  ASSERT_EQ(delegate_->pending_start_count(), 1u);
  auto failing = delegate_->TakeStart();
  EXPECT_EQ(failing.requested_tab_id, 999);
  EXPECT_FALSE(failing.exists);

  MahoMcpBrowserDelegate::StartNetworkCaptureResult failure;
  failure.success = false;
  std::move(failing.callback).Run(failure);
  ASSERT_EQ(deferred_responses_.size(), 1u);
  EXPECT_TRUE(
      ParseLine(deferred_responses_.back()).GetDict().FindDict("error"));
  deferred_responses_.clear();

  ASSERT_TRUE(
      CallToolRaw("browser_network_start_capture", R"({"tab_id":7})", 44)
          .empty());
  ASSERT_EQ(delegate_->pending_start_count(), 1u);
  auto retry = delegate_->TakeStart();
  EXPECT_TRUE(retry.exists);
  MahoMcpBrowserDelegate::StartNetworkCaptureResult success;
  success.success = true;
  success.tab_id = retry.resolved_tab_id;
  std::move(retry.callback).Run(success);
  ASSERT_EQ(deferred_responses_.size(), 1u);
  base::Value output = ParseExecutionOutput(deferred_responses_.back());
  EXPECT_EQ(output.GetDict().FindInt("tab_id"), 7);
}

TEST_F(MahoMcpSessionNetworkCaptureTest,
       NetworkStopReturnsNoImmediateAndOneDeferredHar) {
  ASSERT_TRUE(
      CallToolRaw("browser_network_start_capture", R"({"tab_id":7})", 22)
          .empty());
  auto start = delegate_->TakeStart();
  MahoMcpBrowserDelegate::StartNetworkCaptureResult ok;
  ok.success = true;
  ok.tab_id = start.resolved_tab_id;
  std::move(start.callback).Run(ok);
  deferred_responses_.clear();

  auto stop_responses =
      CallToolRaw("browser_network_stop_capture", R"({})", 23);

  EXPECT_TRUE(stop_responses.empty());
  ASSERT_EQ(delegate_->pending_stop_count(), 1u);
  auto stop = delegate_->TakeStop();
  EXPECT_EQ(stop.capture_id, start.capture_id);

  base::DictValue har;
  har.Set("version", "1.2");
  har.Set("entries", base::ListValue());
  MahoMcpBrowserDelegate::StopNetworkCaptureResult stop_result;
  stop_result.har = MahoMcpFirewall::WrapHar(std::move(har));
  std::move(stop.callback).Run(std::move(stop_result));

  ASSERT_EQ(deferred_responses_.size(), 1u);
  base::Value response = ParseLine(deferred_responses_.back());
  EXPECT_EQ(response.GetDict().FindInt("id").value(), 23);
  base::Value output = ParseExecutionOutput(deferred_responses_.back());
  EXPECT_EQ(*output.GetDict().FindString("version"), "1.2");
}

TEST_F(MahoMcpSessionNetworkCaptureTest, NetworkStopRedactsHarAtAsyncBoundary) {
  ASSERT_TRUE(
      CallToolRaw("browser_network_start_capture", R"({"tab_id":7})", 24)
          .empty());
  auto start = delegate_->TakeStart();
  MahoMcpBrowserDelegate::StartNetworkCaptureResult ok;
  ok.success = true;
  ok.tab_id = start.resolved_tab_id;
  std::move(start.callback).Run(ok);
  deferred_responses_.clear();

  ASSERT_TRUE(CallToolRaw("browser_network_stop_capture", R"({})", 25).empty());
  auto stop = delegate_->TakeStop();
  MahoMcpBrowserDelegate::StopNetworkCaptureResult stop_result;
  stop_result.har = MahoMcpFirewall::WrapHar(HarWithSecretUrlAndHeaders());
  std::move(stop.callback).Run(std::move(stop_result));

  ASSERT_EQ(deferred_responses_.size(), 1u);
  base::Value output = ParseExecutionOutput(deferred_responses_.back());
  const auto* entries = output.GetDict().FindList("entries");
  ASSERT_TRUE(entries);
  ASSERT_EQ(entries->size(), 1u);
  const auto *entry = (*entries)[0].GetIfDict();
  ASSERT_TRUE(entry);
  const auto *request = entry->FindDict("request");
  ASSERT_TRUE(request);
  EXPECT_EQ(*request->FindString("url"),
            "https://example.com/api?access_token=[REDACTED]");
  const auto *request_headers = request->FindDict("headers");
  ASSERT_TRUE(request_headers);
  EXPECT_EQ(*request_headers->FindString("Authorization"), "[REDACTED]");
  const auto *response_headers =
      entry->FindDict("response")->FindDict("headers");
  ASSERT_TRUE(response_headers);
  EXPECT_EQ(*response_headers->FindString("Set-Cookie"), "[REDACTED]");
}

TEST_F(MahoMcpSessionNetworkCaptureTest,
       NetworkStopUnknownCaptureReturnsError) {
  ASSERT_TRUE(
      CallToolRaw("browser_network_start_capture", R"({"tab_id":7})", 45)
          .empty());
  auto start = delegate_->TakeStart();
  MahoMcpBrowserDelegate::StartNetworkCaptureResult ok;
  ok.success = true;
  ok.tab_id = start.resolved_tab_id;
  std::move(start.callback).Run(ok);
  deferred_responses_.clear();

  ASSERT_TRUE(CallToolRaw("browser_network_stop_capture", R"({})", 46).empty());
  auto stop = delegate_->TakeStop();
  // The capture id is unknown/evicted at the delegate; a missing capture must
  // surface a structured error, never an empty-HAR success.
  std::move(stop.callback).Run(
      MahoMcpBrowserDelegate::StopNetworkCaptureResult{});

  ASSERT_EQ(deferred_responses_.size(), 1u);
  base::Value response = ParseLine(deferred_responses_.back());
  EXPECT_FALSE(response.GetDict().FindDict("result"));
  const auto *error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(*error->FindString("message"),
            "Network capture not found or already stopped");
}

TEST_F(MahoMcpSessionNetworkCaptureTest,
       NetworkStopWithoutActiveCaptureErrors) {
  auto responses = CallToolRaw("browser_network_stop_capture", R"({})", 31);

  ASSERT_EQ(responses.size(), 1u);
  base::Value response = ParseLine(responses.front());
  const auto *error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(*error->FindString("message"), "No active capture to stop");
  EXPECT_EQ(delegate_->pending_stop_count(), 0u);
  EXPECT_TRUE(deferred_responses_.empty());
}

TEST_F(MahoMcpSessionNetworkCaptureTest,
       NetworkTeardownCancelsAndStartCallbackIsSafe) {
  ASSERT_TRUE(
      CallToolRaw("browser_network_start_capture", R"({"tab_id":7})", 32)
          .empty());
  auto start = delegate_->TakeStart();
  const std::string capture_id = start.capture_id;

  session_.reset();

  ASSERT_EQ(delegate_->canceled_capture_ids().size(), 1u);
  EXPECT_EQ(delegate_->canceled_capture_ids().front(), capture_id);

  // Completing the stored start callback after the session is destroyed must
  // be safe: it mutates only the refcounted shared state, never freed session
  // memory.
  MahoMcpBrowserDelegate::StartNetworkCaptureResult ok;
  ok.success = true;
  ok.tab_id = start.resolved_tab_id;
  std::move(start.callback).Run(ok);
  EXPECT_EQ(deferred_responses_.size(), 1u);
}

TEST_F(MahoMcpSessionNewFeaturesTest,
       ConsoleMessages_RingBuffer_HoldsUpToLimit) {
  for (int i = 0; i < 501; ++i) {
    MahoMcpSession::ConsoleMessage msg;
    msg.level = "log";
    msg.message = "message " + std::to_string(i);
    msg.source_url = "https://example.com";
    msg.line = i;
    msg.timestamp_ms = 1000 + i;
    session_->AddConsoleMessageForTesting(0, std::move(msg));
  }

  base::Value resp =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
               R"("params":{"name":"browser_console_messages",)"
               R"("arguments":{"tab_id":0,"limit":500}}})"
               "\n");

  const auto *result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto *messages = result->FindList("messages");
  ASSERT_TRUE(messages);
  EXPECT_EQ(messages->size(), 500u);
  const auto *first_msg = (*messages)[0].GetIfDict();
  ASSERT_TRUE(first_msg);
  EXPECT_EQ(*first_msg->FindString("message"), "message 1");
}

TEST_F(MahoMcpSessionNewFeaturesTest,
       ConsoleMessages_LevelFilter_ReturnsMatchingOnly) {
  {
    MahoMcpSession::ConsoleMessage msg;
    msg.level = "log";
    msg.message = "log message";
    session_->AddConsoleMessageForTesting(0, std::move(msg));
  }
  {
    MahoMcpSession::ConsoleMessage msg;
    msg.level = "error";
    msg.message = "error message";
    session_->AddConsoleMessageForTesting(0, std::move(msg));
  }

  base::Value resp =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
               R"("params":{"name":"browser_console_messages",)"
               R"("arguments":{"tab_id":0,"level_filter":["error"]}}})"
               "\n");

  const auto *result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto *messages = result->FindList("messages");
  ASSERT_TRUE(messages);
  EXPECT_EQ(messages->size(), 1u);
  const auto *first_msg = (*messages)[0].GetIfDict();
  ASSERT_TRUE(first_msg);
  EXPECT_EQ(*first_msg->FindString("message"), "error message");
}

TEST_F(MahoMcpSessionNewFeaturesTest,
       ConsoleMessages_SinceTimestamp_ReturnsAfterCutoff) {
  for (int i = 0; i < 3; ++i) {
    MahoMcpSession::ConsoleMessage msg;
    msg.level = "log";
    msg.message = "message " + std::to_string(i);
    msg.timestamp_ms = 1000 + i;
    session_->AddConsoleMessageForTesting(0, std::move(msg));
  }

  base::Value resp =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
               R"("params":{"name":"browser_console_messages",)"
               R"("arguments":{"tab_id":0,"since_timestamp_ms":1001}}})"
               "\n");

  const auto *result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto *messages = result->FindList("messages");
  ASSERT_TRUE(messages);
  EXPECT_EQ(messages->size(), 1u);
  const auto *first_msg = (*messages)[0].GetIfDict();
  ASSERT_TRUE(first_msg);
  EXPECT_EQ(*first_msg->FindString("message"), "message 2");
}

TEST_F(MahoMcpSessionNewFeaturesTest,
       ConsoleMessages_Firewall_ScrubsAuthorizationInMessage) {
  MahoMcpSession::ConsoleMessage msg;
  msg.level = "log";
  msg.message = "Bearer "
                "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                "eyJzdWIiOiIxMiIsInVzZXIiOiJhZG1pbiJ9.signature";
  session_->AddConsoleMessageForTesting(0, std::move(msg));

  base::Value resp =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
               R"("params":{"name":"browser_console_messages",)"
               R"("arguments":{"tab_id":0}}})"
               "\n");

  const auto *result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto *messages = result->FindList("messages");
  ASSERT_TRUE(messages);
  ASSERT_EQ(messages->size(), 1u);
  const auto *first_msg = (*messages)[0].GetIfDict();
  ASSERT_TRUE(first_msg);
  EXPECT_EQ(*first_msg->FindString("message"), "Bearer [REDACTED]");
}

TEST_F(MahoMcpSessionNewFeaturesTest,
       ConsoleMessages_Firewall_ScrubsAccessTokenInUrl) {
  MahoMcpSession::ConsoleMessage msg;
  msg.level = "log";
  msg.message = "Loaded resource";
  msg.source_url = "https://example.com/api?access_token=super-secret-token";
  session_->AddConsoleMessageForTesting(0, std::move(msg));

  base::Value resp =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
               R"("params":{"name":"browser_console_messages",)"
               R"("arguments":{"tab_id":0}}})"
               "\n");

  const auto *result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto *messages = result->FindList("messages");
  ASSERT_TRUE(messages);
  ASSERT_EQ(messages->size(), 1u);
  const auto *first_msg = (*messages)[0].GetIfDict();
  ASSERT_TRUE(first_msg);
  EXPECT_EQ(*first_msg->FindString("source_url"),
            "https://example.com/api?access_token=[REDACTED]");
}

TEST_F(MahoMcpSessionNewFeaturesTest, WaitForNavigation_ReturnsImmediately) {
  base::Value resp =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
               R"("params":{"name":"browser_wait_for_navigation",)"
               R"("arguments":{"tab_id":0}}})"
               "\n");

  const auto *result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_EQ(*result->FindString("url"), "about:blank");
}

TEST_F(MahoMcpSessionNewFeaturesTest, WaitForNavigation_TimesOutForUnknownTab) {
  base::Value resp =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
               R"("params":{"name":"browser_wait_for_navigation",)"
               R"("arguments":{"tab_id":999}}})"
               "\n");

  const auto *result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->FindBool("timeout").value());
}

TEST_F(MahoMcpSessionNewFeaturesTest, TabSwitch_UpdatesActiveTab) {
  base::Value new_tab_resp =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
               R"("params":{"name":"browser_tab_new",)"
               R"("arguments":{"url":"https://example.com/"}}})"
               "\n");
  const auto *new_tab_result = new_tab_resp.GetDict().FindDict("result");
  ASSERT_TRUE(new_tab_result);
  const auto *tab = new_tab_result->FindDict("tab");
  ASSERT_TRUE(tab);
  int new_tab_id = tab->FindInt("id").value();

  base::Value switch_resp =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":4,)"
               R"("params":{"name":"browser_tab_switch",)"
               R"("arguments":{"tab_id":0}}})"
               "\n");
  const auto *switch_result = switch_resp.GetDict().FindDict("result");
  ASSERT_TRUE(switch_result);
  EXPECT_TRUE(switch_result->FindBool("activated").value());
  EXPECT_EQ(switch_result->FindInt("previous_tab_id").value(), new_tab_id);

  base::Value get_resp =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":5,)"
               R"("params":{"name":"browser_tab_get",)"
               R"("arguments":{"tab_id":0}}})"
               "\n");
  const auto *get_result = get_resp.GetDict().FindDict("result");
  ASSERT_TRUE(get_result);
  EXPECT_TRUE(get_result->FindBool("is_active").value());
}

TEST_F(MahoMcpSessionNewFeaturesTest, SetViewportSize_ValidAndOutOfBounds) {
  base::Value resp1 =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
               R"("params":{"name":"browser_set_viewport_size",)"
               R"("arguments":{"width_px":375,"height_px":667}}})"
               "\n");
  const auto *r1 = resp1.GetDict().FindDict("result");
  ASSERT_TRUE(r1);
  EXPECT_EQ(r1->FindInt("width").value(), 375);
  EXPECT_EQ(r1->FindInt("height").value(), 667);

  base::Value resp2 =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
               R"("params":{"name":"browser_set_viewport_size",)"
               R"("arguments":{"width_px":99,"height_px":667}}})"
               "\n");
  const auto *error = resp2.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32602);
}

TEST_F(MahoMcpSessionNewFeaturesTest, InteractiveTools_Scroll_Hover_KeyPress) {
  base::Value scroll_resp =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
               R"("params":{"name":"browser_scroll",)"
               R"("arguments":{"direction":"down","pixels":200}}})"
               "\n");
  const auto *scroll_result = scroll_resp.GetDict().FindDict("result");
  ASSERT_TRUE(scroll_result);
  EXPECT_TRUE(scroll_result->FindBool("scrolled").value());
  EXPECT_EQ(scroll_result->FindInt("new_scroll_y").value(), 200);

  base::Value key_resp =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
               R"("params":{"name":"browser_key_press",)"
               R"("arguments":{"key":"Enter","modifiers":["shift"]}}})"
               "\n");
  const auto *key_result = key_resp.GetDict().FindDict("result");
  ASSERT_TRUE(key_result);
  EXPECT_TRUE(key_result->FindBool("key_pressed").value());
  EXPECT_EQ(*key_result->FindString("key"), "Enter");

  base::Value invalid_key_resp =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":4,)"
               R"("params":{"name":"browser_key_press",)"
               R"("arguments":{"key":"InvalidKeyName"}}})"
               "\n");
  const auto *key_error = invalid_key_resp.GetDict().FindDict("error");
  ASSERT_TRUE(key_error);
  EXPECT_EQ(key_error->FindInt("code").value(), -32602);
}

TEST_F(MahoMcpSessionNewFeaturesTest,
       InteractiveTools_KeyPressAcceptsSpaceForCheckboxes) {
  base::Value response =
      CallTool(R"({"jsonrpc":"2.0","method":"tools/call","id":4,)"
               R"("params":{"name":"browser_key_press",)"
               R"("arguments":{"key":"Space"}}})"
               "\n");
  const auto* result = response.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->FindBool("key_pressed").value());
  EXPECT_EQ(*result->FindString("key"), "Space");
}

TEST_F(MahoMcpSessionNetworkCaptureTest,
       WaitForSelectorForwardsArgsAndMarshalsFoundTrue) {
  delegate_->set_wait_selector_return(true);
  auto responses = CallToolRaw(
      "page_wait_for_selector",
      R"({"tab_id":7,"selector":"div.result","timeout_ms":4321})", 60);

  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(delegate_->wait_selector_call_count(), 1);
  EXPECT_EQ(delegate_->last_wait_selector_tab_id(), 7);
  EXPECT_EQ(delegate_->last_wait_selector_selector(), "div.result");
  EXPECT_EQ(delegate_->last_wait_selector_timeout_ms(), 4321);

  base::Value output = ParseExecutionOutput(responses.front());
  EXPECT_EQ(output.GetDict().FindBool("found"), true);
}

TEST_F(MahoMcpSessionNetworkCaptureTest,
       WaitForSelectorDefaultsTimeoutAndMarshalsFoundFalse) {
  delegate_->set_wait_selector_return(false);
  auto responses =
      CallToolRaw("page_wait_for_selector", R"({"selector":"#missing"})", 61);

  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(delegate_->last_wait_selector_selector(), "#missing");
  EXPECT_EQ(delegate_->last_wait_selector_timeout_ms(), 10000);

  base::Value output = ParseExecutionOutput(responses.front());
  EXPECT_EQ(output.GetDict().FindBool("found"), false);
}

TEST_F(MahoMcpSessionNetworkCaptureTest,
       HistorySearchForwardsArgsAndMarshalsEntries) {
  std::vector<MahoMcpBrowserDelegate::HistoryEntry> canned;
  canned.push_back({"https://example.com/a", "Alpha", 1716000000.0});
  canned.push_back({"https://example.com/b", "Beta", 1716000100.0});
  canned.push_back({"https://example.com/c", "Gamma", 1716000200.0});
  delegate_->set_search_history_return(std::move(canned));

  auto responses = CallToolRaw("browser_history_search",
                               R"({"query":"exam","max_results":3})", 70);

  // FIX B1: history_search is routed through the deferred sender (to avoid
  // partial-write truncation of large payloads on the synchronous path), so
  // its response arrives via deferred_responses_, not the returned vector.
  EXPECT_TRUE(responses.empty());
  ASSERT_EQ(deferred_responses_.size(), 1u);
  EXPECT_EQ(delegate_->search_history_call_count(), 1);
  EXPECT_EQ(delegate_->last_search_history_query(), "exam");
  EXPECT_EQ(delegate_->last_search_history_max_results(), 3u);

  base::Value output = ParseExecutionOutput(deferred_responses_.back());
  const auto* entries = output.GetDict().FindList("entries");
  ASSERT_TRUE(entries);
  ASSERT_EQ(entries->size(), 3u);

  const auto &e0 = (*entries)[0].GetDict();
  EXPECT_EQ(*e0.FindString("url"), "https://example.com/a");
  EXPECT_EQ(*e0.FindString("title"), "Alpha");
  EXPECT_EQ(e0.FindDouble("visited_at").value(), 1716000000.0);

  const auto &e1 = (*entries)[1].GetDict();
  EXPECT_EQ(*e1.FindString("url"), "https://example.com/b");
  EXPECT_EQ(*e1.FindString("title"), "Beta");

  const auto &e2 = (*entries)[2].GetDict();
  EXPECT_EQ(*e2.FindString("url"), "https://example.com/c");
  EXPECT_EQ(*e2.FindString("title"), "Gamma");
}

TEST_F(MahoMcpSessionNetworkCaptureTest,
       HistorySearchReturnsEmptyWhenDelegateHasNoMatch) {
  // Delegate returns an empty vector -> handler must marshal an empty list.
  // This proves the handler reflects real delegate output, not a hardcoded
  // non-empty response.
  delegate_->set_search_history_return({});

  auto responses =
      CallToolRaw("browser_history_search", R"({"query":"no-such-term"})", 71);

  // FIX B1: history_search response is delivered via the deferred sender.
  EXPECT_TRUE(responses.empty());
  ASSERT_EQ(deferred_responses_.size(), 1u);
  EXPECT_EQ(delegate_->search_history_call_count(), 1);
  EXPECT_EQ(delegate_->last_search_history_query(), "no-such-term");
  EXPECT_EQ(delegate_->last_search_history_max_results(), 20u);

  base::Value output = ParseExecutionOutput(deferred_responses_.back());
  const auto* entries = output.GetDict().FindList("entries");
  ASSERT_TRUE(entries);
  EXPECT_TRUE(entries->empty());
}

// ---------------------------------------------------------------------------
// R-8: non-revealing, generation-bound target resolution (task-8-mcp-session).
// The ResolvingFakeDelegate and the resolution infra used by the tests below
// live on MahoMcpSessionTest (see top of file); each test opts in with
// InitResolvingSession().
// ---------------------------------------------------------------------------

TEST_F(MahoMcpSessionTest, ToolSetsAreExactAndEvaluateJsStaysDenied) {
  delegate_ = std::make_unique<ResolvingFakeDelegate>();
  MahoMcpSession::SetBrowserDelegate(delegate_.get());
  session_->ProcessData(R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
                        R"("params":{"protocolVersion":"2025-03-26",)"
                        R"("clientInfo":{"name":"t","version":"0.1.0"}}})"
                        "\n");
  ASSERT_EQ(session_->state(), MahoMcpSession::State::kActive);

  auto list_responses =
      session_->ProcessData(R"({"jsonrpc":"2.0","method":"tools/list","id":2})"
                            "\n");
  ASSERT_EQ(list_responses.size(), 1u);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(list_responses[0], base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());
  const auto *result = parsed->GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto *diagnostics = result->FindDict("catalogDiagnostics");
  ASSERT_TRUE(diagnostics);
  EXPECT_EQ(diagnostics->FindInt("catalogVersion"), 1);
  EXPECT_EQ(diagnostics->FindInt("schemaVersion"), 1);
  EXPECT_EQ(diagnostics->FindInt("resultVersion"), 1);
  EXPECT_EQ(diagnostics->FindInt("canonicalCount"), 80);
  const auto *surfaces = diagnostics->FindDict("surfaces");
  ASSERT_TRUE(surfaces);
  const auto *public_mcp = surfaces->FindDict("publicMcp");
  ASSERT_TRUE(public_mcp);
  EXPECT_EQ(public_mcp->FindInt("count"), 50);
  EXPECT_EQ(public_mcp->FindList("ids")->size(), 50u);

  const auto *tools = result->FindList("tools");
  ASSERT_TRUE(tools);

  std::set<std::string> advertised;
  for (const auto &tool : *tools) {
    const std::string *name = tool.GetDict().FindString("name");
    ASSERT_TRUE(name);
    advertised.insert(*name);
  }
  EXPECT_EQ(advertised.size(), tools->size());

  const std::set<std::string> expected = {
      "browser_tab_list",
      "browser_tab_get",
      "browser_navigate",
      "browser_tab_close",
      "browser_tab_new",
      "browser_history_search",
      "browser_bookmarks_search",
      "browser_bookmark_create",
      "browser_page_content",
      "browser_page_text",
      "browser_search_in_page",
      "browser_page_context",
      "page_query_selector",
      "page_get_text",
      "page_get_attribute",
      "page_wait_for_selector",
      "browser_same_origin_fetch",
      "browser_set_blocked_domains",
      "browser_get_blocked_domains",
      "browser_routines_list",
      "browser_routines_run",
      "mail_list_accounts",
      "mail_list_folders",
      "mail_list_emails",
      "mail_get_email",
      "mail_search_emails",
      "mail_list_thread",
      "mail_extract_otp",
      "mail_send",
      "mail_save_draft",
      "mail_update_draft",
      "mail_queue_email",
      "mail_flag",
      "browser_screenshot_full",
      "browser_screenshot_element",
      "browser_network_start_capture",
      "browser_network_stop_capture",
      "browser_network_get_har",
      "browser_accessibility_snapshot",
      "browser_click",
      "browser_type",
      "browser_file_upload_select",
      "browser_select",
      "browser_console_messages",
      "browser_wait_for_navigation",
      "browser_scroll",
      "browser_hover",
      "browser_key_press",
      "browser_tab_switch",
      "browser_set_viewport_size",
  };
  EXPECT_EQ(advertised, expected);

  // evaluate_js is never advertised and is rejected on direct call.
  EXPECT_EQ(advertised.count("evaluate_js"), 0u);
  auto eval_responses =
      session_->ProcessData(R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
                            R"("params":{"name":"evaluate_js","arguments":{}}})"
                            "\n");
  ASSERT_EQ(eval_responses.size(), 1u);
  std::optional<base::Value> eval =
      base::JSONReader::Read(eval_responses[0], base::JSON_PARSE_RFC);
  ASSERT_TRUE(eval.has_value());
  const auto *eval_error = eval->GetDict().FindDict("error");
  ASSERT_TRUE(eval_error);
  EXPECT_EQ(eval_error->FindInt("code").value(), -32601);

  // ping is hidden (not advertised) but functional.
  EXPECT_EQ(advertised.count("ping"), 0u);
  auto ping_responses =
      session_->ProcessData(R"({"jsonrpc":"2.0","method":"tools/call","id":4,)"
                            R"("params":{"name":"ping","arguments":{}}})"
                            "\n");
  ASSERT_EQ(ping_responses.size(), 1u);
  std::optional<base::Value> ping =
      base::JSONReader::Read(ping_responses[0], base::JSON_PARSE_RFC);
  ASSERT_TRUE(ping.has_value());
  const auto *ping_result = ping->GetDict().FindDict("result");
  ASSERT_TRUE(ping_result) << ping_responses[0];
  EXPECT_TRUE(ping_result->FindBool("pong").value());
}

TEST_F(MahoMcpSessionTest, EveryTargetedToolUsesStableResolution) {
  InitResolvingSession();

  const std::vector<std::string> tab_target_tools = {
      "browser_tab_get",
      "browser_navigate",
      "browser_tab_close",
      "browser_tab_switch",
      "browser_page_content",
      "browser_page_text",
      "browser_page_context",
      "browser_search_in_page",
      "page_query_selector",
      "page_get_text",
      "page_get_attribute",
      "page_wait_for_selector",
      "browser_same_origin_fetch",
      "browser_accessibility_snapshot",
      "browser_click",
      "browser_type",
      "browser_file_upload_select",
      "browser_select",
      "browser_console_messages",
      "browser_wait_for_navigation",
      "browser_scroll",
      "browser_hover",
      "browser_key_press",
      "browser_set_viewport_size",
      "browser_screenshot_full",
      "browser_screenshot_element",
      "browser_network_start_capture",
      "browser_network_stop_capture",
      "browser_network_get_har",
  };
  const std::vector<std::string> control_tab_target_tools = {
      "browser_acquire_lease",
      "browser_heartbeat_lease",
      "browser_release_lease",
      "vault_list_credentials_for_active_page",
      "vault_request_credential_use",
      "vault_fill_credential",
      "vault_fill_totp",
  };

  int id = 100;
  for (const std::string &tool : tab_target_tools) {
    const int before = delegate_->resolve_tab_calls;
    CallToolRaw(tool, "{}", id++);
    EXPECT_EQ(delegate_->resolve_tab_calls, before + 1)
        << "tool did not resolve its target exactly once: " << tool;
  }
  for (const std::string& tool : control_tab_target_tools) {
    const int before = delegate_->resolve_tab_calls;
    CallControlRaw(tool, "{}", id++);
    EXPECT_EQ(delegate_->resolve_tab_calls, before + 1)
        << "control capability did not resolve its target exactly once: "
        << tool;
  }

  // Profile-target tool resolves through the browser entry point.
  const int profile_before = delegate_->resolve_profile_calls;
  CallToolRaw("browser_tab_new", "{}", id++);
  EXPECT_EQ(delegate_->resolve_profile_calls, profile_before + 1);

  // Session/core tools never resolve a tab or browser target.
  const int tab_before = delegate_->resolve_tab_calls;
  const int prof_before = delegate_->resolve_profile_calls;
  CallToolRaw("ping", "{}", id++);
  CallToolRaw("browser_tab_list", "{}", id++);
  CallToolRaw("browser_get_blocked_domains", "{}", id++);
  EXPECT_EQ(delegate_->resolve_tab_calls, tab_before);
  EXPECT_EQ(delegate_->resolve_profile_calls, prof_before);
}

// Todo 6: input/navigation mutators require an active tab lease held by this
// session. Without a lease the mutation is denied (-32007) before any delegate
// dispatch; after acquiring the lease the gate no longer blocks it.
TEST_F(MahoMcpSessionTest, MutationRequiresActiveLease) {
  InitResolvingSession(true);

  {
    auto responses =
        CallToolRaw("browser_scroll",
                    R"({"direction":"down","pixels":100,"tab_id":7})", 300);
    ASSERT_EQ(responses.size(), 1u);
    base::Value r = ParseLine(responses[0]);
    const auto *error = r.GetDict().FindDict("error");
    ASSERT_TRUE(error);
    EXPECT_EQ(error->FindInt("code").value(), kMahoMcpErrorLeaseRequired);
  }

  {
    auto responses =
        CallControlRaw("browser_acquire_lease", R"({"tab_id":7})", 301);
    ASSERT_EQ(responses.size(), 1u);
    base::Value r = ParseLine(responses[0]);
    ASSERT_FALSE(r.GetDict().FindDict("error"))
        << "lease acquisition unexpectedly failed";
  }

  {
    auto responses =
        CallToolRaw("browser_scroll",
                    R"({"direction":"down","pixels":100,"tab_id":7})", 302);
    ASSERT_EQ(responses.size(), 1u);
    base::Value r = ParseLine(responses[0]);
    const auto *error = r.GetDict().FindDict("error");
    if (error) {
      EXPECT_NE(error->FindInt("code").value(), kMahoMcpErrorLeaseRequired)
          << "held lease must let the mutation past the gate";
    }
  }
}

TEST_F(MahoMcpSessionTest, LeaseControlsRejectIneligibleTabsBeforeRegistry) {
  InitResolvingSession(true);
  delegate_->eligible_tabs.erase(11);

  const auto rejected =
      CallControlRaw("browser_acquire_lease", R"({"tab_id":11})", 303);
  ASSERT_EQ(rejected.size(), 1u);
  const base::Value response = ParseLine(rejected[0]);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code"), kMahoMcpErrorTabNotFound);
  EXPECT_FALSE(lease_registry_->IsHeldBy(11, session_->session_id()));
}

TEST_F(MahoMcpSessionTest,
       LeaseHeartbeatRejectsIneligibleTabWithoutExtendingLease) {
  InitResolvingSession(true);
  ASSERT_EQ(CallControlRaw("browser_acquire_lease",
                           R"({"tab_id":7,"ttl_seconds":10})", 304)
                .size(),
            1u);
  ASSERT_TRUE(lease_registry_->IsHeldBy(7, session_->session_id()));
  delegate_->eligible_tabs.erase(7);

  const auto rejected = CallControlRaw(
      "browser_heartbeat_lease", R"({"tab_id":7,"ttl_seconds":300})", 305);
  ASSERT_EQ(rejected.size(), 1u);
  const base::Value response = ParseLine(rejected[0]);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code"), kMahoMcpErrorTabNotFound);

  task_environment_.FastForwardBy(base::Seconds(11));
  EXPECT_FALSE(lease_registry_->IsHeldBy(7, session_->session_id()));
}

TEST_F(MahoMcpSessionTest, LeaseReleaseRejectsIneligibleTabWithoutReleasing) {
  InitResolvingSession(true);
  ASSERT_EQ(CallControlRaw("browser_acquire_lease", R"({"tab_id":7})", 306)
                .size(),
            1u);
  ASSERT_TRUE(lease_registry_->IsHeldBy(7, session_->session_id()));
  delegate_->eligible_tabs.erase(7);

  const auto rejected =
      CallControlRaw("browser_release_lease", R"({"tab_id":7})", 307);
  ASSERT_EQ(rejected.size(), 1u);
  const base::Value response = ParseLine(rejected[0]);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code"), kMahoMcpErrorTabNotFound);
  EXPECT_TRUE(lease_registry_->IsHeldBy(7, session_->session_id()));
}

TEST_F(MahoMcpSessionTest, FileUploadRequiresPathAndUsesResolvedTab) {
  InitResolvingSession();

  auto missing_path =
      CallToolRaw("browser_file_upload_select", R"({"tab_id":7})", 303);
  ASSERT_EQ(missing_path.size(), 1u);
  const base::Value missing_path_response = ParseLine(missing_path[0]);
  const base::DictValue* missing_path_error =
      missing_path_response.GetDict().FindDict("error");
  ASSERT_TRUE(missing_path_error);
  EXPECT_EQ(missing_path_error->FindInt("code"), -32602);
  EXPECT_EQ(delegate_->file_upload_call_count, 0);

  auto uploaded = CallToolRaw("browser_file_upload_select",
                              R"({"path":"/tmp/build.aab"})", 304);
  ASSERT_EQ(uploaded.size(), 1u);
  const base::Value uploaded_response = ParseLine(uploaded[0]);
  ASSERT_FALSE(uploaded_response.GetDict().FindDict("error"))
      << uploaded[0];
  EXPECT_EQ(delegate_->file_upload_call_count, 1);
  EXPECT_EQ(delegate_->file_upload_tab_id, 7);
  EXPECT_EQ(delegate_->file_upload_path, "/tmp/build.aab");
  base::Value uploaded_output = ParseExecutionOutput(uploaded[0]);
  const base::DictValue* result = uploaded_output.GetIfDict();
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->FindBool("file_selected").value_or(false));

  delegate_->file_upload_succeeds = false;
  auto failed = CallToolRaw("browser_file_upload_select",
                            R"({"tab_id":7,"path":"/tmp/build.aab"})",
                            305);
  ASSERT_EQ(failed.size(), 1u);
  const base::Value failed_response = ParseLine(failed[0]);
  const base::DictValue* failed_error =
      failed_response.GetDict().FindDict("error");
  ASSERT_TRUE(failed_error);
  EXPECT_EQ(failed_error->FindInt("code"), -32000);
  EXPECT_EQ(delegate_->file_upload_call_count, 2);
}

// Resolving a target issues a fresh capability token for every operation in
// production. A ref is bound to its underlying tab and browser, not to the
// short-lived token minted for the following click/type operation.
TEST_F(MahoMcpSessionTest,
       SnapshotRefSurvivesFreshResolutionForSameTabAndBrowser) {
  InitResolvingSession();
  delegate_->issue_unique_generations = true;
  ASSERT_EQ(CallControlRaw("browser_acquire_lease", R"({"tab_id":7})", 310)
                .size(),
            1u);
  ASSERT_EQ(CallToolRaw("browser_accessibility_snapshot", R"({"tab_id":7})",
                        311)
                .size(),
            1u);
  session_->ref_table()[1] = 101;

  auto responses = CallToolRaw(
      "browser_type", R"({"tab_id":7,"ref":1,"text":"Maho Browser"})",
      312);
  ASSERT_EQ(responses.size(), 1u);
  const base::Value response = ParseLine(responses[0]);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_FALSE(error) << responses[0];
  EXPECT_EQ(delegate_->browser_type_call_count, 1);
  EXPECT_EQ(delegate_->browser_type_tab_id, 7);
  EXPECT_EQ(delegate_->browser_type_ax_id, 101);
}

TEST_F(MahoMcpSessionTest, SnapshotRefRejectsDocumentChange) {
  InitResolvingSession();
  delegate_->issue_unique_generations = true;
  ASSERT_EQ(CallControlRaw("browser_acquire_lease", R"({"tab_id":7})", 313)
                .size(),
            1u);
  ASSERT_EQ(CallToolRaw("browser_accessibility_snapshot", R"({"tab_id":7})",
                        314)
                .size(),
            1u);
  session_->ref_table()[1] = 101;
  ++delegate_->navigation_epoch;

  auto responses = CallToolRaw(
      "browser_type", R"({"tab_id":7,"ref":1,"text":"Maho Browser"})",
      315);
  ASSERT_EQ(responses.size(), 1u);
  const base::Value response = ParseLine(responses[0]);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error) << responses[0];
  EXPECT_EQ(error->FindInt("code").value(), -32009);
}

TEST_F(MahoMcpSessionTest,
       SnapshotRefRejectsDocumentChangeDuringApproval) {
  InitResolvingSession(true);
  ASSERT_EQ(CallControlRaw("browser_acquire_lease", R"({"tab_id":7})", 316)
                .size(),
            1u);
  ASSERT_EQ(CallToolRaw("browser_accessibility_snapshot", R"({"tab_id":7})",
                        317)
                .size(),
            1u);
  session_->ref_table()[1] = 101;
  delegate_->advance_navigation_epoch_on_approval = true;

  auto responses = CallToolRaw(
      "browser_type", R"({"tab_id":7,"ref":1,"text":"Maho Browser"})",
      318);
  ASSERT_EQ(responses.size(), 1u);
  const base::Value response = ParseLine(responses[0]);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error) << responses[0];
  EXPECT_EQ(error->FindInt("code"), kMahoMcpErrorStaleReference);
  EXPECT_EQ(delegate_->browser_type_call_count, 0);
}

TEST_F(MahoMcpSessionTest, NavigationEpochResetsDiff) {
  InitResolvingSession();

  // Initial V2 snapshot on active tab (tab_id: 7)
  auto responses1 = CallToolRaw("page.accessibility_snapshot_v2",
                                R"({"tab_id":7,"mode":"interactive"})", 501);
  ASSERT_EQ(responses1.size(), 1u);
  base::Value output1 = ParseExecutionOutput(responses1[0]);
  const base::DictValue* result1 = output1.GetIfDict();
  ASSERT_TRUE(result1) << responses1[0];
  const std::string* token1 = result1->FindString("snapshot_token");
  ASSERT_TRUE(token1);
  EXPECT_FALSE(token1->empty());

  // Advance navigation epoch on the delegate to simulate page navigation
  ++delegate_->navigation_epoch;

  // Request next snapshot with since_snapshot_token from previous epoch
  std::string req2 = base::StringPrintf(
      R"({"tab_id":7,"mode":"interactive","since_snapshot_token":"%s"})",
      token1->c_str());
  auto responses2 = CallToolRaw("page.accessibility_snapshot_v2", req2, 502);
  ASSERT_EQ(responses2.size(), 1u);
  base::Value output2 = ParseExecutionOutput(responses2[0]);
  const base::DictValue* result2 = output2.GetIfDict();
  ASSERT_TRUE(result2) << responses2[0];

  // Navigation epoch change forces a full reset diff (all nodes added)
  const std::string* diff = result2->FindString("diff");
  ASSERT_TRUE(diff);
  EXPECT_FALSE(diff->empty());
  EXPECT_NE(diff->find("+ "), std::string::npos);
}

TEST_F(MahoMcpSessionTest, AcquireLeaseForceStealsOtherSession) {
  InitResolvingSession();
  auto other_session = MakeTrustedCliTestSession(lease_registry_.get());
  const std::string other_session_id = other_session->session_id();
  ASSERT_TRUE(lease_registry_
                  ->Acquire(7, other_session_id, base::Seconds(60), false)
                  .ok);

  auto responses =
      CallControlRaw("browser_acquire_lease", R"({"tab_id":7})", 319);
  ASSERT_EQ(responses.size(), 1u);
  base::Value response = ParseLine(responses[0]);
  ASSERT_FALSE(response.GetDict().FindDict("error")) << responses[0];
  const base::DictValue* result = response.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  base::Value payload;
  if (const std::string* output_json = result->FindString("outputJson")) {
    auto parsed_output =
        base::JSONReader::Read(*output_json, base::JSON_PARSE_RFC);
    ASSERT_TRUE(parsed_output.has_value());
    payload = std::move(*parsed_output);
  } else {
    payload = base::Value(result->Clone());
  }
  const base::DictValue* payload_dict = payload.GetIfDict();
  ASSERT_TRUE(payload_dict);
  EXPECT_TRUE(payload_dict->FindBool("acquired").value_or(false));
  const std::string* previous_holder =
      payload_dict->FindString("previous_holder");
  ASSERT_TRUE(previous_holder);
  EXPECT_EQ(*previous_holder, other_session_id);
  EXPECT_TRUE(lease_registry_->IsHeldBy(7, session_->session_id()));
  EXPECT_FALSE(lease_registry_->IsHeldBy(7, other_session_id));
}

TEST_F(MahoMcpSessionTest,
       AutonomousAcquireLeaseRejectsContestedTabWithHolderData) {
  InitResolvingSession(false, true);
  ASSERT_TRUE(session_->autonomous());
  ASSERT_EQ(CallControlRaw("browser_acquire_lease", R"({"tab_id":7})", 910)
                .size(),
            1u);
  ASSERT_TRUE(lease_registry_->IsHeldBy(7, session_->session_id()));

  auto contender = MakeTrustedCliTestSession(lease_registry_.get());
  auto init = InitializeCliRaw(contender.get(), true, 911);
  ASSERT_EQ(init.size(), 1u);
  ASSERT_TRUE(contender->autonomous());

  auto responses = CallControlRawOn(
      contender.get(), "browser_acquire_lease", R"({"tab_id":7})", 912);
  ASSERT_EQ(responses.size(), 1u);
  base::Value response = ParseLine(responses[0]);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error) << responses[0];
  EXPECT_EQ(error->FindInt("code"), -32000);
  ASSERT_TRUE(error->FindString("message"));
  EXPECT_EQ(*error->FindString("message"), "tab already leased");
  const base::DictValue* data = error->FindDict("data");
  ASSERT_TRUE(data);
  EXPECT_EQ(data->size(), 2u);
  ASSERT_TRUE(data->FindString("previous_holder"));
  EXPECT_EQ(*data->FindString("previous_holder"), session_->session_id());
  EXPECT_EQ(data->FindInt("tab_id"), 7);
  EXPECT_TRUE(lease_registry_->IsHeldBy(7, session_->session_id()));
  EXPECT_FALSE(lease_registry_->IsHeldBy(7, contender->session_id()));

  for (const auto& entry : lease_registry_->GetAuditLog()) {
    EXPECT_FALSE(entry.action == "steal" &&
                 entry.session_id == contender->session_id());
  }
}

TEST_F(MahoMcpSessionTest, HumanCliAcquireLeaseForceStealsAutonomousHolder) {
  InitResolvingSession(false, true);
  ASSERT_EQ(CallControlRaw("browser_acquire_lease", R"({"tab_id":7})", 920)
                .size(),
            1u);
  const std::string agent_id = session_->session_id();
  ASSERT_TRUE(lease_registry_->IsHeldBy(7, agent_id));

  auto human = MakeTrustedCliTestSession(lease_registry_.get());
  auto init = InitializeCliRaw(human.get(), false, 921);
  ASSERT_EQ(init.size(), 1u);
  ASSERT_FALSE(human->autonomous());
  auto responses = CallControlRawOn(
      human.get(), "browser_acquire_lease", R"({"tab_id":7})", 922);
  ASSERT_EQ(responses.size(), 1u);
  base::Value response = ParseLine(responses[0]);
  ASSERT_FALSE(response.GetDict().FindDict("error")) << responses[0];
  const base::DictValue* result = response.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  base::Value payload(result->Clone());
  if (const std::string* output_json = result->FindString("outputJson")) {
    auto parsed = base::JSONReader::Read(*output_json, base::JSON_PARSE_RFC);
    ASSERT_TRUE(parsed.has_value());
    payload = std::move(*parsed);
  }
  const base::DictValue* payload_dict = payload.GetIfDict();
  ASSERT_TRUE(payload_dict);
  ASSERT_TRUE(payload_dict->FindString("previous_holder"));
  EXPECT_EQ(*payload_dict->FindString("previous_holder"), agent_id);
  EXPECT_TRUE(lease_registry_->IsHeldBy(7, human->session_id()));
  EXPECT_FALSE(lease_registry_->IsHeldBy(7, agent_id));

  bool saw_steal = false;
  for (const auto& entry : lease_registry_->GetAuditLog()) {
    if (entry.action == "steal" && entry.session_id == human->session_id() &&
        entry.previous == agent_id && entry.tab_id == 7) {
      saw_steal = true;
    }
  }
  EXPECT_TRUE(saw_steal);
}

TEST_F(MahoMcpSessionTest,
       AutonomousMutationWithoutLeaseDoesNotImplicitlyForceSteal) {
  InitResolvingSession(false, true);
  const std::string incumbent = "incumbent-session";
  ASSERT_TRUE(
      lease_registry_->Acquire(7, incumbent, base::Seconds(60), false).ok);

  auto responses = CallToolRaw(
      "input.locator_click",
      R"({"tab_id":7,"locator":{"css":"#action-btn"}})", 930);
  ASSERT_EQ(responses.size(), 1u);
  base::Value response = ParseLine(responses[0]);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error) << responses[0];
  EXPECT_EQ(error->FindInt("code"), kMahoMcpErrorLeaseRequired);
  EXPECT_TRUE(lease_registry_->IsHeldBy(7, incumbent));
  EXPECT_FALSE(lease_registry_->IsHeldBy(7, session_->session_id()));
  EXPECT_EQ(delegate_->browser_click_call_count, 0);
  for (const auto& entry : lease_registry_->GetAuditLog()) {
    EXPECT_FALSE(entry.action == "steal" &&
                 entry.session_id == session_->session_id());
  }
}

TEST_F(MahoMcpSessionTest,
       AutonomousScopedLeaseContentionReturnsStructuredAdmissionError) {
  InitResolvingSession(false, true);
  delegate_->mock_locator_nodes = {
      {.ax_id = 101,
       .role = "button",
       .name = "Submit",
       .css = "#action-btn"},
  };
  const std::string incumbent = "incumbent-session";
  ASSERT_TRUE(
      lease_registry_->Acquire(7, incumbent, base::Seconds(60), false).ok);

  auto responses = CallToolRaw(
      "input.locator_click",
      R"({"tab_id":7,"locator":{"css":"#action-btn"},"lease":"scoped"})",
      940);
  ASSERT_EQ(responses.size(), 1u);
  base::Value response = ParseLine(responses[0]);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error) << responses[0];
  EXPECT_EQ(error->FindInt("code"), -32000);
  ASSERT_TRUE(error->FindString("message"));
  EXPECT_EQ(*error->FindString("message"), "tab already leased");
  const base::DictValue* data = error->FindDict("data");
  ASSERT_TRUE(data);
  EXPECT_EQ(data->size(), 2u);
  ASSERT_TRUE(data->FindString("previous_holder"));
  EXPECT_EQ(*data->FindString("previous_holder"), incumbent);
  EXPECT_EQ(data->FindInt("tab_id"), 7);
  EXPECT_TRUE(lease_registry_->IsHeldBy(7, incumbent));
  EXPECT_FALSE(lease_registry_->IsHeldBy(7, session_->session_id()));
  EXPECT_EQ(delegate_->browser_click_call_count, 0);
}

TEST_F(MahoMcpSessionTest, GrantedExactOriginIsRequiredForNavigation) {
  InitResolvingSession();
  ASSERT_EQ(CallControlRaw("browser_acquire_lease", R"({"tab_id":7})", 320)
                .size(),
            1u);

  auto denied =
      CallToolRaw("browser_navigate", R"({"url":"https://example.com/x"})",
                  321);
  ASSERT_EQ(denied.size(), 1u);
  const base::Value denied_response = ParseLine(denied[0]);
  const base::DictValue* error =
      denied_response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code"), kMahoMcpErrorApprovalDenied);
  EXPECT_TRUE(delegate_->last_navigated_url.empty());

  ASSERT_EQ(CallControlRaw("browser_grant_exact_origin",
                           R"({"origin":"https://example.com"})", 322)
                .size(),
            1u);
  auto allowed =
      CallToolRaw("browser_navigate", R"({"url":"https://example.com/x"})",
                  323);
  ASSERT_EQ(allowed.size(), 1u);
  EXPECT_EQ(delegate_->last_navigated_url, "https://example.com/x");
}

TEST_F(MahoMcpSessionTest, GenerationChangeBeforeMutationFailsClosed) {
  InitResolvingSession();
  ASSERT_EQ(CallControlRaw("browser_acquire_lease", R"({"tab_id":7})", 324)
                .size(),
            1u);
  ASSERT_EQ(CallControlRaw("browser_grant_exact_origin",
                           R"({"origin":"https://example.com"})", 325)
                .size(),
            1u);
  delegate_->advance_generation_on_approval = true;
  auto responses =
      CallToolRaw("browser_navigate", R"({"url":"https://example.com/x"})",
                  326);
  ASSERT_EQ(responses.size(), 1u);
  const base::Value response = ParseLine(responses[0]);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code"), kMahoMcpErrorTabNotFound);
  EXPECT_TRUE(delegate_->last_navigated_url.empty());
}

TEST_F(MahoMcpSessionTest, NavigateResolvesRelativeUrlAgainstActiveTab) {
  InitResolvingSession();

  {
    auto responses =
        CallControlRaw("browser_acquire_lease", R"({"tab_id":7})", 301);
    ASSERT_EQ(responses.size(), 1u);
    base::Value r = ParseLine(responses[0]);
    ASSERT_FALSE(r.GetDict().FindDict("error"))
        << "lease acquisition unexpectedly failed";
  }

  ASSERT_EQ(CallControlRaw("browser_grant_exact_origin",
                           R"({"origin":"https://example.com"})", 3011)
                .size(),
            1u);
  auto responses = CallToolRaw("browser_navigate", R"({"url":"/sites"})", 302);
  ASSERT_EQ(responses.size(), 1u);
  base::Value output = ParseExecutionOutput(responses[0]);
  EXPECT_EQ(output.GetDict().FindBool("navigated"), true);
  EXPECT_EQ(delegate_->last_navigated_url, "https://example.com/sites");
}

TEST_F(MahoMcpSessionTest, SameOriginFetchRequiresLeaseAndRejectsCrossOrigin) {
  InitResolvingSession();

  auto no_lease = CallToolRaw(
      "browser_same_origin_fetch",
      R"({"tab_id":7,"url":"https://example.com/graphql","method":"POST","body":"{}"})",
      330);
  ASSERT_EQ(no_lease.size(), 1u);
  base::Value no_lease_value = ParseLine(no_lease[0]);
  const auto *lease_error = no_lease_value.GetDict().FindDict("error");
  ASSERT_TRUE(lease_error);
  EXPECT_EQ(lease_error->FindInt("code").value(), kMahoMcpErrorLeaseRequired);

  auto acquired =
      CallControlRaw("browser_acquire_lease", R"({"tab_id":7})", 331);
  ASSERT_EQ(acquired.size(), 1u);
  ASSERT_FALSE(ParseLine(acquired[0]).GetDict().FindDict("error"));
  ASSERT_EQ(CallControlRaw("browser_grant_exact_origin",
                           R"({"origin":"https://example.com"})", 3311)
                .size(),
            1u);

  auto cross_origin = CallToolRaw(
      "browser_same_origin_fetch",
      R"({"tab_id":7,"url":"https://other.example/graphql","method":"POST","body":"{}"})",
      332);
  ASSERT_EQ(cross_origin.size(), 1u);
  base::Value cross_origin_value = ParseLine(cross_origin[0]);
  const auto *origin_error = cross_origin_value.GetDict().FindDict("error");
  ASSERT_TRUE(origin_error);
  EXPECT_NE(origin_error->FindString("message")->find("origin"),
            std::string::npos);
  EXPECT_EQ(delegate_->same_origin_fetch_calls, 0);
}

TEST_F(MahoMcpSessionTest, SameOriginFetchReturnsBoundedRendererPayload) {
  InitResolvingSession();
  auto acquired =
      CallControlRaw("browser_acquire_lease", R"({"tab_id":7})", 340);
  ASSERT_EQ(acquired.size(), 1u);
  ASSERT_EQ(CallControlRaw("browser_grant_exact_origin",
                           R"({"origin":"https://example.com"})", 3401)
                .size(),
            1u);

  auto immediate = CallToolRaw(
      "browser_same_origin_fetch",
      R"({"tab_id":7,"url":"https://example.com/graphql","method":"POST","body":"{\"query\":\"query Dogs { animals }\"}","headers":{"x-apollo-operation-name":"Dogs"}})",
      341);
  EXPECT_TRUE(immediate.empty());
  ASSERT_EQ(delegate_->same_origin_fetch_calls, 1);
  EXPECT_EQ(delegate_->last_fetch_url.spec(), "https://example.com/graphql");
  EXPECT_EQ(delegate_->last_fetch_method, "POST");
  EXPECT_EQ(delegate_->last_fetch_body,
            R"({"query":"query Dogs { animals }"})");
  std::optional<base::Value> headers = base::JSONReader::Read(
      delegate_->last_fetch_headers_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(headers && headers->is_dict());
  EXPECT_EQ(*headers->GetDict().FindString("content-type"), "application/json");
  EXPECT_EQ(*headers->GetDict().FindString("x-apollo-operation-name"), "Dogs");
  ASSERT_EQ(deferred_responses_.size(), 1u);
  base::Value output = ParseExecutionOutput(deferred_responses_[0]);
  EXPECT_EQ(output.GetDict().FindInt("status"), 200);
  EXPECT_EQ(*output.GetDict().FindString("content_type"), "application/json");
  EXPECT_EQ(*output.GetDict().FindString("text"), R"({"ok":true})");
  EXPECT_EQ(output.GetDict().FindBool("truncated"), false);
}

// Todo 6: read-only tools remain available with no lease held.
TEST_F(MahoMcpSessionTest, ReadOnlyToolAllowedWithoutLease) {
  InitResolvingSession();

  auto responses = CallToolRaw("browser_page_text", R"({"tab_id":7})", 310);
  ASSERT_EQ(responses.size(), 1u);
  base::Value r = ParseLine(responses[0]);
  const auto *error = r.GetDict().FindDict("error");
  if (error) {
    EXPECT_NE(error->FindInt("code").value(), kMahoMcpErrorLeaseRequired);
  }
}

TEST_F(MahoMcpSessionTest, NoCachedOrZeroActiveFallback) {
  InitResolvingSession();

  // Explicit unknown/OTR id is indistinguishable from unknown: -32004.
  {
    auto responses =
        CallToolRaw("browser_page_text", R"({"tab_id":4242})", 200);
    ASSERT_EQ(responses.size(), 1u);
    base::Value r = ParseLine(responses[0]);
    const auto *error = r.GetDict().FindDict("error");
    ASSERT_TRUE(error);
    EXPECT_EQ(error->FindInt("code").value(), -32004);
    EXPECT_EQ(*error->FindString("message"), "tab not found");
  }

  // browser_tab_get with an unknown id must NOT fall back to the in-memory
  // tabs_ list when a delegate is present.
  {
    auto responses = CallToolRaw("browser_tab_get", R"({"tab_id":4242})", 201);
    ASSERT_EQ(responses.size(), 1u);
    base::Value r = ParseLine(responses[0]);
    const auto *error = r.GetDict().FindDict("error");
    ASSERT_TRUE(error);
    EXPECT_EQ(error->FindInt("code").value(), -32004);
  }

  // Omitted/zero request while there is no eligible regular active tab: -32005,
  // never a silent fallback to the local default tab (id 0).
  delegate_->has_eligible_active = false;
  {
    auto responses = CallToolRaw("browser_page_text", R"({})", 202);
    ASSERT_EQ(responses.size(), 1u);
    base::Value r = ParseLine(responses[0]);
    const auto *error = r.GetDict().FindDict("error");
    ASSERT_TRUE(error);
    EXPECT_EQ(error->FindInt("code").value(), -32005);
    EXPECT_EQ(*error->FindString("message"), "no eligible active tab");
  }

  // Profile/new-tab tool with no eligible regular browser: -32006.
  delegate_->has_eligible_browser = false;
  {
    auto responses = CallToolRaw("browser_tab_new", R"({})", 203);
    ASSERT_EQ(responses.size(), 1u);
    base::Value r = ParseLine(responses[0]);
    const auto *error = r.GetDict().FindDict("error");
    ASSERT_TRUE(error);
    EXPECT_EQ(error->FindInt("code").value(), -32006);
    EXPECT_EQ(*error->FindString("message"), "no eligible active browser");
  }
}

TEST_F(MahoMcpSessionTest, AsyncDenialCompletesExactlyOnce) {
  InitResolvingSession();

  ASSERT_TRUE(
      CallToolRaw("browser_network_start_capture", R"({"tab_id":7})", 300)
          .empty());
  ASSERT_EQ(delegate_->pending_starts.size(), 1u);
  auto start = std::move(delegate_->pending_starts.front());
  delegate_->pending_starts.pop_front();

  // The bound tab is closed/reused before the async start completes.
  delegate_->generation = 2;

  MahoMcpBrowserDelegate::StartNetworkCaptureResult ok;
  ok.success = true;
  ok.tab_id = 7;
  std::move(start.callback).Run(ok);

  // Exactly one deferred response, and it is a denial — success never fires.
  ASSERT_EQ(deferred_responses_.size(), 1u);
  base::Value response = ParseLine(deferred_responses_.back());
  EXPECT_FALSE(response.GetDict().FindDict("result"));
  const auto *error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32004);
}

TEST_F(MahoMcpSessionTest,
       DeferredActivityPublishesOneTerminalWithCanonicalMetadataAndTarget) {
  InitResolvingSession();

  ASSERT_TRUE(
      CallToolRaw("browser_network_start_capture", R"({"tab_id":7})", 301)
          .empty());
  ASSERT_EQ(delegate_->published_activities.size(), 1u);
  const auto started = delegate_->published_activities.front();
  EXPECT_EQ(started.category, MahoBrowserToolRegistry::Category::kCapture);
  EXPECT_EQ(started.sensitivity,
            MahoBrowserToolRegistry::Sensitivity::kSensitive);
  EXPECT_EQ(started.phase, MahoMcpActivityPhase::kActing);
  EXPECT_EQ(started.target.tab_id, 7);

  auto start = std::move(delegate_->pending_starts.front());
  delegate_->pending_starts.pop_front();
  MahoMcpBrowserDelegate::StartNetworkCaptureResult ok;
  ok.success = true;
  ok.tab_id = 7;
  std::move(start.callback).Run(ok);

  ASSERT_EQ(delegate_->published_activities.size(), 2u);
  const auto& completed = delegate_->published_activities.back();
  EXPECT_EQ(completed.activity_id, started.activity_id);
  EXPECT_EQ(completed.category, started.category);
  EXPECT_EQ(completed.sensitivity, started.sensitivity);
  EXPECT_EQ(completed.phase, MahoMcpActivityPhase::kCompleted);
  EXPECT_EQ(completed.target.tab_id, started.target.tab_id);
  EXPECT_EQ(completed.target.browser_id, started.target.browser_id);
  EXPECT_EQ(completed.target.generation, started.target.generation);
  EXPECT_GT(completed.revision, started.revision);
}

TEST_F(MahoMcpSessionTest, DeferredFailurePublishesOneFailedTerminal) {
  InitResolvingSession();

  ASSERT_TRUE(
      CallToolRaw("browser_network_start_capture", R"({"tab_id":7})", 302)
          .empty());
  ASSERT_EQ(delegate_->published_activities.size(), 1u);
  auto start = std::move(delegate_->pending_starts.front());
  delegate_->pending_starts.pop_front();
  MahoMcpBrowserDelegate::StartNetworkCaptureResult failed;
  failed.success = false;
  std::move(start.callback).Run(failed);

  ASSERT_EQ(delegate_->published_activities.size(), 2u);
  EXPECT_EQ(delegate_->published_activities.back().phase,
            MahoMcpActivityPhase::kFailed);
}

TEST_F(MahoMcpSessionTest, OmittedTargetForwardsResolvedConcreteId) {
  InitResolvingSession();

  // The frontmost active tab resolves to id 7. An omitted-target tool must
  // forward that resolved concrete id to the delegate, never 0 (which would
  // let the delegate re-resolve "active" at a later, possibly-OTR moment).
  delegate_->active_tab_id = 7;
  auto responses = CallToolRaw("browser_network_start_capture", R"({})", 500);
  EXPECT_TRUE(responses.empty());
  ASSERT_EQ(delegate_->pending_starts.size(), 1u);
  EXPECT_EQ(delegate_->pending_starts.front().requested_tab_id, 7);
}

TEST_F(MahoMcpSessionTest, CaptureAndLeaseRemainBoundToGeneration) {
  InitResolvingSession();

  // Capture succeeds while the bound generation is still valid.
  ASSERT_TRUE(
      CallToolRaw("browser_network_start_capture", R"({"tab_id":7})", 400)
          .empty());
  ASSERT_EQ(delegate_->pending_starts.size(), 1u);
  auto start = std::move(delegate_->pending_starts.front());
  delegate_->pending_starts.pop_front();
  MahoMcpBrowserDelegate::StartNetworkCaptureResult ok;
  ok.success = true;
  ok.tab_id = 7;
  std::move(start.callback).Run(ok);
  ASSERT_EQ(deferred_responses_.size(), 1u);
  ASSERT_TRUE(
      ParseLine(deferred_responses_.back()).GetDict().FindDict("result"));
  deferred_responses_.clear();

  // The bound tab is closed/reused: stop must revalidate the SAME generation
  // and refuse, rather than resolving a new active target. Tab 7 remains fully
  // eligible here, so the denial proves the check is generation-bound and not
  // merely a "does some eligible tab with this id still exist" lookup.
  delegate_->generation = 5;
  ASSERT_NE(delegate_->eligible_tabs.find(7), delegate_->eligible_tabs.end());
  auto stop_responses =
      CallToolRaw("browser_network_stop_capture", R"({})", 401);
  EXPECT_TRUE(stop_responses.empty());
  ASSERT_EQ(delegate_->pending_stops.size(), 1u);
  auto stop = std::move(delegate_->pending_stops.front());
  delegate_->pending_stops.pop_front();
  MahoMcpBrowserDelegate::StopNetworkCaptureResult invalid;
  invalid.target_valid = true;
  std::move(stop).Run(std::move(invalid));
  ASSERT_EQ(deferred_responses_.size(), 1u);
  base::Value stop_response = ParseLine(deferred_responses_.back());
  const auto* stop_error = stop_response.GetDict().FindDict("error");
  ASSERT_TRUE(stop_error);
  EXPECT_EQ(stop_error->FindInt("code").value(), -32004);
  EXPECT_EQ(delegate_->pending_stops.size(), 0u);
  EXPECT_EQ(delegate_->canceled_capture_ids.size(), 0u);

  // The tab whose capture was just denied is still a live, resolvable target:
  // confirms the stop denial above came from generation mismatch, not tab
  // death.
  auto alive_responses =
      CallToolRaw("browser_tab_get", R"({"tab_id":7})", 4011);
  ASSERT_EQ(alive_responses.size(), 1u);
  EXPECT_TRUE(ParseLine(alive_responses[0]).GetDict().FindDict("result"));

  // Reset generation and acquire a lease on an eligible tab.
  delegate_->generation = 6;
  auto acquire = CallControlRaw("browser_acquire_lease",
                                R"({"tab_id":11,"ttl_seconds":60})", 402);
  ASSERT_EQ(acquire.size(), 1u);
  ASSERT_TRUE(ParseLine(acquire[0]).GetDict().FindDict("result"));

  // The leased tab is closed/reused (no longer eligible): a heartbeat resolves
  // through the boundary and is denied with the uniform non-revealing error.
  delegate_->eligible_tabs.erase(11);
  auto heartbeat = CallControlRaw("browser_heartbeat_lease",
                                  R"({"tab_id":11,"ttl_seconds":60})", 403);
  ASSERT_EQ(heartbeat.size(), 1u);
  base::Value hb = ParseLine(heartbeat[0]);
  const auto *hb_error = hb.GetDict().FindDict("error");
  ASSERT_TRUE(hb_error);
  EXPECT_EQ(hb_error->FindInt("code").value(), -32004);
}

// ---------------------------------------------------------------------------
// Todo-4: browser_type credential-field guard + no-echo response.
// Sentinel: S3NTINEL-maho-vault-9F4C
// ---------------------------------------------------------------------------

// Extends the resolving delegate to (a) record every Type() call so a test can
// prove secret text never reached the page, and (b) return trusted AX field
// metadata keyed by AXNodeID so the session's classifier decides per-field.
class TypeGuardFakeDelegate : public ResolvingFakeDelegate {
public:
  std::unordered_map<ui::AXNodeID, MahoMcpFieldMetadata> field_metadata;
  int type_call_count = 0;
  int credential_typing_approval_count = 0;
  bool credential_typing_approval_result = true;
  std::string last_typed_text;
  int vault_fill_count = 0;
  int vault_totp_fill_count = 0;
  ui::AXNodeID last_vault_ax_id = 0;
  std::string last_vault_grant_handle;
  std::vector<VaultCredentialSummary> vault_credentials;
  std::unordered_map<std::string, std::string> grant_by_handle;
  bool verified_type_result = true;
  bool verified_type_dispatched = true;
  bool requires_deferred_verified_input_responses = false;
  std::string verified_type_reason;
  base::OnceCallback<void(InputActionOutcome)> pending_verified_type;
  bool verified_click_result = true;
  bool verified_click_dispatched = true;
  std::string verified_click_reason;

  bool ConfirmCredentialTypingApproval(
      std::string_view,
      const ResolvedMahoMcpTarget&) override {
    ++credential_typing_approval_count;
    return credential_typing_approval_result;
  }

  bool Type(int tab_id, ui::AXNodeID ax_id, const std::string &text) override {
    ++type_call_count;
    last_typed_text = text;
    return true;
  }

  void TypeVerified(
      int tab_id,
      ui::AXNodeID ax_id,
      const std::string& text,
      base::OnceCallback<void(InputActionOutcome)> callback) override {
    ++type_call_count;
    last_typed_text = text;
    if (requires_deferred_verified_input_responses) {
      pending_verified_type = std::move(callback);
      return;
    }
    InputActionOutcome outcome;
    outcome.dispatched = verified_type_dispatched;
    outcome.verified = verified_type_result;
    outcome.method = "test";
    outcome.reason = verified_type_reason;
    std::move(callback).Run(std::move(outcome));
  }

  void ClickVerified(
      int tab_id,
      ui::AXNodeID ax_id,
      base::OnceCallback<void(InputActionOutcome)> callback) override {
    InputActionOutcome outcome;
    outcome.dispatched = verified_click_dispatched;
    outcome.verified = verified_click_result;
    outcome.method = "test";
    outcome.reason = verified_click_reason;
    std::move(callback).Run(std::move(outcome));
  }

  bool RequiresDeferredVerifiedInputResponses() const override {
    return requires_deferred_verified_input_responses;
  }

  void CompleteVerifiedType(InputActionOutcome outcome) {
    ASSERT_TRUE(pending_verified_type);
    std::move(pending_verified_type).Run(std::move(outcome));
  }

  MahoMcpFieldMetadata GetFieldMetadata(int tab_id,
                                        ui::AXNodeID ax_id) override {
    auto it = field_metadata.find(ax_id);
    return it == field_metadata.end() ? MahoMcpFieldMetadata() : it->second;
  }

  std::vector<VaultCredentialSummary>
  VaultListCredentialsForActivePage(int tab_id) override {
    return vault_credentials;
  }

  std::optional<std::string>
  VaultRequestCredentialUse(int tab_id, const std::string &handle,
                            const std::string &origin) override {
    for (const auto &credential : vault_credentials) {
      if (credential.handle == handle && credential.origin == origin) {
        auto it = grant_by_handle.find(handle);
        if (it == grant_by_handle.end()) {
          return std::nullopt;
        }
        return it->second;
      }
    }
    return std::nullopt;
  }

  bool VaultFillCredential(int tab_id, const std::string &grant_handle,
                           ui::AXNodeID ax_id) override {
    ++vault_fill_count;
    last_vault_grant_handle = grant_handle;
    last_vault_ax_id = ax_id;
    return true;
  }

  bool VaultFillTotp(int tab_id, const std::string &grant_handle,
                     ui::AXNodeID ax_id) override {
    ++vault_totp_fill_count;
    last_vault_grant_handle = grant_handle;
    last_vault_ax_id = ax_id;
    return true;
  }
};

class MahoMcpSessionTypeGuardTest : public testing::Test {
protected:
  static constexpr char kSentinel[] = "S3NTINEL-maho-vault-9F4C";

  void SetUp() override {
    delegate_ = std::make_unique<TypeGuardFakeDelegate>();
    MahoMcpSession::SetBrowserDelegate(delegate_.get());
    lease_registry_ = std::make_unique<MahoMcpLeaseRegistry>();
    session_ = MakeTrustedCliTestSession(lease_registry_.get());
    auto responses = session_->ProcessData(
        R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
        R"("params":{"protocolVersion":"2025-03-26",)"
        R"("controllerKind":"maho-cli",)"
        R"("clientInfo":{"name":"t","version":"0.1.0"}}})"
        "\n");
    ASSERT_EQ(responses.size(), 1u);
    ASSERT_EQ(session_->state(), MahoMcpSession::State::kActive);
    // Todo 6: browser_type is a lease-gated mutator. Hold a lease on the
    // resolving delegate's active tab (7) so these credential-guard tests reach
    // the field classifier that they actually exercise.
    auto lease = session_->ProcessData(
        R"({"jsonrpc":"2.0","method":"maho/control/call","id":2,)"
        R"("params":{"name":"browser_acquire_lease",)"
        R"("arguments":{"tab_id":7}}})"
        "\n");
    ASSERT_EQ(lease.size(), 1u);
  }

  void TearDown() override {
    MahoMcpSession::SetBrowserDelegate(nullptr);
    session_.reset();
    delegate_.reset();
  }

  base::Value CallType(int ref, const std::string &text, int id) {
    auto responses = session_->ProcessData(
        base::StringPrintf(R"({"jsonrpc":"2.0","method":"tools/call","id":%d,)"
                           R"("params":{"name":"browser_type","arguments":)"
                           R"({"ref":%d,"text":%s}}})"
                           "\n",
                           id, ref, base::GetQuotedJSONString(text).c_str()));
    EXPECT_EQ(responses.size(), 1u);
    std::optional<base::Value> parsed =
        responses.empty()
            ? std::nullopt
            : base::JSONReader::Read(responses[0], base::JSON_PARSE_RFC);
    return parsed.has_value() ? std::move(*parsed) : base::Value();
  }

  base::Value CallClick(int ref, int id) {
    auto responses = session_->ProcessData(
        base::StringPrintf(
            R"({"jsonrpc":"2.0","method":"tools/call","id":%d,)"
            R"("params":{"name":"browser_click","arguments":{"tab_id":7,"ref":%d}}})"
            "\n",
            id, ref));
    EXPECT_EQ(responses.size(), 1u);
    std::optional<base::Value> parsed =
        base::JSONReader::Read(responses.front(), base::JSON_PARSE_RFC);
    return parsed.has_value() ? std::move(*parsed) : base::Value();
  }

  base::Value CallTool(const std::string &name, const std::string &arguments,
                       int id) {
    auto responses = session_->ProcessData(
        base::StringPrintf(R"({"jsonrpc":"2.0","method":"tools/call","id":%d,)"
                           R"("params":{"name":"%s","arguments":%s}})"
                           "\n",
                           id, name.c_str(), arguments.c_str()));
    EXPECT_EQ(responses.size(), 1u);
    std::optional<base::Value> parsed =
        responses.empty()
            ? std::nullopt
            : base::JSONReader::Read(responses[0], base::JSON_PARSE_RFC);
    return parsed.has_value() ? std::move(*parsed) : base::Value();
  }

  base::Value CallControl(const std::string& name,
                          const std::string& arguments,
                          int id) {
    auto responses = session_->ProcessData(base::StringPrintf(
        R"({"jsonrpc":"2.0","method":"maho/control/call","id":%d,)"
        R"("params":{"name":"%s","arguments":%s}})"
        "\n",
        id, name.c_str(), arguments.c_str()));
    EXPECT_EQ(responses.size(), 1u);
    std::optional<base::Value> parsed =
        responses.empty()
            ? std::nullopt
            : base::JSONReader::Read(responses[0], base::JSON_PARSE_RFC);
    return parsed.has_value() ? std::move(*parsed) : base::Value();
  }

  base::Value ParseExecutionOutput(base::Value response) {
    const auto* result = response.GetDict().FindDict("result");
    EXPECT_TRUE(result) << response.DebugString();
    if (!result) {
      return base::Value(base::Value::Type::DICT);
    }
    const std::string* output_json = result->FindString("outputJson");
    EXPECT_TRUE(output_json) << response.DebugString();
    if (!output_json) {
      return base::Value(base::Value::Type::DICT);
    }
    std::optional<base::Value> output =
        base::JSONReader::Read(*output_json, base::JSON_PARSE_RFC);
    EXPECT_TRUE(output.has_value()) << *output_json;
    return output.has_value() ? std::move(*output)
                              : base::Value(base::Value::Type::DICT);
  }

  void CaptureAccessibilitySnapshot(int id) {
    base::Value snapshot =
        CallTool("browser_accessibility_snapshot", R"({"tab_id":7})", id);
    ASSERT_TRUE(snapshot.GetDict().FindDict("result"))
        << snapshot.DebugString();
  }

  std::vector<std::string> CallTypeRaw(int ref,
                                       const std::string& text,
                                       int id) {
    return session_->ProcessData(
        base::StringPrintf(R"({"jsonrpc":"2.0","method":"tools/call","id":%d,)"
                           R"("params":{"name":"browser_type","arguments":)"
                           R"({"ref":%d,"text":%s}}})"
                           "\n",
                           id, ref, base::GetQuotedJSONString(text).c_str()));
  }

  void CaptureDeferredResponse(std::string response) {
    deferred_responses_.push_back(std::move(response));
  }

  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<MahoMcpLeaseRegistry> lease_registry_;
  std::unique_ptr<TypeGuardFakeDelegate> delegate_;
  std::unique_ptr<MahoMcpSession> session_;
  std::vector<std::string> deferred_responses_;
};

TEST_F(MahoMcpSessionTypeGuardTest, OrdinaryTextFieldTypesThroughWithoutEcho) {
  MahoMcpFieldMetadata ordinary;
  ordinary.classified = true;
  ordinary.accessible_name = "search";
  delegate_->field_metadata[101] = ordinary;
  session_->ref_table()[1] = 101;
  CaptureAccessibilitySnapshot(9);
  session_->ref_table()[1] = 101;

  base::Value resp = CallType(1, "hello world", 10);

  const auto *result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_EQ(delegate_->type_call_count, 1);
  EXPECT_EQ(delegate_->last_typed_text, "hello world");
  // Response must confirm the action without echoing the typed text back.
  EXPECT_FALSE(result->FindString("text"));
  EXPECT_EQ(resp.DebugString().find("hello world"), std::string::npos);
}

TEST_F(MahoMcpSessionTypeGuardTest,
       VerifiedTypeSuccessReturnsMachineConsumedOutcomeFields) {
  MahoMcpFieldMetadata ordinary;
  ordinary.classified = true;
  delegate_->field_metadata[101] = ordinary;
  CaptureAccessibilitySnapshot(20);
  session_->ref_table()[1] = 101;

  base::Value response = CallType(1, "verified value", 21);
  base::Value output = ParseExecutionOutput(response.Clone());
  const base::DictValue& result = output.GetDict();
  EXPECT_EQ(response.DebugString().find("verified value"),
            std::string::npos);
  EXPECT_TRUE(result.FindBool("typed").value_or(false));
  EXPECT_EQ(result.FindInt("ref"), 1);
  EXPECT_TRUE(result.FindBool("verified").value_or(false));
  const std::string* method = result.FindString("method");
  ASSERT_TRUE(method);
  EXPECT_EQ(*method, "test");
  EXPECT_FALSE(result.FindString("text"));
}

TEST_F(MahoMcpSessionTypeGuardTest,
       CredentialFieldTypesThroughWhenAllowCredentialsSet) {
  MahoMcpFieldMetadata secret;
  secret.classified = true;
  secret.is_protected = true;
  secret.accessible_name = "password";
  delegate_->field_metadata[202] = secret;
  session_->ref_table()[2] = 202;
  CaptureAccessibilitySnapshot(10);
  session_->ref_table()[2] = 202;

  base::Value resp =
      CallTool("browser_type",
               R"({"ref":2,"text":"admin1234","allow_credentials":true})", 11);

  const auto *result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result) << resp.DebugString();
  EXPECT_EQ(delegate_->credential_typing_approval_count, 1);
  EXPECT_EQ(delegate_->type_call_count, 1);
  EXPECT_EQ(delegate_->last_typed_text, "admin1234");
  EXPECT_EQ(resp.DebugString().find("admin1234"), std::string::npos);
}

TEST_F(MahoMcpSessionTypeGuardTest,
       CredentialFieldRejectsWhenNativeApprovalIsDenied) {
  MahoMcpFieldMetadata secret;
  secret.classified = true;
  secret.is_protected = true;
  delegate_->field_metadata[202] = secret;
  delegate_->credential_typing_approval_result = false;
  CaptureAccessibilitySnapshot(12);
  session_->ref_table()[2] = 202;

  base::Value response = CallTool(
      "browser_type",
      R"({"ref":2,"text":"S3NTINEL-denied","allow_credentials":true})", 13);

  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error) << response.DebugString();
  EXPECT_EQ(error->FindInt("code"), -32008);
  EXPECT_EQ(*error->FindString("message"), "credential_typing_denied");
  EXPECT_EQ(delegate_->credential_typing_approval_count, 1);
  EXPECT_EQ(delegate_->type_call_count, 0);
  EXPECT_EQ(response.DebugString().find("S3NTINEL"), std::string::npos);
}

TEST_F(MahoMcpSessionTypeGuardTest,
       VaultCredentialToolsReturnHandlesAndFillWithoutEchoingSecret) {
  MahoMcpBrowserDelegate::VaultCredentialSummary credential;
  credential.handle = "vh_sentinel_handle";
  credential.username_hint = "tester@example.com";
  credential.origin = "https://example.com";
  delegate_->vault_credentials.push_back(credential);
  delegate_->grant_by_handle[credential.handle] = "vg_sentinel_grant";
  CaptureAccessibilitySnapshot(19);
  session_->ref_table()[9] = 909;

  base::Value list =
      CallControl("vault_list_credentials_for_active_page", R"({"tab_id":7})",
                  20);
  base::Value list_output = ParseExecutionOutput(list.Clone());
  const auto* items = list_output.GetDict().FindList("items");
  ASSERT_TRUE(items);
  ASSERT_EQ(items->size(), 1u);
  EXPECT_EQ(*items->front().GetDict().FindString("handle"),
            "vh_sentinel_handle");
  EXPECT_FALSE(items->front().GetDict().FindString("password"));
  EXPECT_FALSE(items->front().GetDict().FindString("secret"));
  EXPECT_FALSE(items->front().GetDict().FindString("item_id"));
  EXPECT_EQ(list.DebugString().find(kSentinel), std::string::npos);

  base::Value grant = CallControl(
      "vault_request_credential_use",
      R"({"tab_id":7,"handle":"vh_sentinel_handle","origin":"https://example.com"})",
      21);
  base::Value grant_output = ParseExecutionOutput(grant.Clone());
  const auto* grant_result = &grant_output.GetDict();
  EXPECT_EQ(*grant_result->FindString("grant_handle"), "vg_sentinel_grant");
  EXPECT_EQ(grant.DebugString().find(kSentinel), std::string::npos);
  EXPECT_FALSE(grant_result->FindString("password"));

  base::Value fill = CallControl(
      "vault_fill_credential",
      R"({"tab_id":7,"grant_handle":"vg_sentinel_grant","ref":9})", 22);
  base::Value fill_output = ParseExecutionOutput(fill.Clone());
  const auto* fill_result = &fill_output.GetDict();
  EXPECT_TRUE(fill_result->FindBool("filled").value());
  EXPECT_EQ(delegate_->vault_fill_count, 1);
  EXPECT_EQ(delegate_->type_call_count, 0);
  EXPECT_EQ(delegate_->last_vault_ax_id, 909);
  EXPECT_EQ(delegate_->last_vault_grant_handle, "vg_sentinel_grant");
  EXPECT_FALSE(fill_result->FindString("grant_handle"));
  EXPECT_FALSE(fill_result->FindString("password"));
  EXPECT_EQ(fill.DebugString().find(kSentinel), std::string::npos);
}

TEST_F(MahoMcpSessionTypeGuardTest,
       VaultFillRejectsReferenceAfterDocumentChange) {
  CaptureAccessibilitySnapshot(24);
  session_->ref_table()[9] = 909;
  ++delegate_->navigation_epoch;

  base::Value response = CallControl(
      "vault_fill_credential",
      R"({"tab_id":7,"grant_handle":"vg_sentinel_grant","ref":9})", 25);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code"), kMahoMcpErrorStaleReference);
  EXPECT_EQ(delegate_->vault_fill_count, 0);
}

TEST_F(MahoMcpSessionTypeGuardTest, PasswordFieldRejectsBeforeType) {
  MahoMcpFieldMetadata pw;
  pw.classified = true;
  pw.is_protected = true;
  delegate_->field_metadata[202] = pw;
  session_->ref_table()[2] = 202;

  base::Value resp = CallType(2, kSentinel, 11);

  EXPECT_TRUE(resp.GetDict().FindDict("error"));
  EXPECT_FALSE(resp.GetDict().FindDict("result"));
  EXPECT_EQ(delegate_->type_call_count, 0);
  EXPECT_EQ(resp.DebugString().find(kSentinel), std::string::npos);
}

TEST_F(MahoMcpSessionTypeGuardTest, TypePostconditionFailureIsNotSuccess) {
  MahoMcpFieldMetadata ordinary;
  ordinary.classified = true;
  delegate_->field_metadata[202] = ordinary;
  delegate_->verified_type_result = false;
  delegate_->verified_type_reason = "value_mismatch";
  CaptureAccessibilitySnapshot(12);
  session_->ref_table()[2] = 202;

  base::Value response = CallType(2, "expected", 12);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  ASSERT_TRUE(error->FindInt("code"));
  EXPECT_EQ(error->FindInt("code").value(),
            kMahoMcpErrorActionNotApplied);
  const base::DictValue* data = error->FindDict("data");
  ASSERT_TRUE(data);
  const std::string* reason = data->FindString("reason");
  ASSERT_TRUE(reason);
  EXPECT_EQ(*reason, "value_mismatch");
  const std::string* remedy = data->FindString("remedy");
  ASSERT_TRUE(remedy);
  EXPECT_EQ(*remedy, "capture a new accessibility snapshot and retry");
}

TEST_F(MahoMcpSessionTypeGuardTest,
       TypePostconditionErrorDataIsRedactedAtEgress) {
  MahoMcpFieldMetadata ordinary;
  ordinary.classified = true;
  delegate_->field_metadata[202] = ordinary;
  delegate_->verified_type_result = false;
  delegate_->verified_type_reason = "password=S3NTINEL-error-data-secret";
  CaptureAccessibilitySnapshot(22);
  session_->ref_table()[2] = 202;

  base::Value response = CallType(2, "expected", 23);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(response.DebugString().find("S3NTINEL-error-data-secret"),
            std::string::npos);
  const std::string* reason = error->FindStringByDottedPath("data.reason");
  ASSERT_TRUE(reason);
  EXPECT_EQ(*reason, "password=[REDACTED]");
}

TEST_F(MahoMcpSessionTypeGuardTest, TypeDispatchFailureUsesGenericError) {
  MahoMcpFieldMetadata ordinary;
  ordinary.classified = true;
  delegate_->field_metadata[202] = ordinary;
  delegate_->verified_type_dispatched = false;
  CaptureAccessibilitySnapshot(13);
  session_->ref_table()[2] = 202;

  base::Value response = CallType(2, "expected", 13);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  ASSERT_TRUE(error->FindInt("code"));
  EXPECT_EQ(error->FindInt("code").value(), -32000);
  EXPECT_FALSE(error->FindDict("data"));
}

TEST_F(MahoMcpSessionTypeGuardTest,
       DeferredInputDelegateDoesNotDispatchWithoutTransport) {
  MahoMcpFieldMetadata ordinary;
  ordinary.classified = true;
  delegate_->field_metadata[202] = ordinary;
  delegate_->requires_deferred_verified_input_responses = true;
  CaptureAccessibilitySnapshot(14);
  session_->ref_table()[2] = 202;

  base::Value response = CallType(2, "expected", 14);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  ASSERT_TRUE(error->FindInt("code"));
  EXPECT_EQ(error->FindInt("code").value(), -32000);
  EXPECT_EQ(delegate_->type_call_count, 0);
}

TEST_F(MahoMcpSessionTypeGuardTest,
       DeferredVerifiedTypePublishesOneReceiptWrappedTerminalSuccess) {
  MahoMcpFieldMetadata ordinary;
  ordinary.classified = true;
  delegate_->field_metadata[202] = ordinary;
  delegate_->requires_deferred_verified_input_responses = true;
  session_->SetDeferredResponseSender(base::BindRepeating(
      [](std::vector<std::string>* responses, std::string response) {
        responses->push_back(std::move(response));
      },
      base::Unretained(&deferred_responses_)));
  CaptureAccessibilitySnapshot(16);
  session_->ref_table()[2] = 202;

  const auto immediate = CallTypeRaw(2, "expected", 17);
  EXPECT_TRUE(immediate.empty());
  EXPECT_EQ(delegate_->type_call_count, 1);
  EXPECT_TRUE(delegate_->pending_verified_type);
  EXPECT_TRUE(deferred_responses_.empty());

  InputActionOutcome outcome;
  outcome.dispatched = true;
  outcome.verified = true;
  outcome.method = "test";
  delegate_->CompleteVerifiedType(std::move(outcome));

  ASSERT_EQ(deferred_responses_.size(), 1u);
  std::optional<base::Value> response =
      base::JSONReader::Read(deferred_responses_.front(),
                             base::JSON_PARSE_RFC);
  ASSERT_TRUE(response.has_value());
  const base::DictValue* result = response->GetDict().FindDict("result");
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->FindDict("receipt"));
  const std::string* output_json = result->FindString("outputJson");
  ASSERT_TRUE(output_json);
  std::optional<base::Value> output =
      base::JSONReader::Read(*output_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(output.has_value());
  const base::DictValue& output_dict = output->GetDict();
  EXPECT_TRUE(output_dict.FindBool("typed").value_or(false));
  EXPECT_EQ(output_dict.FindInt("ref"), 2);
  EXPECT_TRUE(output_dict.FindBool("verified").value_or(false));
}

TEST_F(MahoMcpSessionTypeGuardTest,
       DeferredVerifiedTypePublishesOneRedactedTerminalError) {
  MahoMcpFieldMetadata ordinary;
  ordinary.classified = true;
  delegate_->field_metadata[202] = ordinary;
  delegate_->requires_deferred_verified_input_responses = true;
  session_->SetDeferredResponseSender(base::BindRepeating(
      [](std::vector<std::string>* responses, std::string response) {
        responses->push_back(std::move(response));
      },
      base::Unretained(&deferred_responses_)));
  CaptureAccessibilitySnapshot(16);
  session_->ref_table()[2] = 202;

  const auto immediate = CallTypeRaw(2, "expected", 17);
  EXPECT_TRUE(immediate.empty());
  EXPECT_EQ(delegate_->type_call_count, 1);
  EXPECT_TRUE(delegate_->pending_verified_type);
  EXPECT_TRUE(deferred_responses_.empty());

  InputActionOutcome outcome;
  outcome.dispatched = true;
  outcome.verified = false;
  outcome.method = "test";
  outcome.reason = "password=S3NTINEL-deferred-error-data";
  delegate_->CompleteVerifiedType(std::move(outcome));

  ASSERT_EQ(deferred_responses_.size(), 1u);
  std::optional<base::Value> response =
      base::JSONReader::Read(deferred_responses_.front(),
                             base::JSON_PARSE_RFC);
  ASSERT_TRUE(response.has_value());
  const base::DictValue* error = response->GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code"), kMahoMcpErrorActionNotApplied);
  EXPECT_EQ(deferred_responses_.front().find("S3NTINEL-deferred-error-data"),
            std::string::npos);
  const std::string* reason = error->FindStringByDottedPath("data.reason");
  ASSERT_TRUE(reason);
  EXPECT_EQ(*reason, "password=[REDACTED]");
}

TEST_F(MahoMcpSessionTypeGuardTest, ClickPostconditionFailureIsNotSuccess) {
  delegate_->verified_click_result = false;
  delegate_->verified_click_reason = "state_not_toggled";
  CaptureAccessibilitySnapshot(15);
  session_->ref_table()[1] = 101;

  base::Value response = CallClick(1, 14);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  ASSERT_TRUE(error->FindInt("code"));
  EXPECT_EQ(error->FindInt("code").value(),
            kMahoMcpErrorActionNotApplied);
  const base::DictValue* data = error->FindDict("data");
  ASSERT_TRUE(data);
  const std::string* action = data->FindString("action");
  ASSERT_TRUE(action);
  EXPECT_EQ(*action, "click");
  const std::string* reason = data->FindString("reason");
  ASSERT_TRUE(reason);
  EXPECT_EQ(*reason, "state_not_toggled");
}

TEST_F(MahoMcpSessionTypeGuardTest, CurrentPasswordAutocompleteRejects) {
  MahoMcpFieldMetadata meta;
  meta.classified = true;
  meta.autocomplete = "current-password";
  delegate_->field_metadata[303] = meta;
  session_->ref_table()[3] = 303;

  base::Value resp = CallType(3, kSentinel, 12);

  EXPECT_TRUE(resp.GetDict().FindDict("error"));
  EXPECT_EQ(delegate_->type_call_count, 0);
  EXPECT_EQ(resp.DebugString().find(kSentinel), std::string::npos);
}

TEST_F(MahoMcpSessionTypeGuardTest, OneTimeCodeAutocompleteRejects) {
  MahoMcpFieldMetadata meta;
  meta.classified = true;
  meta.autocomplete = "one-time-code";
  delegate_->field_metadata[404] = meta;
  session_->ref_table()[4] = 404;

  base::Value resp = CallType(4, kSentinel, 13);

  EXPECT_TRUE(resp.GetDict().FindDict("error"));
  EXPECT_EQ(delegate_->type_call_count, 0);
  EXPECT_EQ(resp.DebugString().find(kSentinel), std::string::npos);
}

TEST_F(MahoMcpSessionTypeGuardTest, RecoveryCodeNameRejects) {
  MahoMcpFieldMetadata meta;
  meta.classified = true;
  meta.accessible_name = "recovery code";
  delegate_->field_metadata[505] = meta;
  session_->ref_table()[5] = 505;

  base::Value resp = CallType(5, kSentinel, 14);

  EXPECT_TRUE(resp.GetDict().FindDict("error"));
  EXPECT_EQ(delegate_->type_call_count, 0);
  EXPECT_EQ(resp.DebugString().find(kSentinel), std::string::npos);
}

// A delegate that never classified the field (default/omitted GetFieldMetadata)
// must not be able to type into it: unknown metadata fails closed.
TEST_F(MahoMcpSessionTypeGuardTest, UnclassifiedFieldFailsClosed) {
  session_->ref_table()[6] = 606; // ax_id 606 absent from field_metadata

  base::Value resp = CallType(6, kSentinel, 15);

  EXPECT_TRUE(resp.GetDict().FindDict("error"));
  EXPECT_FALSE(resp.GetDict().FindDict("result"));
  EXPECT_EQ(delegate_->type_call_count, 0);
  EXPECT_EQ(resp.DebugString().find(kSentinel), std::string::npos);
}

TEST(MahoMcpCredentialClassifierTest, ClassifiesTrustedSignals) {
  MahoMcpFieldMetadata protected_field;
  protected_field.classified = true;
  protected_field.is_protected = true;
  EXPECT_TRUE(MahoMcpSession::IsCredentialField(protected_field));

  MahoMcpFieldMetadata otp;
  otp.classified = true;
  otp.autocomplete = "one-time-code";
  EXPECT_TRUE(MahoMcpSession::IsCredentialField(otp));

  MahoMcpFieldMetadata recovery;
  recovery.classified = true;
  recovery.accessible_name = "recovery code";
  EXPECT_TRUE(MahoMcpSession::IsCredentialField(recovery));

  MahoMcpFieldMetadata ordinary;
  ordinary.classified = true;
  ordinary.autocomplete = "email";
  ordinary.accessible_name = "search the site";
  EXPECT_FALSE(MahoMcpSession::IsCredentialField(ordinary));

  // Unknown/unclassified metadata is treated as a credential (fail closed).
  MahoMcpFieldMetadata unclassified;
  EXPECT_TRUE(MahoMcpSession::IsCredentialField(unclassified));
}

// ======================================================================
// MahoMcpCapabilityRegistry unit tests (B1 exact-origin, additivity,
// validation, tab ownership, teardown)
// ======================================================================

TEST(MahoMcpCapabilityRegistryTest, GrantExactOriginAdditive) {
  MahoMcpCapabilityRegistry reg;
  const url::Origin a = url::Origin::Create(GURL("https://example.com"));
  const url::Origin b = url::Origin::Create(GURL("https://iana.org"));
  EXPECT_TRUE(reg.GrantExactOrigin(a));
  EXPECT_TRUE(reg.GrantExactOrigin(b));
  EXPECT_TRUE(reg.IsOriginGranted(a));
  EXPECT_TRUE(reg.IsOriginGranted(b));
  EXPECT_EQ(reg.ListGrantedOrigins().size(), 2u);
}

TEST(MahoMcpCapabilityRegistryTest, RevokeExactOriginRemovesOne) {
  MahoMcpCapabilityRegistry reg;
  const url::Origin a = url::Origin::Create(GURL("https://example.com"));
  const url::Origin b = url::Origin::Create(GURL("https://iana.org"));
  reg.GrantExactOrigin(a);
  reg.GrantExactOrigin(b);
  EXPECT_TRUE(reg.RevokeExactOrigin(a));
  EXPECT_FALSE(reg.IsOriginGranted(a));
  EXPECT_TRUE(reg.IsOriginGranted(b));
  EXPECT_FALSE(reg.RevokeExactOrigin(a)); // already gone
}

TEST(MahoMcpCapabilityRegistryTest, SchemeAndPortDistinction) {
  MahoMcpCapabilityRegistry reg;
  const url::Origin https_443 =
      url::Origin::Create(GURL("https://example.com"));
  const url::Origin http_80 = url::Origin::Create(GURL("http://example.com"));
  const url::Origin https_8443 =
      url::Origin::Create(GURL("https://example.com:8443"));
  reg.GrantExactOrigin(https_443);
  EXPECT_FALSE(reg.IsOriginGranted(http_80));
  EXPECT_FALSE(reg.IsOriginGranted(https_8443));
  reg.GrantExactOrigin(https_8443);
  EXPECT_TRUE(reg.IsOriginGranted(https_8443));
  EXPECT_EQ(reg.ListGrantedOrigins().size(), 2u);
}

TEST(MahoMcpCapabilityRegistryTest, WwwAndApexAreDistinct) {
  MahoMcpCapabilityRegistry reg;
  const url::Origin apex = url::Origin::Create(GURL("https://example.com"));
  const url::Origin www = url::Origin::Create(GURL("https://www.example.com"));
  reg.GrantExactOrigin(apex);
  EXPECT_FALSE(reg.IsOriginGranted(www));
}

TEST(MahoMcpCapabilityRegistryTest, RejectsOpaqueOrigin) {
  MahoMcpCapabilityRegistry reg;
  const url::Origin opaque;
  EXPECT_FALSE(reg.IsValidGrantOrigin(opaque));
  EXPECT_FALSE(reg.GrantExactOrigin(opaque));
}

TEST(MahoMcpCapabilityRegistryTest, RejectsDangerousSchemes) {
  MahoMcpCapabilityRegistry reg;
  EXPECT_FALSE(
      reg.IsValidGrantOrigin(url::Origin::Create(GURL("file:///etc/passwd"))));
  EXPECT_FALSE(
      reg.IsValidGrantOrigin(url::Origin::Create(GURL("javascript:void(0)"))));
  EXPECT_FALSE(reg.IsValidGrantOrigin(
      url::Origin::Create(GURL("data:text/html,hello"))));
  EXPECT_FALSE(
      reg.IsValidGrantOrigin(url::Origin::Create(GURL("chrome://version"))));
}

TEST(MahoMcpCapabilityRegistryTest, HandlesIncomingDomainList) {
  MahoMcpCapabilityRegistry reg;
  // The rust-side sandbox handler validates origin strings before calling
  // GrantExactOrigin. Wildcards cannot be constructed as url::Origin
  // at all, so the registry itself never sees them.
  // Verify that the handler path (the only caller that can receive wildcard
  // strings) is tested through the session-level tool-call tests.
  const url::Origin a = url::Origin::Create(GURL("https://example.com"));
  EXPECT_TRUE(reg.GrantExactOrigin(a));
  EXPECT_TRUE(reg.IsOriginGranted(a));
}

TEST(MahoMcpCapabilityRegistryTest, TabOwnership) {
  MahoMcpCapabilityRegistry reg;
  EXPECT_EQ(reg.AdoptTab(42), MahoMcpCapabilityRegistry::AdoptResult::kOk);
  EXPECT_TRUE(reg.IsTabOwned(42));
  EXPECT_EQ(reg.AdoptTab(42),
            MahoMcpCapabilityRegistry::AdoptResult::kAlreadyOwnedByThisSession);
  reg.ReleaseTab(42);
  EXPECT_FALSE(reg.IsTabOwned(42));
  EXPECT_EQ(reg.ListOwnedTabs().size(), 0u);
}

TEST(MahoMcpCapabilityRegistryTest, ClearResetsEverything) {
  MahoMcpCapabilityRegistry reg;
  const url::Origin a = url::Origin::Create(GURL("https://example.com"));
  reg.GrantExactOrigin(a);
  reg.AdoptTab(99);
  EXPECT_EQ(reg.ListGrantedOrigins().size(), 1u);
  EXPECT_EQ(reg.ListOwnedTabs().size(), 1u);
  reg.Clear();
  EXPECT_EQ(reg.ListGrantedOrigins().size(), 0u);
  EXPECT_EQ(reg.ListOwnedTabs().size(), 0u);
  EXPECT_FALSE(reg.IsOriginGranted(a));
  EXPECT_FALSE(reg.IsTabOwned(99));
}

// ======================================================================
// Truthful Input Remediation Tests
// ======================================================================

class LegacyContractTestDelegate : public MahoMcpBrowserDelegate {
 public:
  int click_count = 0;
  int type_count = 0;
  ui::AXNodeID last_clicked_ax_id = 0;
  ui::AXNodeID last_typed_ax_id = 0;
  std::string last_typed_text;

  std::vector<MahoMcpSession::TabInfo> GetTabList() override {
    MahoMcpSession::TabInfo tab;
    tab.id = 1;
    tab.is_active = true;
    tab.targetable = true;
    tab.url = "https://example.com";
    return {tab};
  }
  std::vector<MahoMcpSession::ConsoleMessage> GetConsoleMessages(int tab_id) override { return {}; }
  std::vector<MahoMcpSession::NavigationEvent> GetNavigationEvents(int tab_id, int64_t since_ms) override { return {}; }
  std::string GetPageText(int tab_id) override { return ""; }

  // Legacy 2-argument GetAccessibilitySnapshot implementation (no snapshot token override)
  base::Value GetAccessibilitySnapshot(int tab_id, MahoMcpSession::RefTable* out_refs) override {
    if (out_refs) {
      (*out_refs)[1] = 101;
      (*out_refs)[2] = 202;
    }
    base::DictValue snapshot;
    snapshot.Set("role", "WebArea");
    return base::Value(std::move(snapshot));
  }

  PageContentResult GetPageContent(int tab_id) override { return {}; }
  PageContextResult GetPageContext(int tab_id) override { return {}; }
  SearchResult SearchInPage(int tab_id, const std::string& query) override { return {}; }
  QuerySelectorResult QuerySelector(int tab_id, const std::string& selector) override { return {}; }
  std::string GetElementText(int tab_id, const std::string& ref_id) override { return ""; }
  std::string GetElementAttribute(int tab_id, const std::string& ref_id, const std::string& attribute) override { return ""; }
  bool WaitForSelector(int tab_id, const std::string& selector, int timeout_ms) override { return false; }
  int CreateNewTab(const GURL& url) override { return 0; }
  bool CloseTab(int tab_id) override { return false; }
  std::vector<BookmarkInfo> SearchBookmarks(const std::string& query) override { return {}; }
  BookmarkInfo CreateBookmark(const std::string& title, const GURL& url, const std::string& folder) override { return {}; }
  std::vector<HistoryEntry> SearchHistory(const std::string& query, size_t max_results) override { return {}; }
  void CaptureFullPagePngBase64(int tab_id, base::OnceCallback<void(std::string)> callback) override { std::move(callback).Run(""); }
  void CaptureElementPngBase64(int tab_id, ui::AXNodeID ax_id, base::OnceCallback<void(std::string)> callback) override { std::move(callback).Run(""); }
  bool Scroll(int tab_id, const std::string& direction, int pixels, std::optional<ui::AXNodeID> ax_id) override { return false; }
  bool Click(int tab_id, ui::AXNodeID ax_id) override {
    ++click_count;
    last_clicked_ax_id = ax_id;
    return true;
  }
  bool Type(int tab_id, ui::AXNodeID ax_id, const std::string& text) override {
    ++type_count;
    last_typed_ax_id = ax_id;
    last_typed_text = text;
    return true;
  }
  bool Select(int tab_id, ui::AXNodeID ax_id, const std::string& value) override { return false; }
  bool Hover(int tab_id, ui::AXNodeID ax_id) override { return false; }
  bool KeyPress(int tab_id, const std::string& key, const std::vector<std::string>& modifiers) override { return false; }
  bool ActivateTab(int tab_id) override { return false; }
  bool Navigate(int tab_id, const GURL& url) override { return false; }
  bool SetViewportSize(int tab_id, int width, int height) override { return false; }
  void StartNetworkCapture(const std::string& capture_id, int tab_id, const ResolvedMahoMcpTarget& target, StartNetworkCaptureCallback callback) override {}
  void StopNetworkCapture(const std::string& capture_id, const ResolvedMahoMcpTarget& target, StopNetworkCaptureCallback callback) override {}
  void CancelNetworkCapture(const std::string& capture_id) override {}
};

TEST_F(MahoMcpSessionTest, LegacyDelegateWithoutSnapshotTokensPreservesRefsAndAllowsMutations) {
  auto legacy_delegate = std::make_unique<LegacyContractTestDelegate>();
  MahoMcpSession::SetBrowserDelegate(legacy_delegate.get());

  // Call browser_accessibility_snapshot
  auto snapshot_res = CallToolRaw("browser_accessibility_snapshot", R"({"tab_id":1})", 401);
  ASSERT_EQ(snapshot_res.size(), 1u);
  base::Value parsed_snapshot = ParseLine(snapshot_res[0]);
  EXPECT_FALSE(parsed_snapshot.GetDict().FindDict("error")) << snapshot_res[0];

  // Ref table should be preserved, not cleared
  EXPECT_EQ(session_->ref_table().size(), 2u);
  EXPECT_EQ(session_->ref_table()[1], 101);
  EXPECT_EQ(session_->ref_table()[2], 202);

  // Call browser_click on ref 1
  auto click_res = CallToolRaw("browser_click", R"({"tab_id":1,"ref":1})", 402);
  ASSERT_EQ(click_res.size(), 1u);
  base::Value parsed_click = ParseLine(click_res[0]);
  EXPECT_FALSE(parsed_click.GetDict().FindDict("error")) << click_res[0];
  EXPECT_EQ(legacy_delegate->click_count, 1);
  EXPECT_EQ(legacy_delegate->last_clicked_ax_id, 101);

  // Call browser_type on ref 2
  auto type_res = CallToolRaw("browser_type", R"({"tab_id":1,"ref":2,"text":"sample"})", 403);
  ASSERT_EQ(type_res.size(), 1u);
  base::Value parsed_type = ParseLine(type_res[0]);
  EXPECT_FALSE(parsed_type.GetDict().FindDict("error")) << type_res[0];
  EXPECT_EQ(legacy_delegate->type_count, 1);
  EXPECT_EQ(legacy_delegate->last_typed_ax_id, 202);
  EXPECT_EQ(legacy_delegate->last_typed_text, "sample");

  MahoMcpSession::SetBrowserDelegate(nullptr);
}

TEST(MahoMcpTruthfulInputTest, StaleReferenceErrorCodeContract) {
  EXPECT_EQ(kMahoMcpErrorStaleReference, -32009);
  EXPECT_STREQ(
      kMahoMcpMessageStaleReference,
      "accessibility snapshot target changed; refs are session-scoped - "
      "capture a new snapshot (browser_snapshot) in this session, then retry "
      "with the new ref");
}

TEST(MahoMcpTruthfulInputTest, CredentialFieldClassificationProtection) {
  // 1. Unclassified fails closed
  MahoMcpFieldMetadata unclassified;
  unclassified.classified = false;
  EXPECT_TRUE(MahoMcpSession::IsCredentialField(unclassified));

  // 2. Protected bit (password)
  MahoMcpFieldMetadata protected_input;
  protected_input.classified = true;
  protected_input.is_protected = true;
  EXPECT_TRUE(MahoMcpSession::IsCredentialField(protected_input));

  // 3. Sensitive autocomplete attributes
  for (const std::string& ac : {"current-password", "new-password", "one-time-code", "otp", "totp", "recovery"}) {
    MahoMcpFieldMetadata meta;
    meta.classified = true;
    meta.autocomplete = ac;
    EXPECT_TRUE(MahoMcpSession::IsCredentialField(meta)) << "Failed for autocomplete: " << ac;
  }

  // 4. Sensitive accessible names
  for (const std::string& name : {"Password", "Enter 2fa token", "Passcode", "One-time code", "Recovery key", "Backup code"}) {
    MahoMcpFieldMetadata meta;
    meta.classified = true;
    meta.accessible_name = base::ToLowerASCII(name);
    EXPECT_TRUE(MahoMcpSession::IsCredentialField(meta)) << "Failed for name: " << name;
  }

  // 5. Ordinary non-credential field
  MahoMcpFieldMetadata ordinary;
  ordinary.classified = true;
  ordinary.is_protected = false;
  ordinary.autocomplete = "username";
  ordinary.accessible_name = "search query";
  EXPECT_FALSE(MahoMcpSession::IsCredentialField(ordinary));
}

TEST_F(MahoMcpSessionTest, DuplicateRoleNameFailsClosed) {
  InitResolvingSession();
  delegate_->mock_locator_nodes = {
      {.ax_id = 101, .role = "button", .name = "Continue"},
      {.ax_id = 102, .role = "button", .name = "Continue"},
  };

  auto responses = CallToolRaw(
      "input.locator_click",
      R"({"tab_id":7,"locator":{"role":"button","name":"Continue","exact":true}})",
      101);
  ASSERT_EQ(responses.size(), 1u);
  base::Value response = ParseLine(responses[0]);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error) << responses[0];
  const base::DictValue* data = error->FindDict("data");
  ASSERT_TRUE(data);
  const std::string* code = data->FindString("code");
  ASSERT_TRUE(code);
  EXPECT_EQ(*code, "locator_ambiguous");
  EXPECT_EQ(data->FindInt("matches").value_or(0), 2);
  EXPECT_EQ(delegate_->browser_click_call_count, 0);
}

TEST_F(MahoMcpSessionTest, CssLocatorMapsExactNode) {
  InitResolvingSession();
  delegate_->mock_locator_nodes = {
      {.ax_id = 101, .role = "button", .name = "Submit", .css = "#submit"},
  };

  auto responses = CallToolRaw(
      "input.locator_click",
      R"({"tab_id":7,"locator":{"css":"#submit"}})",
      102);
  ASSERT_EQ(responses.size(), 1u);
  base::Value output = ParseExecutionOutput(responses[0]);
  const base::DictValue* result = output.GetIfDict();
  ASSERT_TRUE(result);
  const base::DictValue* action = result->FindDict("action");
  ASSERT_TRUE(action);
  EXPECT_TRUE(action->FindBool("dispatched").value_or(false));
  EXPECT_EQ(delegate_->browser_click_call_count, 1);
  EXPECT_EQ(delegate_->browser_click_ax_id, 101);
}

TEST_F(MahoMcpSessionTest, DetachedNodeError) {
  InitResolvingSession();
  delegate_->mock_locator_nodes = {
      {.ax_id = 101, .role = "button", .name = "Submit", .css = "#detached-btn", .attached = false},
  };

  auto responses = CallToolRaw(
      "input.locator_click",
      R"({"tab_id":7,"locator":{"css":"#detached-btn"}})",
      103);
  ASSERT_EQ(responses.size(), 1u);
  base::Value response = ParseLine(responses[0]);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error) << responses[0];
  const base::DictValue* data = error->FindDict("data");
  ASSERT_TRUE(data);
  const std::string* code = data->FindString("code");
  ASSERT_TRUE(code);
  EXPECT_EQ(*code, "locator_detached");
  EXPECT_EQ(delegate_->browser_click_call_count, 0);
}

TEST_F(MahoMcpSessionTest, DisabledNodeError) {
  InitResolvingSession();
  delegate_->mock_locator_nodes = {
      {.ax_id = 101, .role = "button", .name = "Submit", .css = "#disabled-btn", .enabled = false},
  };

  auto responses = CallToolRaw(
      "input.locator_click",
      R"({"tab_id":7,"locator":{"css":"#disabled-btn"}})",
      104);
  ASSERT_EQ(responses.size(), 1u);
  base::Value response = ParseLine(responses[0]);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error) << responses[0];
  const base::DictValue* data = error->FindDict("data");
  ASSERT_TRUE(data);
  const std::string* code = data->FindString("code");
  ASSERT_TRUE(code);
  EXPECT_EQ(*code, "locator_not_actionable");
  EXPECT_EQ(delegate_->browser_click_call_count, 0);
}

TEST_F(MahoMcpSessionTest, ScopedLeaseCleanup) {
  InitResolvingSession(true);
  delegate_->mock_locator_nodes = {
      {.ax_id = 101, .role = "button", .name = "Submit", .css = "#action-btn"},
  };
  EXPECT_FALSE(lease_registry_->IsHeldBy(7, session_->session_id()));

  auto responses = CallToolRaw(
      "input.locator_click",
      R"({"tab_id":7,"locator":{"css":"#action-btn"},"lease":"scoped"})",
      105);
  ASSERT_EQ(responses.size(), 1u);
  base::Value response = ParseLine(responses[0]);
  EXPECT_FALSE(response.GetDict().FindDict("error")) << responses[0];
  EXPECT_EQ(delegate_->browser_click_call_count, 1);
  EXPECT_FALSE(lease_registry_->IsHeldBy(7, session_->session_id()));
}

TEST_F(MahoMcpSessionTest, NoReplayOnUnknownResult) {
  InitResolvingSession();
  delegate_->mock_locator_nodes = {
      {.ax_id = 101, .role = "button", .name = "Submit", .css = "#fail-btn"},
  };
  delegate_->click_succeeds = false;

  auto responses = CallToolRaw(
      "input.locator_click",
      R"({"tab_id":7,"locator":{"css":"#fail-btn"}})",
      106);
  ASSERT_EQ(responses.size(), 1u);
  base::Value response = ParseLine(responses[0]);
  const base::DictValue* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error) << responses[0];
  EXPECT_EQ(delegate_->browser_click_call_count, 1);
}

TEST_F(MahoMcpSessionTest, ObserveReturnsPostActionDiff) {
  InitResolvingSession();
  delegate_->mock_locator_nodes = {
      {.ax_id = 101, .role = "button", .name = "Submit", .css = "#observe-btn"},
  };

  // Initial snapshot to populate observation cache
  auto snap_responses = CallToolRaw("page.accessibility_snapshot_v2",
                                    R"({"tab_id":7,"mode":"interactive"})",
                                    1071);
  ASSERT_EQ(snap_responses.size(), 1u);

  // Click with observe: "diff"
  auto responses = CallToolRaw(
      "input.locator_click",
      R"({"tab_id":7,"locator":{"css":"#observe-btn"},"observe":"diff"})",
      1072);
  ASSERT_EQ(responses.size(), 1u);
  base::Value output = ParseExecutionOutput(responses[0]);
  const base::DictValue* result = output.GetIfDict();
  ASSERT_TRUE(result);
  const base::DictValue* obs = result->FindDict("observation");
  ASSERT_TRUE(obs);
  const std::string* token = obs->FindString("snapshot_token");
  ASSERT_TRUE(token);
  EXPECT_FALSE(token->empty());
  EXPECT_TRUE(obs->Find("diff"));
}

} // namespace
} // namespace maho
