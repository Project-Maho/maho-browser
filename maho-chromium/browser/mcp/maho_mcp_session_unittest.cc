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

#include "base/json/json_reader.h"
#include "base/json/string_escape.h"
#include "base/values.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/task_environment.h"
#include "maho/browser/mcp/maho_mcp_firewall.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

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
  int active_tab_id = 7;
  bool has_eligible_active = true;
  bool has_eligible_browser = true;
  std::set<int> eligible_tabs = {7, 11};
  int64_t generation = 1;

  int resolve_tab_calls = 0;
  int resolve_profile_calls = 0;
  int revalidate_calls = 0;

  struct PendingStart {
    std::string capture_id;
    int requested_tab_id = 0;
    StartNetworkCaptureCallback callback;
  };
  std::deque<PendingStart> pending_starts;
  std::deque<StopNetworkCaptureCallback> pending_stops;
  std::vector<std::string> canceled_capture_ids;

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
      resolution.target.generation = generation;
      return resolution;
    }
    if (eligible_tabs.find(requested_tab_id) == eligible_tabs.end()) {
      resolution.error = MahoMcpTargetError::kTabNotFound;
      return resolution;
    }
    resolution.target.valid = true;
    resolution.target.tab_id = requested_tab_id;
    resolution.target.browser_id = 100;
    resolution.target.generation = generation;
    return resolution;
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

  bool RevalidateTarget(const ResolvedMahoMcpTarget& target) override {
    ++revalidate_calls;
    if (!target.valid) {
      return false;
    }
    if (target.generation != generation) {
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

  void StartNetworkCapture(const std::string& capture_id,
                           int tab_id,
                           StartNetworkCaptureCallback callback) override {
    pending_starts.push_back(
        PendingStart{capture_id, tab_id, std::move(callback)});
  }
  void StopNetworkCapture(const std::string& capture_id,
                          StopNetworkCaptureCallback callback) override {
    pending_stops.push_back(std::move(callback));
  }
  void CancelNetworkCapture(const std::string& capture_id) override {
    canceled_capture_ids.push_back(capture_id);
  }

  std::vector<MahoMcpSession::ConsoleMessage> GetConsoleMessages(
      int tab_id) override {
    return {};
  }
  std::vector<MahoMcpSession::NavigationEvent> GetNavigationEvents(
      int tab_id,
      int64_t since_ms) override {
    return {};
  }
  std::string GetPageText(int tab_id) override { return std::string(); }
  base::Value GetAccessibilitySnapshot(
      int tab_id,
      MahoMcpSession::RefTable* out_refs) override {
    return base::Value(base::DictValue());
  }
  PageContentResult GetPageContent(int tab_id) override { return {}; }
  PageContextResult GetPageContext(int tab_id) override { return {}; }
  SearchResult SearchInPage(int tab_id, const std::string& query) override {
    return {};
  }
  QuerySelectorResult QuerySelector(int tab_id,
                                    const std::string& selector) override {
    return {};
  }
  std::string GetElementText(int tab_id, const std::string& ref_id) override {
    return std::string();
  }
  std::string GetElementAttribute(int tab_id,
                                  const std::string& ref_id,
                                  const std::string& attribute) override {
    return std::string();
  }
  bool WaitForSelector(int tab_id,
                       const std::string& selector,
                       int timeout_ms) override {
    return false;
  }
  int CreateNewTab(const GURL& url) override { return 99; }
  bool CloseTab(int tab_id) override { return true; }
  std::vector<BookmarkInfo> SearchBookmarks(const std::string& query) override {
    return {};
  }
  BookmarkInfo CreateBookmark(const std::string& title,
                              const GURL& url,
                              const std::string& folder) override {
    return BookmarkInfo();
  }
  std::vector<HistoryEntry> SearchHistory(const std::string& query,
                                          size_t max_results) override {
    return {};
  }
  void CaptureFullPagePngBase64(
      int tab_id,
      base::OnceCallback<void(std::string)> callback) override {
    std::move(callback).Run(std::string());
  }
  void CaptureElementPngBase64(
      int tab_id,
      ui::AXNodeID ax_id,
      base::OnceCallback<void(std::string)> callback) override {
    std::move(callback).Run(std::string());
  }
  bool Scroll(int tab_id,
              const std::string& direction,
              int pixels,
              std::optional<ui::AXNodeID> ax_id) override {
    return false;
  }
  bool Click(int tab_id, ui::AXNodeID ax_id) override { return false; }
  bool Type(int tab_id, ui::AXNodeID ax_id, const std::string& text) override {
    return false;
  }
  bool Select(int tab_id,
              ui::AXNodeID ax_id,
              const std::string& value) override {
    return false;
  }
  bool Hover(int tab_id, ui::AXNodeID ax_id) override { return false; }
  bool KeyPress(int tab_id,
                const std::string& key,
                const std::vector<std::string>& modifiers) override {
    return false;
  }
  bool ActivateTab(int tab_id) override { return true; }
  bool Navigate(int tab_id, const GURL& url) override { return true; }
  bool SetViewportSize(int tab_id, int width, int height) override {
    return true;
  }
};

class MahoMcpSessionTest : public testing::Test {
 protected:
  void SetUp() override {
    // Create session with matching UID (simulates same-user connection).
    session_ = std::make_unique<MahoMcpSession>(getuid());
  }

  void TearDown() override {
    // Safe no-op for tests that never installed a resolving delegate: the
    // members below stay null and SetBrowserDelegate(nullptr) is idempotent.
    session_.reset();
    MahoMcpSession::SetBrowserDelegate(nullptr);
    delegate_.reset();
  }

  // Installs a ResolvingFakeDelegate + lease registry and initializes a fresh
  // session for the R-8 target-resolution tests. Regression tests keep the
  // default SetUp() session and never call this, so the lazily-created
  // TaskEnvironment/lease registry below never come into existence for them
  // (avoids a duplicate TaskEnvironment in fixtures that own their own).
  void InitResolvingSession() {
    resolution_task_environment_ =
        std::make_unique<base::test::TaskEnvironment>();
    delegate_ = std::make_unique<ResolvingFakeDelegate>();
    MahoMcpSession::SetBrowserDelegate(delegate_.get());
    lease_registry_ = std::make_unique<MahoMcpLeaseRegistry>();
    session_ =
        std::make_unique<MahoMcpSession>(getuid(), lease_registry_.get());
    session_->SetDeferredResponseSender(base::BindRepeating(
        &MahoMcpSessionTest::CaptureDeferred, base::Unretained(this)));
    auto responses = session_->ProcessData(
        R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
        R"("params":{"protocolVersion":"2025-03-26",)"
        R"("clientInfo":{"name":"t","version":"0.1.0"}}})"
        "\n");
    EXPECT_EQ(responses.size(), 1u);
    EXPECT_EQ(session_->state(), MahoMcpSession::State::kActive);
  }

  std::vector<std::string> CallToolRaw(const std::string& name,
                                       const std::string& args,
                                       int id) {
    return session_->ProcessData(base::StringPrintf(
        R"({"jsonrpc":"2.0","method":"tools/call","id":%d,)"
        R"("params":{"name":"%s","arguments":%s}})"
        "\n",
        id, name.c_str(), args.c_str()));
  }

  base::Value ParseLine(const std::string& line) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(line, base::JSON_PARSE_RFC);
    EXPECT_TRUE(parsed.has_value());
    return parsed.has_value() ? std::move(*parsed) : base::Value();
  }

  void CaptureDeferred(std::string line) {
    deferred_responses_.push_back(std::move(line));
  }

  std::unique_ptr<base::test::TaskEnvironment> resolution_task_environment_;
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

  const auto* error = parsed->GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32600);
  EXPECT_TRUE(error->FindString("message")->find("initialize expected") !=
              std::string::npos);

  EXPECT_EQ(session_->state(), MahoMcpSession::State::kClosed);
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

  const auto& dict = parsed->GetDict();
  EXPECT_EQ(*dict.FindString("jsonrpc"), "2.0");
  EXPECT_FALSE(dict.FindDict("error"));

  const auto* result = dict.FindDict("result");
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

  const auto* result = parsed->GetDict().FindDict("result");
  ASSERT_TRUE(result);

  // Verify serverInfo.
  const auto* server_info = result->FindDict("serverInfo");
  ASSERT_TRUE(server_info);
  EXPECT_EQ(*server_info->FindString("name"), "maho-browser");
  EXPECT_EQ(*server_info->FindString("version"), "0.4.1");

  // Verify protocolVersion.
  EXPECT_EQ(*result->FindString("protocolVersion"), "2025-03-26");

  // Verify capabilities contains tools.
  const auto* capabilities = result->FindDict("capabilities");
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
  std::string tools_input =
      R"({"jsonrpc":"2.0","method":"tools/list","id":2})" "\n";
  auto responses = session_->ProcessData(tools_input);
  ASSERT_EQ(responses.size(), 1u);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(responses[0], base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());

  const auto* result = parsed->GetDict().FindDict("result");
  ASSERT_TRUE(result);
  // tools/list returns a "tools" array (empty for now — Wave 2A adds tools).
  EXPECT_TRUE(result->FindList("tools"));
}

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

// D1 allowed_domains: helper to drive a session through initialize
// followed by an arbitrary payload, returning the LAST response parsed.
class MahoMcpSessionAllowedDomainsTest : public MahoMcpSessionTest {
 protected:
  void InitializeSession() {
    std::string init =
        R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
        R"("params":{"protocolVersion":"2025-03-26",)"
        R"("clientInfo":{"name":"t","version":"0.1.0"}}})"
        "\n";
    (void)session_->ProcessData(init);
    ASSERT_EQ(session_->state(), MahoMcpSession::State::kActive);
  }

  base::Value CallTool(const std::string& payload) {
    auto responses = session_->ProcessData(payload);
    EXPECT_FALSE(responses.empty());
    if (responses.empty()) return base::Value();
    std::optional<base::Value> parsed =
        base::JSONReader::Read(responses.back(), base::JSON_PARSE_RFC);
    if (!parsed.has_value()) return base::Value();
    return std::move(*parsed);
  }
};

TEST_F(MahoMcpSessionAllowedDomainsTest, NavigateBlockedWhenEmptyAllowlist) {
  InitializeSession();
  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_navigate",)"
      R"("arguments":{"url":"https://example.com/"}}})" "\n");
  const auto* error = resp.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32000);
  const auto* data = error->FindDict("data");
  ASSERT_TRUE(data);
  EXPECT_EQ(*data->FindString("status"), "domain_approval_required");
}

TEST_F(MahoMcpSessionAllowedDomainsTest, SetAllowedDomainsThenNavigateOk) {
  InitializeSession();
  base::Value set_resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_set_allowed_domains",)"
      R"("arguments":{"domains":["example.com"]}}})" "\n");
  const auto* set_result = set_resp.GetDict().FindDict("result");
  ASSERT_TRUE(set_result);
  EXPECT_EQ(set_result->FindInt("count").value(), 1);

  base::Value nav_resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
      R"("params":{"name":"browser_navigate",)"
      R"("arguments":{"url":"https://example.com/foo"}}})" "\n");
  const auto* nav_result = nav_resp.GetDict().FindDict("result");
  ASSERT_TRUE(nav_result);
  EXPECT_EQ(nav_result->FindBool("navigated").value(), true);
}

TEST_F(MahoMcpSessionAllowedDomainsTest, WildcardMatchesSubdomainAndBare) {
  InitializeSession();
  (void)CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_set_allowed_domains",)"
      R"("arguments":{"domains":["*.github.com"]}}})" "\n");

  base::Value r1 = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
      R"("params":{"name":"browser_navigate",)"
      R"("arguments":{"url":"https://api.github.com/repos"}}})" "\n");
  ASSERT_TRUE(r1.GetDict().FindDict("result"));

  base::Value r2 = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":4,)"
      R"("params":{"name":"browser_navigate",)"
      R"("arguments":{"url":"https://github.com/"}}})" "\n");
  ASSERT_TRUE(r2.GetDict().FindDict("result"));
}

TEST_F(MahoMcpSessionAllowedDomainsTest, DisallowedHostBlockedAfterSet) {
  InitializeSession();
  (void)CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_set_allowed_domains",)"
      R"("arguments":{"domains":["example.com"]}}})" "\n");

  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
      R"("params":{"name":"browser_navigate",)"
      R"("arguments":{"url":"https://evil.example.org/"}}})" "\n");
  const auto* error = resp.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32000);
  const auto* data = error->FindDict("data");
  ASSERT_TRUE(data);
  EXPECT_EQ(*data->FindString("status"), "domain_not_allowed");
}

TEST_F(MahoMcpSessionAllowedDomainsTest, TabNewWithUrlEnforced) {
  InitializeSession();
  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_tab_new",)"
      R"("arguments":{"url":"https://example.com/"}}})" "\n");
  const auto* error = resp.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32000);
}

TEST_F(MahoMcpSessionAllowedDomainsTest, TabNewWithoutUrlUnaffected) {
  InitializeSession();
  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_tab_new",)"
      R"("arguments":{}}})" "\n");
  ASSERT_TRUE(resp.GetDict().FindDict("result"));
}

class MahoMcpSessionNewFeaturesTest : public MahoMcpSessionAllowedDomainsTest {
 protected:
  void SetUp() override {
    MahoMcpSessionAllowedDomainsTest::SetUp();
    InitializeSession();
  }
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
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

  std::vector<MahoMcpSession::ConsoleMessage> GetConsoleMessages(
      int tab_id) override {
    return {};
  }

  std::vector<MahoMcpSession::NavigationEvent> GetNavigationEvents(
      int tab_id,
      int64_t since_ms) override {
    return {};
  }

  std::string GetPageText(int tab_id) override { return std::string(); }

  base::Value GetAccessibilitySnapshot(
      int tab_id,
      MahoMcpSession::RefTable* out_refs) override {
    return base::Value(base::DictValue());
  }

  void CaptureFullPagePngBase64(
      int tab_id,
      base::OnceCallback<void(std::string)> callback) override {
    std::move(callback).Run(std::string());
  }

  bool Scroll(int tab_id,
              const std::string& direction,
              int pixels,
              std::optional<ui::AXNodeID> ax_id) override {
    return false;
  }
  bool Click(int tab_id, ui::AXNodeID ax_id) override { return false; }
  bool Type(int tab_id, ui::AXNodeID ax_id, const std::string& text) override {
    return false;
  }
  bool Select(int tab_id,
              ui::AXNodeID ax_id,
              const std::string& value) override {
    return false;
  }
  bool Hover(int tab_id, ui::AXNodeID ax_id) override { return false; }
  bool KeyPress(int tab_id,
                const std::string& key,
                const std::vector<std::string>& modifiers) override {
    return false;
  }
  bool ActivateTab(int tab_id) override { return false; }
  bool Navigate(int tab_id, const GURL& url) override { return false; }
  bool SetViewportSize(int tab_id, int width, int height) override {
    return false;
  }

  PageContentResult GetPageContent(int tab_id) override { return {}; }
  PageContextResult GetPageContext(int tab_id) override { return {}; }
  SearchResult SearchInPage(int tab_id, const std::string& query) override {
    return {};
  }
  QuerySelectorResult QuerySelector(int tab_id,
                                    const std::string& selector) override {
    return {};
  }
  std::string GetElementText(int tab_id, const std::string& ref_id) override {
    return std::string();
  }
  std::string GetElementAttribute(int tab_id,
                                  const std::string& ref_id,
                                  const std::string& attribute) override {
    return std::string();
  }
  int CreateNewTab(const GURL& url) override { return 0; }
  bool CloseTab(int tab_id) override { return false; }
  std::vector<BookmarkInfo> SearchBookmarks(
      const std::string& query) override {
    return {};
  }
  BookmarkInfo CreateBookmark(const std::string& title,
                              const GURL& url,
                              const std::string& folder) override {
    return BookmarkInfo();
  }
  void CaptureElementPngBase64(
      int tab_id,
      ui::AXNodeID ax_id,
      base::OnceCallback<void(std::string)> callback) override {
    std::move(callback).Run(std::string());
  }

  bool WaitForSelector(int tab_id,
                       const std::string& selector,
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
  const std::string& last_wait_selector_selector() const {
    return last_wait_selector_selector_;
  }
  int last_wait_selector_timeout_ms() const {
    return last_wait_selector_timeout_ms_;
  }

  std::vector<HistoryEntry> SearchHistory(const std::string& query,
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
  const std::string& last_search_history_query() const {
    return last_search_history_query_;
  }
  size_t last_search_history_max_results() const {
    return last_search_history_max_results_;
  }

  void StartNetworkCapture(
      const std::string& capture_id,
      int tab_id,
      StartNetworkCaptureCallback callback) override {
    int resolved = tab_id;
    if (resolved == 0) {
      for (const auto& t : tabs_) {
        if (t.is_active) {
          resolved = t.id;
          break;
        }
      }
    }
    bool exists = false;
    for (const auto& t : tabs_) {
      if (t.id == resolved) {
        exists = true;
        break;
      }
    }
    pending_starts_.push_back(PendingStart{capture_id, tab_id, resolved, exists,
                                           std::move(callback)});
  }

  void StopNetworkCapture(
      const std::string& capture_id,
      StopNetworkCaptureCallback callback) override {
    pending_stops_.push_back(PendingStop{capture_id, std::move(callback)});
  }

  void CancelNetworkCapture(const std::string& capture_id) override {
    canceled_capture_ids_.push_back(capture_id);
  }

  void SetActiveTab(int tab_id) {
    for (auto& tab : tabs_) {
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
  const std::vector<std::string>& canceled_capture_ids() const {
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
  auto delegate = std::make_unique<FakeAsyncNetworkDelegate>();

  MahoMcpSession::TabInfo live_tab;
  live_tab.id = 7;
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

  delegate->SetTabsForTesting(
      {live_tab, suspended_tab, sidebar_only_tab});
  MahoMcpSession::SetBrowserDelegate(delegate.get());

  session_->ProcessData(
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("clientInfo":{"name":"test-client","version":"0.1.0"}}})"
      "\n");
  ASSERT_EQ(session_->state(), MahoMcpSession::State::kActive);

  auto responses = CallToolRaw("browser_tab_list", "{}", 2);
  ASSERT_EQ(responses.size(), 1u);
  base::Value parsed = ParseLine(responses[0]);
  const auto* result = parsed.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto* tabs = result->FindList("tabs");
  ASSERT_TRUE(tabs);
  ASSERT_EQ(tabs->size(), 3u);

  const auto& live = (*tabs)[0].GetDict();
  EXPECT_EQ(live.FindInt("id").value(), 7);
  EXPECT_EQ(*live.FindString("stable_id"), "stable-live");
  EXPECT_EQ(live.FindBool("targetable").value(), true);
  EXPECT_EQ(live.FindInt("tab_strip_index").value(), 0);

  const auto& suspended = (*tabs)[1].GetDict();
  EXPECT_EQ(suspended.FindInt("id").value(), 0);
  EXPECT_EQ(*suspended.FindString("stable_id"), "stable-suspended");
  EXPECT_EQ(suspended.FindBool("targetable").value(), false);
  EXPECT_EQ(suspended.FindInt("tab_strip_index").value(), -1);

  const auto& sidebar_only = (*tabs)[2].GetDict();
  EXPECT_EQ(sidebar_only.FindInt("id").value(), 0);
  EXPECT_EQ(*sidebar_only.FindString("stable_id"), "stable-sidebar-only");
  EXPECT_EQ(sidebar_only.FindBool("targetable").value(), false);
  EXPECT_EQ(sidebar_only.FindInt("tab_strip_index").value(), -1);
}

class FakeMailReadDelegate : public FakeAsyncNetworkDelegate {
 public:
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

  void MailListFolders(const std::string& account_id,
                       MailReadCallback callback) override {
    PendingMailRead pending;
    pending.method = MailReadMethod::kListFolders;
    pending.account_id = account_id;
    pending.callback = std::move(callback);
    pending_mail_reads_.push_back(std::move(pending));
  }

  void MailListEmails(const std::string& account_id,
                      const std::string& folder_id,
                      int64_t limit,
                      int64_t offset,
                      MailReadCallback callback) override {
    PendingMailRead pending;
    pending.method = MailReadMethod::kListEmails;
    pending.account_id = account_id;
    pending.folder_id = folder_id;
    pending.limit = limit;
    pending.offset = offset;
    pending.callback = std::move(callback);
    pending_mail_reads_.push_back(std::move(pending));
  }

  void MailGetEmail(const std::string& email_id,
                    MailReadCallback callback) override {
    PendingMailRead pending;
    pending.method = MailReadMethod::kGetEmail;
    pending.email_id = email_id;
    pending.callback = std::move(callback);
    pending_mail_reads_.push_back(std::move(pending));
  }

  void MailSearchEmails(const std::string& query_json,
                        MailReadCallback callback) override {
    PendingMailRead pending;
    pending.method = MailReadMethod::kSearchEmails;
    pending.query_json = query_json;
    pending.callback = std::move(callback);
    pending_mail_reads_.push_back(std::move(pending));
  }

  void MailListThread(const std::string& account_id,
                      const std::string& message_id,
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

  size_t pending_mail_read_count() const {
    return pending_mail_reads_.size();
  }

 private:
  std::deque<PendingMailRead> pending_mail_reads_;
};

class MahoMcpSessionMailReadTest : public testing::Test {
 protected:
  void SetUp() override {
    delegate_ = std::make_unique<FakeMailReadDelegate>();
    MahoMcpSession::SetBrowserDelegate(delegate_.get());
    session_ = std::make_unique<MahoMcpSession>(getuid());
    session_->SetDeferredResponseSender(base::BindRepeating(
        &MahoMcpSessionMailReadTest::CaptureDeferred,
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
    session_.reset();
    MahoMcpSession::SetBrowserDelegate(nullptr);
    delegate_.reset();
  }

  std::vector<std::string> CallToolRaw(const std::string& name,
                                       const std::string& args,
                                       int id) {
    return session_->ProcessData(base::StringPrintf(
        R"({"jsonrpc":"2.0","method":"tools/call","id":%d,)"
        R"("params":{"name":"%s","arguments":%s}})"
        "\n",
        id, name.c_str(), args.c_str()));
  }

  base::Value ParseLine(const std::string& line) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(line, base::JSON_PARSE_RFC);
    EXPECT_TRUE(parsed.has_value());
    return parsed.has_value() ? std::move(*parsed) : base::Value();
  }

  void CaptureDeferred(std::string line) {
    deferred_responses_.push_back(std::move(line));
  }

  void ExpectImmediateError(const std::vector<std::string>& responses,
                            int expected_code) {
    ASSERT_EQ(responses.size(), 1u);
    base::Value response = ParseLine(responses.front());
    const auto* error = response.GetDict().FindDict("error");
    ASSERT_TRUE(error);
    EXPECT_EQ(error->FindInt("code").value(), expected_code);
  }

  void ExpectSingleDeferredTextPayload(const std::string& expected_json) {
    ASSERT_EQ(deferred_responses_.size(), 1u);
    base::Value response = ParseLine(deferred_responses_.back());
    const auto* result = response.GetDict().FindDict("result");
    ASSERT_TRUE(result);
    const auto* content = result->FindList("content");
    ASSERT_TRUE(content);
    ASSERT_EQ(content->size(), 1u);
    const auto* item = (*content)[0].GetIfDict();
    ASSERT_TRUE(item);
    EXPECT_EQ(*item->FindString("type"), "text");
    const std::string* text = item->FindString("text");
    ASSERT_TRUE(text);
    EXPECT_EQ(*text, expected_json);
    EXPECT_TRUE(base::JSONReader::Read(*text, base::JSON_PARSE_RFC).has_value());
    deferred_responses_.clear();
  }

  std::unique_ptr<FakeMailReadDelegate> delegate_;
  std::unique_ptr<MahoMcpSession> session_;
  std::vector<std::string> deferred_responses_;
};

TEST_F(MahoMcpSessionMailReadTest,
       MailRequiredArgumentValidationRejectsMissingOrWrongTypes) {
  struct Case {
    const char* tool;
    const char* args;
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
  for (const Case& c : cases) {
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
    auto responses = CallToolRaw(
        "mail_list_emails",
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
    const auto& query_dict = search_query->GetDict();
    EXPECT_EQ(*query_dict.FindString("query"), "receipt");
    EXPECT_EQ(*query_dict.FindString("account_id"), "acct");
    EXPECT_EQ(*query_dict.FindString("folder_id"), "inbox");
    EXPECT_EQ(query_dict.FindInt("limit").value(), 7);
    EXPECT_EQ(query_dict.FindInt("offset").value(), 3);

    std::move(pending.callback).Run(true, R"({"emails":[{"id":"match"}]})");
    ExpectSingleDeferredTextPayload(R"({"emails":[{"id":"match"}]})");
  }

  {
    auto responses = CallToolRaw(
        "mail_list_thread",
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
  const auto* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32000);
  EXPECT_EQ(*error->FindString("message"), "broker offline");
}

TEST_F(MahoMcpSessionMailReadTest,
       MailExtractOtpReturnsSynchronousDocumentedErrorOnly) {
  auto responses = CallToolRaw("mail_extract_otp", R"({})", 400);

  ASSERT_EQ(responses.size(), 1u);
  base::Value response = ParseLine(responses.front());
  const auto* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32000);
  EXPECT_EQ(*error->FindString("message"),
            "mail_extract_otp is not served by the mail read broker in this "
            "build; one-time passwords are delivered through the browser OTP "
            "push/arbitration path.");
  EXPECT_EQ(delegate_->pending_mail_read_count(), 0u);
  EXPECT_TRUE(deferred_responses_.empty());
}

class MahoMcpSessionNetworkCaptureTest : public testing::Test {
 protected:
  void SetUp() override {
    delegate_ = std::make_unique<FakeAsyncNetworkDelegate>();
    MahoMcpSession::SetBrowserDelegate(delegate_.get());
    session_ = std::make_unique<MahoMcpSession>(getuid());
    session_->SetDeferredResponseSender(base::BindRepeating(
        &MahoMcpSessionNetworkCaptureTest::CaptureDeferred,
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
    session_.reset();
    MahoMcpSession::SetBrowserDelegate(nullptr);
    delegate_.reset();
  }

  std::vector<std::string> CallToolRaw(const std::string& name,
                                       const std::string& args,
                                       int id) {
    return session_->ProcessData(base::StringPrintf(
        R"({"jsonrpc":"2.0","method":"tools/call","id":%d,)"
        R"("params":{"name":"%s","arguments":%s}})"
        "\n",
        id, name.c_str(), args.c_str()));
  }

  base::Value ParseLine(const std::string& line) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(line, base::JSON_PARSE_RFC);
    EXPECT_TRUE(parsed.has_value());
    return std::move(*parsed);
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
  base::Value response = ParseLine(deferred_responses_.back());
  const auto* result_dict = response.GetDict().FindDict("result");
  ASSERT_TRUE(result_dict);
  EXPECT_TRUE(result_dict->FindBool("capturing").value());
  EXPECT_EQ(result_dict->FindInt("tab_id").value(), 11);
}

TEST_F(MahoMcpSessionNetworkCaptureTest, NetworkStartActiveTabResolvedByDelegate) {
  auto responses =
      CallToolRaw("browser_network_start_capture", R"({})", 21);

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
  base::Value response = ParseLine(deferred_responses_.back());
  const auto* result_dict = response.GetDict().FindDict("result");
  ASSERT_TRUE(result_dict);
  EXPECT_EQ(result_dict->FindInt("tab_id").value(), 7);
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
  const auto* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(*error->FindString("message"), "Network capture already active");
}

TEST_F(MahoMcpSessionNetworkCaptureTest, NetworkStartFailureThenSuccessfulRetry) {
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
  EXPECT_TRUE(ParseLine(deferred_responses_.back()).GetDict().FindDict("error"));
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
  base::Value response = ParseLine(deferred_responses_.back());
  const auto* result_dict = response.GetDict().FindDict("result");
  ASSERT_TRUE(result_dict);
  EXPECT_EQ(result_dict->FindInt("tab_id").value(), 7);
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
  std::move(stop.callback).Run(MahoMcpFirewall::WrapHar(std::move(har)));

  ASSERT_EQ(deferred_responses_.size(), 1u);
  base::Value response = ParseLine(deferred_responses_.back());
  EXPECT_EQ(response.GetDict().FindInt("id").value(), 23);
  const auto* result = response.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_EQ(*result->FindString("version"), "1.2");
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

  ASSERT_TRUE(CallToolRaw("browser_network_stop_capture", R"({})", 25)
                  .empty());
  auto stop = delegate_->TakeStop();
  std::move(stop.callback)
      .Run(MahoMcpFirewall::WrapHar(HarWithSecretUrlAndHeaders()));

  ASSERT_EQ(deferred_responses_.size(), 1u);
  base::Value response = ParseLine(deferred_responses_.back());
  const auto* result = response.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto* entries = result->FindList("entries");
  ASSERT_TRUE(entries);
  ASSERT_EQ(entries->size(), 1u);
  const auto* entry = (*entries)[0].GetIfDict();
  ASSERT_TRUE(entry);
  const auto* request = entry->FindDict("request");
  ASSERT_TRUE(request);
  EXPECT_EQ(*request->FindString("url"),
            "https://example.com/api?access_token=[REDACTED]");
  const auto* request_headers = request->FindDict("headers");
  ASSERT_TRUE(request_headers);
  EXPECT_EQ(*request_headers->FindString("Authorization"), "[REDACTED]");
  const auto* response_headers =
      entry->FindDict("response")->FindDict("headers");
  ASSERT_TRUE(response_headers);
  EXPECT_EQ(*response_headers->FindString("Set-Cookie"), "[REDACTED]");
}

TEST_F(MahoMcpSessionNetworkCaptureTest, NetworkStopUnknownCaptureReturnsError) {
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
  std::move(stop.callback).Run(std::nullopt);

  ASSERT_EQ(deferred_responses_.size(), 1u);
  base::Value response = ParseLine(deferred_responses_.back());
  EXPECT_FALSE(response.GetDict().FindDict("result"));
  const auto* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(*error->FindString("message"),
            "Network capture not found or already stopped");
}

TEST_F(MahoMcpSessionNetworkCaptureTest, NetworkStopWithoutActiveCaptureErrors) {
  auto responses = CallToolRaw("browser_network_stop_capture", R"({})", 31);

  ASSERT_EQ(responses.size(), 1u);
  base::Value response = ParseLine(responses.front());
  const auto* error = response.GetDict().FindDict("error");
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

TEST_F(MahoMcpSessionNewFeaturesTest, ConsoleMessages_RingBuffer_HoldsUpToLimit) {
  for (int i = 0; i < 501; ++i) {
    MahoMcpSession::ConsoleMessage msg;
    msg.level = "log";
    msg.message = "message " + std::to_string(i);
    msg.source_url = "https://example.com";
    msg.line = i;
    msg.timestamp_ms = 1000 + i;
    session_->AddConsoleMessageForTesting(0, std::move(msg));
  }

  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_console_messages",)"
      R"("arguments":{"tab_id":0,"limit":500}}})" "\n");

  const auto* result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto* messages = result->FindList("messages");
  ASSERT_TRUE(messages);
  EXPECT_EQ(messages->size(), 500u);
  const auto* first_msg = (*messages)[0].GetIfDict();
  ASSERT_TRUE(first_msg);
  EXPECT_EQ(*first_msg->FindString("message"), "message 1");
}

TEST_F(MahoMcpSessionNewFeaturesTest, ConsoleMessages_LevelFilter_ReturnsMatchingOnly) {
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

  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_console_messages",)"
      R"("arguments":{"tab_id":0,"level_filter":["error"]}}})" "\n");

  const auto* result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto* messages = result->FindList("messages");
  ASSERT_TRUE(messages);
  EXPECT_EQ(messages->size(), 1u);
  const auto* first_msg = (*messages)[0].GetIfDict();
  ASSERT_TRUE(first_msg);
  EXPECT_EQ(*first_msg->FindString("message"), "error message");
}

TEST_F(MahoMcpSessionNewFeaturesTest, ConsoleMessages_SinceTimestamp_ReturnsAfterCutoff) {
  for (int i = 0; i < 3; ++i) {
    MahoMcpSession::ConsoleMessage msg;
    msg.level = "log";
    msg.message = "message " + std::to_string(i);
    msg.timestamp_ms = 1000 + i;
    session_->AddConsoleMessageForTesting(0, std::move(msg));
  }

  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_console_messages",)"
      R"("arguments":{"tab_id":0,"since_timestamp_ms":1001}}})" "\n");

  const auto* result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto* messages = result->FindList("messages");
  ASSERT_TRUE(messages);
  EXPECT_EQ(messages->size(), 1u);
  const auto* first_msg = (*messages)[0].GetIfDict();
  ASSERT_TRUE(first_msg);
  EXPECT_EQ(*first_msg->FindString("message"), "message 2");
}

TEST_F(MahoMcpSessionNewFeaturesTest, ConsoleMessages_Firewall_ScrubsAuthorizationInMessage) {
  MahoMcpSession::ConsoleMessage msg;
  msg.level = "log";
  msg.message = "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMiIsInVzZXIiOiJhZG1pbiJ9.signature";
  session_->AddConsoleMessageForTesting(0, std::move(msg));

  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_console_messages",)"
      R"("arguments":{"tab_id":0}}})" "\n");

  const auto* result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto* messages = result->FindList("messages");
  ASSERT_TRUE(messages);
  ASSERT_EQ(messages->size(), 1u);
  const auto* first_msg = (*messages)[0].GetIfDict();
  ASSERT_TRUE(first_msg);
  EXPECT_EQ(*first_msg->FindString("message"), "Bearer [REDACTED_JWT]");
}

TEST_F(MahoMcpSessionNewFeaturesTest, ConsoleMessages_Firewall_ScrubsAccessTokenInUrl) {
  MahoMcpSession::ConsoleMessage msg;
  msg.level = "log";
  msg.message = "Loaded resource";
  msg.source_url = "https://example.com/api?access_token=super-secret-token";
  session_->AddConsoleMessageForTesting(0, std::move(msg));

  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_console_messages",)"
      R"("arguments":{"tab_id":0}}})" "\n");

  const auto* result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto* messages = result->FindList("messages");
  ASSERT_TRUE(messages);
  ASSERT_EQ(messages->size(), 1u);
  const auto* first_msg = (*messages)[0].GetIfDict();
  ASSERT_TRUE(first_msg);
  EXPECT_EQ(*first_msg->FindString("source_url"), "https://example.com/api?access_token=[REDACTED]");
}

TEST_F(MahoMcpSessionNewFeaturesTest, WaitForNavigation_ReturnsImmediately) {
  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_wait_for_navigation",)"
      R"("arguments":{"tab_id":0}}})" "\n");

  const auto* result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_EQ(*result->FindString("url"), "about:blank");
}

TEST_F(MahoMcpSessionNewFeaturesTest, WaitForNavigation_TimesOutForUnknownTab) {
  base::Value resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_wait_for_navigation",)"
      R"("arguments":{"tab_id":999}}})" "\n");

  const auto* result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->FindBool("timeout").value());
}

TEST_F(MahoMcpSessionNewFeaturesTest, TabSwitch_UpdatesActiveTab) {
  (void)CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_set_allowed_domains",)"
      R"("arguments":{"domains":["example.com"]}}})" "\n");

  base::Value new_tab_resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
      R"("params":{"name":"browser_tab_new",)"
      R"("arguments":{"url":"https://example.com/"}}})" "\n");
  const auto* new_tab_result = new_tab_resp.GetDict().FindDict("result");
  ASSERT_TRUE(new_tab_result);
  const auto* tab = new_tab_result->FindDict("tab");
  ASSERT_TRUE(tab);
  int new_tab_id = tab->FindInt("id").value();

  base::Value switch_resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":4,)"
      R"("params":{"name":"browser_tab_switch",)"
      R"("arguments":{"tab_id":0}}})" "\n");
  const auto* switch_result = switch_resp.GetDict().FindDict("result");
  ASSERT_TRUE(switch_result);
  EXPECT_TRUE(switch_result->FindBool("activated").value());
  EXPECT_EQ(switch_result->FindInt("previous_tab_id").value(), new_tab_id);

  base::Value get_resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":5,)"
      R"("params":{"name":"browser_tab_get",)"
      R"("arguments":{"tab_id":0}}})" "\n");
  const auto* get_result = get_resp.GetDict().FindDict("result");
  ASSERT_TRUE(get_result);
  EXPECT_TRUE(get_result->FindBool("is_active").value());
}

TEST_F(MahoMcpSessionNewFeaturesTest, SetViewportSize_ValidAndOutOfBounds) {
  base::Value resp1 = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_set_viewport_size",)"
      R"("arguments":{"width_px":375,"height_px":667}}})" "\n");
  const auto* r1 = resp1.GetDict().FindDict("result");
  ASSERT_TRUE(r1);
  EXPECT_EQ(r1->FindInt("width").value(), 375);
  EXPECT_EQ(r1->FindInt("height").value(), 667);

  base::Value resp2 = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
      R"("params":{"name":"browser_set_viewport_size",)"
      R"("arguments":{"width_px":99,"height_px":667}}})" "\n");
  const auto* error = resp2.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32602);
}

TEST_F(MahoMcpSessionNewFeaturesTest, InteractiveTools_Scroll_Hover_KeyPress) {
  base::Value scroll_resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
      R"("params":{"name":"browser_scroll",)"
      R"("arguments":{"direction":"down","pixels":200}}})" "\n");
  const auto* scroll_result = scroll_resp.GetDict().FindDict("result");
  ASSERT_TRUE(scroll_result);
  EXPECT_TRUE(scroll_result->FindBool("scrolled").value());
  EXPECT_EQ(scroll_result->FindInt("new_scroll_y").value(), 200);

  base::Value key_resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
      R"("params":{"name":"browser_key_press",)"
      R"("arguments":{"key":"Enter","modifiers":["shift"]}}})" "\n");
  const auto* key_result = key_resp.GetDict().FindDict("result");
  ASSERT_TRUE(key_result);
  EXPECT_TRUE(key_result->FindBool("key_pressed").value());
  EXPECT_EQ(*key_result->FindString("key"), "Enter");

  base::Value invalid_key_resp = CallTool(
      R"({"jsonrpc":"2.0","method":"tools/call","id":4,)"
      R"("params":{"name":"browser_key_press",)"
      R"("arguments":{"key":"InvalidKeyName"}}})" "\n");
  const auto* key_error = invalid_key_resp.GetDict().FindDict("error");
  ASSERT_TRUE(key_error);
  EXPECT_EQ(key_error->FindInt("code").value(), -32602);
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

  base::Value response = ParseLine(responses.front());
  const auto* result = response.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->FindBool("found").value());
}

TEST_F(MahoMcpSessionNetworkCaptureTest,
       WaitForSelectorDefaultsTimeoutAndMarshalsFoundFalse) {
  delegate_->set_wait_selector_return(false);
  auto responses =
      CallToolRaw("page_wait_for_selector", R"({"selector":"#missing"})", 61);

  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(delegate_->last_wait_selector_selector(), "#missing");
  EXPECT_EQ(delegate_->last_wait_selector_timeout_ms(), 10000);

  base::Value response = ParseLine(responses.front());
  const auto* result = response.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_FALSE(result->FindBool("found").value());
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

  base::Value response = ParseLine(deferred_responses_.back());
  const auto* result = response.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto* entries = result->FindList("entries");
  ASSERT_TRUE(entries);
  ASSERT_EQ(entries->size(), 3u);

  const auto& e0 = (*entries)[0].GetDict();
  EXPECT_EQ(*e0.FindString("url"), "https://example.com/a");
  EXPECT_EQ(*e0.FindString("title"), "Alpha");
  EXPECT_EQ(e0.FindDouble("visited_at").value(), 1716000000.0);

  const auto& e1 = (*entries)[1].GetDict();
  EXPECT_EQ(*e1.FindString("url"), "https://example.com/b");
  EXPECT_EQ(*e1.FindString("title"), "Beta");

  const auto& e2 = (*entries)[2].GetDict();
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

  base::Value response = ParseLine(deferred_responses_.back());
  const auto* result = response.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  const auto* entries = result->FindList("entries");
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
  // No production delegate: the advertised tool set is independent of the
  // private-context boundary.
  MahoMcpSession::SetBrowserDelegate(nullptr);
  session_->ProcessData(
      R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
      R"("params":{"protocolVersion":"2025-03-26",)"
      R"("clientInfo":{"name":"t","version":"0.1.0"}}})"
      "\n");
  ASSERT_EQ(session_->state(), MahoMcpSession::State::kActive);

  auto list_responses = session_->ProcessData(
      R"({"jsonrpc":"2.0","method":"tools/list","id":2})" "\n");
  ASSERT_EQ(list_responses.size(), 1u);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(list_responses[0], base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());
  const auto* tools = parsed->GetDict().FindDict("result")->FindList("tools");
  ASSERT_TRUE(tools);

  std::set<std::string> advertised;
  for (const auto& tool : *tools) {
    const std::string* name = tool.GetDict().FindString("name");
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
      "browser_set_allowed_domains",
      "browser_get_allowed_domains",
      "browser_routines_list",
      "browser_routines_run",
      "mail_list_accounts",
      "mail_list_folders",
      "mail_list_emails",
      "mail_get_email",
      "mail_search_emails",
      "mail_list_thread",
      "mail_extract_otp",
      "mail_add_account",
      "mail_test_connection",
      "mail_delete_account",
      "mail_start_oauth",
      "mail_complete_oauth",
      "mail_reconnect_account",
      "mail_import_migration_archive",
      "mail_send",
      "mail_save_draft",
      "mail_update_draft",
      "mail_queue_email",
      "browser_acquire_lease",
      "browser_heartbeat_lease",
      "browser_release_lease",
      "browser_screenshot_full",
      "browser_screenshot_element",
      "browser_network_start_capture",
      "browser_network_stop_capture",
      "browser_network_get_har",
      "browser_accessibility_snapshot",
      "browser_click",
      "browser_type",
      "browser_select",
      "browser_console_messages",
      "browser_wait_for_navigation",
      "browser_scroll",
      "browser_hover",
      "browser_key_press",
      "browser_tab_switch",
      "browser_set_viewport_size",
      "vault_list_credentials_for_active_page",
      "vault_request_credential_use",
      "vault_fill_credential",
      "vault_fill_totp",
  };
  EXPECT_EQ(advertised, expected);

  // evaluate_js is never advertised and is rejected on direct call.
  EXPECT_EQ(advertised.count("evaluate_js"), 0u);
  auto eval_responses = session_->ProcessData(
      R"({"jsonrpc":"2.0","method":"tools/call","id":3,)"
      R"("params":{"name":"evaluate_js","arguments":{}}})" "\n");
  ASSERT_EQ(eval_responses.size(), 1u);
  std::optional<base::Value> eval =
      base::JSONReader::Read(eval_responses[0], base::JSON_PARSE_RFC);
  ASSERT_TRUE(eval.has_value());
  const auto* eval_error = eval->GetDict().FindDict("error");
  ASSERT_TRUE(eval_error);
  EXPECT_EQ(eval_error->FindInt("code").value(), -32601);

  // ping is hidden (not advertised) but functional.
  EXPECT_EQ(advertised.count("ping"), 0u);
  auto ping_responses = session_->ProcessData(
      R"({"jsonrpc":"2.0","method":"tools/call","id":4,)"
      R"("params":{"name":"ping","arguments":{}}})" "\n");
  ASSERT_EQ(ping_responses.size(), 1u);
  std::optional<base::Value> ping =
      base::JSONReader::Read(ping_responses[0], base::JSON_PARSE_RFC);
  ASSERT_TRUE(ping.has_value());
  const auto* ping_result = ping->GetDict().FindDict("result");
  ASSERT_TRUE(ping_result);
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
      "browser_accessibility_snapshot",
      "browser_click",
      "browser_type",
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
      "browser_acquire_lease",
      "browser_heartbeat_lease",
      "browser_release_lease",
      "vault_list_credentials_for_active_page",
      "vault_request_credential_use",
      "vault_fill_credential",
      "vault_fill_totp",
  };

  int id = 100;
  for (const std::string& tool : tab_target_tools) {
    const int before = delegate_->resolve_tab_calls;
    CallToolRaw(tool, "{}", id++);
    EXPECT_EQ(delegate_->resolve_tab_calls, before + 1)
        << "tool did not resolve its target exactly once: " << tool;
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
  CallToolRaw("browser_get_allowed_domains", "{}", id++);
  EXPECT_EQ(delegate_->resolve_tab_calls, tab_before);
  EXPECT_EQ(delegate_->resolve_profile_calls, prof_before);
}

// Todo 6: input/navigation mutators require an active tab lease held by this
// session. Without a lease the mutation is denied (-32007) before any delegate
// dispatch; after acquiring the lease the gate no longer blocks it.
TEST_F(MahoMcpSessionTest, MutationRequiresActiveLease) {
  InitResolvingSession();

  {
    auto responses =
        CallToolRaw("browser_scroll",
                    R"({"direction":"down","pixels":100,"tab_id":7})", 300);
    ASSERT_EQ(responses.size(), 1u);
    base::Value r = ParseLine(responses[0]);
    const auto* error = r.GetDict().FindDict("error");
    ASSERT_TRUE(error);
    EXPECT_EQ(error->FindInt("code").value(), kMahoMcpErrorLeaseRequired);
  }

  {
    auto responses = CallToolRaw("browser_acquire_lease", R"({"tab_id":7})", 301);
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
    const auto* error = r.GetDict().FindDict("error");
    if (error) {
      EXPECT_NE(error->FindInt("code").value(), kMahoMcpErrorLeaseRequired)
          << "held lease must let the mutation past the gate";
    }
  }
}

// Todo 6: read-only tools remain available with no lease held.
TEST_F(MahoMcpSessionTest, ReadOnlyToolAllowedWithoutLease) {
  InitResolvingSession();

  auto responses = CallToolRaw("browser_page_text", R"({"tab_id":7})", 310);
  ASSERT_EQ(responses.size(), 1u);
  base::Value r = ParseLine(responses[0]);
  const auto* error = r.GetDict().FindDict("error");
  if (error) {
    EXPECT_NE(error->FindInt("code").value(), kMahoMcpErrorLeaseRequired);
  }
}

TEST_F(MahoMcpSessionTest, NoCachedOrZeroActiveFallback) {
  InitResolvingSession();

  // Explicit unknown/OTR id is indistinguishable from unknown: -32004.
  {
    auto responses = CallToolRaw("browser_page_text", R"({"tab_id":4242})", 200);
    ASSERT_EQ(responses.size(), 1u);
    base::Value r = ParseLine(responses[0]);
    const auto* error = r.GetDict().FindDict("error");
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
    const auto* error = r.GetDict().FindDict("error");
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
    const auto* error = r.GetDict().FindDict("error");
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
    const auto* error = r.GetDict().FindDict("error");
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
  const auto* error = response.GetDict().FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->FindInt("code").value(), -32004);
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
  ASSERT_TRUE(ParseLine(deferred_responses_.back()).GetDict().FindDict("result"));
  deferred_responses_.clear();

  // The bound tab is closed/reused: stop must revalidate the SAME generation
  // and refuse, rather than resolving a new active target. Tab 7 remains fully
  // eligible here, so the denial proves the check is generation-bound and not
  // merely a "does some eligible tab with this id still exist" lookup.
  delegate_->generation = 5;
  ASSERT_NE(delegate_->eligible_tabs.find(7), delegate_->eligible_tabs.end());
  auto stop_responses = CallToolRaw("browser_network_stop_capture", R"({})", 401);
  ASSERT_EQ(stop_responses.size(), 1u);
  base::Value stop = ParseLine(stop_responses[0]);
  const auto* stop_error = stop.GetDict().FindDict("error");
  ASSERT_TRUE(stop_error);
  EXPECT_EQ(stop_error->FindInt("code").value(), -32004);
  EXPECT_EQ(delegate_->pending_stops.size(), 0u);
  ASSERT_EQ(delegate_->canceled_capture_ids.size(), 1u);

  // The tab whose capture was just denied is still a live, resolvable target:
  // confirms the stop denial above came from generation mismatch, not tab death.
  auto alive_responses = CallToolRaw("browser_tab_get", R"({"tab_id":7})", 4011);
  ASSERT_EQ(alive_responses.size(), 1u);
  EXPECT_TRUE(ParseLine(alive_responses[0]).GetDict().FindDict("result"));

  // Reset generation and acquire a lease on an eligible tab.
  delegate_->generation = 6;
  auto acquire = CallToolRaw("browser_acquire_lease",
                             R"({"tab_id":11,"ttl_seconds":60})", 402);
  ASSERT_EQ(acquire.size(), 1u);
  ASSERT_TRUE(ParseLine(acquire[0]).GetDict().FindDict("result"));

  // The leased tab is closed/reused (no longer eligible): a heartbeat resolves
  // through the boundary and is denied with the uniform non-revealing error.
  delegate_->eligible_tabs.erase(11);
  auto heartbeat = CallToolRaw("browser_heartbeat_lease",
                               R"({"tab_id":11,"ttl_seconds":60})", 403);
  ASSERT_EQ(heartbeat.size(), 1u);
  base::Value hb = ParseLine(heartbeat[0]);
  const auto* hb_error = hb.GetDict().FindDict("error");
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
  std::string last_typed_text;
  int vault_fill_count = 0;
  int vault_totp_fill_count = 0;
  ui::AXNodeID last_vault_ax_id = 0;
  std::string last_vault_grant_handle;
  std::vector<VaultCredentialSummary> vault_credentials;
  std::unordered_map<std::string, std::string> grant_by_handle;

  bool Type(int tab_id, ui::AXNodeID ax_id, const std::string& text) override {
    ++type_call_count;
    last_typed_text = text;
    return true;
  }

  MahoMcpFieldMetadata GetFieldMetadata(int tab_id,
                                        ui::AXNodeID ax_id) override {
    auto it = field_metadata.find(ax_id);
    return it == field_metadata.end() ? MahoMcpFieldMetadata() : it->second;
  }

  std::vector<VaultCredentialSummary> VaultListCredentialsForActivePage(
      int tab_id) override {
    return vault_credentials;
  }

  std::optional<std::string> VaultRequestCredentialUse(
      int tab_id,
      const std::string& handle,
      const std::string& origin) override {
    for (const auto& credential : vault_credentials) {
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

  bool VaultFillCredential(int tab_id,
                           const std::string& grant_handle,
                           ui::AXNodeID ax_id) override {
    ++vault_fill_count;
    last_vault_grant_handle = grant_handle;
    last_vault_ax_id = ax_id;
    return true;
  }

  bool VaultFillTotp(int tab_id,
                     const std::string& grant_handle,
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
    session_ = std::make_unique<MahoMcpSession>(getuid(), lease_registry_.get());
    auto responses = session_->ProcessData(
        R"({"jsonrpc":"2.0","method":"initialize","id":1,)"
        R"("params":{"protocolVersion":"2025-03-26",)"
        R"("clientInfo":{"name":"t","version":"0.1.0"}}})"
        "\n");
    ASSERT_EQ(responses.size(), 1u);
    ASSERT_EQ(session_->state(), MahoMcpSession::State::kActive);
    // Todo 6: browser_type is a lease-gated mutator. Hold a lease on the
    // resolving delegate's active tab (7) so these credential-guard tests reach
    // the field classifier that they actually exercise.
    auto lease = session_->ProcessData(
        R"({"jsonrpc":"2.0","method":"tools/call","id":2,)"
        R"("params":{"name":"browser_acquire_lease",)"
        R"("arguments":{"tab_id":7}}})"
        "\n");
    ASSERT_EQ(lease.size(), 1u);
  }

  void TearDown() override {
    session_.reset();
    MahoMcpSession::SetBrowserDelegate(nullptr);
    delegate_.reset();
  }

  base::Value CallType(int ref, const std::string& text, int id) {
    auto responses = session_->ProcessData(base::StringPrintf(
        R"({"jsonrpc":"2.0","method":"tools/call","id":%d,)"
        R"("params":{"name":"browser_type","arguments":)"
        R"({"ref":%d,"text":%s}}})"
        "\n",
        id, ref, base::GetQuotedJSONString(text).c_str()));
    EXPECT_EQ(responses.size(), 1u);
    std::optional<base::Value> parsed =
        responses.empty() ? std::nullopt
                          : base::JSONReader::Read(responses[0],
                                                   base::JSON_PARSE_RFC);
    return parsed.has_value() ? std::move(*parsed) : base::Value();
  }

  base::Value CallTool(const std::string& name,
                       const std::string& arguments,
                       int id) {
    auto responses = session_->ProcessData(base::StringPrintf(
        R"({"jsonrpc":"2.0","method":"tools/call","id":%d,)"
        R"("params":{"name":"%s","arguments":%s}})"
        "\n",
        id, name.c_str(), arguments.c_str()));
    EXPECT_EQ(responses.size(), 1u);
    std::optional<base::Value> parsed =
        responses.empty() ? std::nullopt
                          : base::JSONReader::Read(responses[0],
                                                   base::JSON_PARSE_RFC);
    return parsed.has_value() ? std::move(*parsed) : base::Value();
  }

  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<MahoMcpLeaseRegistry> lease_registry_;
  std::unique_ptr<TypeGuardFakeDelegate> delegate_;
  std::unique_ptr<MahoMcpSession> session_;
};

TEST_F(MahoMcpSessionTypeGuardTest, OrdinaryTextFieldTypesThroughWithoutEcho) {
  MahoMcpFieldMetadata ordinary;
  ordinary.classified = true;
  ordinary.accessible_name = "search";
  delegate_->field_metadata[101] = ordinary;
  session_->ref_table()[1] = 101;

  base::Value resp = CallType(1, "hello world", 10);

  const auto* result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_EQ(delegate_->type_call_count, 1);
  EXPECT_EQ(delegate_->last_typed_text, "hello world");
  // Response must confirm the action without echoing the typed text back.
  EXPECT_FALSE(result->FindString("text"));
  EXPECT_EQ(resp.DebugString().find("hello world"), std::string::npos);
}

TEST_F(MahoMcpSessionTypeGuardTest,
       CredentialFieldTypesThroughWhenAllowCredentialsSet) {
  MahoMcpFieldMetadata secret;
  secret.classified = true;
  secret.is_protected = true;
  secret.accessible_name = "password";
  delegate_->field_metadata[202] = secret;
  session_->ref_table()[2] = 202;

  base::Value resp = CallTool(
      "browser_type",
      R"({"ref":2,"text":"admin1234","allow_credentials":true})", 11);

  const auto* result = resp.GetDict().FindDict("result");
  ASSERT_TRUE(result) << resp.DebugString();
  EXPECT_EQ(delegate_->type_call_count, 1);
  EXPECT_EQ(delegate_->last_typed_text, "admin1234");
}

TEST_F(MahoMcpSessionTypeGuardTest,
       VaultCredentialToolsReturnHandlesAndFillWithoutEchoingSecret) {
  MahoMcpBrowserDelegate::VaultCredentialSummary credential;
  credential.handle = "vh_sentinel_handle";
  credential.username_hint = "tester@example.com";
  credential.origin = "https://example.com";
  delegate_->vault_credentials.push_back(credential);
  delegate_->grant_by_handle[credential.handle] = "vg_sentinel_grant";
  session_->ref_table()[9] = 909;

  base::Value list = CallTool(
      "vault_list_credentials_for_active_page", R"({"tab_id":7})", 20);
  const auto* list_result = list.GetDict().FindDict("result");
  ASSERT_TRUE(list_result) << list.DebugString();
  const auto* items = list_result->FindList("items");
  ASSERT_TRUE(items);
  ASSERT_EQ(items->size(), 1u);
  EXPECT_EQ(*items->front().GetDict().FindString("handle"),
            "vh_sentinel_handle");
  EXPECT_FALSE(items->front().GetDict().FindString("password"));
  EXPECT_FALSE(items->front().GetDict().FindString("secret"));
  EXPECT_FALSE(items->front().GetDict().FindString("item_id"));
  EXPECT_EQ(list.DebugString().find(kSentinel), std::string::npos);

  base::Value grant = CallTool(
      "vault_request_credential_use",
      R"({"tab_id":7,"handle":"vh_sentinel_handle","origin":"https://example.com"})",
      21);
  const auto* grant_result = grant.GetDict().FindDict("result");
  ASSERT_TRUE(grant_result);
  EXPECT_EQ(*grant_result->FindString("grant_handle"), "vg_sentinel_grant");
  EXPECT_EQ(grant.DebugString().find(kSentinel), std::string::npos);
  EXPECT_FALSE(grant_result->FindString("password"));

  base::Value fill = CallTool(
      "vault_fill_credential",
      R"({"tab_id":7,"grant_handle":"vg_sentinel_grant","ref":9})", 22);
  const auto* fill_result = fill.GetDict().FindDict("result");
  ASSERT_TRUE(fill_result);
  EXPECT_TRUE(fill_result->FindBool("filled").value());
  EXPECT_EQ(delegate_->vault_fill_count, 1);
  EXPECT_EQ(delegate_->type_call_count, 0);
  EXPECT_EQ(delegate_->last_vault_ax_id, 909);
  EXPECT_EQ(delegate_->last_vault_grant_handle, "vg_sentinel_grant");
  EXPECT_FALSE(fill_result->FindString("grant_handle"));
  EXPECT_FALSE(fill_result->FindString("password"));
  EXPECT_EQ(fill.DebugString().find(kSentinel), std::string::npos);
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
  session_->ref_table()[6] = 606;  // ax_id 606 absent from field_metadata

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

}  // namespace
}  // namespace maho
