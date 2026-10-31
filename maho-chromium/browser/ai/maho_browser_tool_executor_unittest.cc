#include "maho/browser/ai/maho_browser_tool_executor.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/json/json_reader.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "base/values.h"
#include "maho/browser/ai/maho_browser_action_contract.h"
#include "maho/browser/ai/maho_ai_runtime_adapter.h"
#include "maho/browser/ai/maho_capability_broker.h"
#include "maho/browser/ai/maho_capability_principal.h"
#include "maho/browser/ai/maho_page_adapter_registry.h"
#include "maho/browser/mcp/maho_mcp_lease_registry.h"
#include "maho/browser/mcp/maho_mcp_session.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_node_id_forward.h"
#include "url/gurl.h"

namespace {

using Authorization = MahoBrowserToolExecutor::BrowserActionAuthorization;
using ApprovalDecision = MahoBrowserToolExecutor::BrowserActionApprovalDecision;

ApprovalDecision ApproveAction(const Authorization&) {
  ApprovalDecision decision;
  decision.approved = true;
  return decision;
}

class FakeBrowserActionDelegate : public maho::MahoMcpBrowserDelegate {
 public:
  maho::ai::MailAuthorizationContext mail_authorization_context{
      true, true, false, true, maho::ai::MailGlobalPolicy::kPrompt, 1};
  int mail_approval_count = 0;
  int credential_typing_approval_count = 0;
  bool credential_typing_approval_result = true;
  bool defer_mail_reads = false;
  MailReadCallback pending_mail_read;

  maho::ai::MailAuthorizationContext GetMailAuthorizationContext() override {
    return mail_authorization_context;
  }

  bool ConfirmMailToolApproval(std::string_view,
                               std::string_view) override {
    ++mail_approval_count;
    return true;
  }

  bool ConfirmCredentialTypingApproval(
      std::string_view,
      const maho::ResolvedMahoMcpTarget&) override {
    ++credential_typing_approval_count;
    return credential_typing_approval_result;
  }

  int active_tab_id = 7;
  int64_t target_generation = 1;
  // Moves the target out from under the request at the pre-dispatch
  // revalidation seam. Browser actions raise no approval ask any more, so this
  // is the hook a fail-closed test has to use.
  bool advance_generation_on_revalidate = false;
  std::string active_url = "https://example.com/page";
  std::unordered_map<int, ui::AXNodeID> snapshot_refs;
  std::unordered_map<ui::AXNodeID, maho::MahoMcpFieldMetadata> field_metadata;
  int click_count = 0;
  int type_count = 0;
  int select_count = 0;
  int scroll_count = 0;
  int hover_count = 0;
  int key_press_count = 0;
  int mail_list_accounts_count = 0;
  int navigate_count = 0;
  std::string last_typed_text;
  std::string last_navigate_url;
  std::string last_selected_value;
  std::string last_scroll_direction;
  int last_scroll_pixels = 0;
  std::optional<ui::AXNodeID> last_scroll_ax_id;
  ui::AXNodeID last_hover_ax_id = 0;
  std::string last_key;
  std::vector<std::string> last_modifiers;

  maho::MahoMcpTargetResolution ResolveTabTarget(
      int requested_tab_id) override {
    maho::MahoMcpTargetResolution resolution;
    if (requested_tab_id != 0 && requested_tab_id != active_tab_id) {
      resolution.error = maho::MahoMcpTargetError::kTabNotFound;
      return resolution;
    }
    resolution.target.valid = true;
    resolution.target.tab_id = active_tab_id;
    resolution.target.browser_id = 1;
    resolution.target.generation = target_generation;
    return resolution;
  }

  bool RevalidateTarget(
      const maho::ResolvedMahoMcpTarget& target) override {
    if (advance_generation_on_revalidate) {
      ++target_generation;
    }
    return target.valid && target.tab_id == active_tab_id &&
           target.generation == target_generation;
  }

  std::vector<maho::MahoMcpSession::TabInfo> GetTabList() override {
    maho::MahoMcpSession::TabInfo tab;
    tab.id = active_tab_id;
    tab.title = "Example";
    tab.url = active_url;
    tab.is_active = true;
    return {tab};
  }

  std::vector<maho::MahoMcpSession::ConsoleMessage> GetConsoleMessages(
      int tab_id) override {
    return {};
  }

  std::vector<maho::MahoMcpSession::NavigationEvent> GetNavigationEvents(
      int tab_id,
      int64_t since_ms) override {
    return {};
  }

  std::string GetPageText(int tab_id) override { return {}; }

  base::Value GetAccessibilitySnapshot(
      int tab_id,
      maho::MahoMcpSession::RefTable* out_refs) override {
    if (out_refs) {
      *out_refs = snapshot_refs;
    }
    base::DictValue snapshot;
    snapshot.Set("role", "WebArea");
    snapshot.Set("name", "Example page");
    return base::Value(std::move(snapshot));
  }

  PageContentResult GetPageContent(int tab_id) override { return {}; }
  PageContextResult GetPageContext(int tab_id) override { return {}; }
  SearchResult SearchInPage(int tab_id, const std::string& query) override {
    return {};
  }

  QuerySelectorResult QuerySelector(int tab_id,
                                    const std::string& selector) override {
    QuerySelectorResult result;
    result.ref_id = "ref_0";
    result.tag = "button";
    return result;
  }

  int get_element_text_count = 0;
  std::string GetElementText(int tab_id, const std::string& ref_id) override {
    ++get_element_text_count;
    return "element text";
  }

  std::string GetElementAttribute(int tab_id,
                                  const std::string& ref_id,
                                  const std::string& attribute) override {
    return "attribute value";
  }

  bool WaitForSelector(int tab_id,
                       const std::string& selector,
                       int timeout_ms) override {
    return true;
  }

  int CreateNewTab(const GURL& url) override { return 0; }
  bool CloseTab(int tab_id) override { return false; }
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
  int capture_full_page_count = 0;
  std::string screenshot_b64 = "fake_screenshot_png_base64_data";
  void CaptureFullPagePngBase64(
      int tab_id,
      base::OnceCallback<void(std::string,
                              std::optional<maho::MahoMcpCaptureMetrics>)>
          callback) override {
    ++capture_full_page_count;
    std::move(callback).Run(screenshot_b64, std::nullopt);
  }
  void CaptureElementPngBase64(
      int tab_id,
      ui::AXNodeID ax_id,
      base::OnceCallback<void(std::string)> callback) override {
    std::move(callback).Run({});
  }

  bool Scroll(int tab_id,
              const std::string& direction,
              int pixels,
              std::optional<ui::AXNodeID> ax_id) override {
    ++scroll_count;
    last_scroll_direction = direction;
    last_scroll_pixels = pixels;
    last_scroll_ax_id = ax_id;
    return true;
  }

  bool Click(int tab_id, ui::AXNodeID ax_id) override {
    ++click_count;
    return true;
  }

  bool Type(int tab_id, ui::AXNodeID ax_id, const std::string& text) override {
    ++type_count;
    last_typed_text = text;
    return true;
  }

  bool Select(int tab_id,
              ui::AXNodeID ax_id,
              const std::string& value) override {
    ++select_count;
    last_selected_value = value;
    return true;
  }

  bool Hover(int tab_id, ui::AXNodeID ax_id) override {
    ++hover_count;
    last_hover_ax_id = ax_id;
    return true;
  }

  bool KeyPress(int tab_id,
                const std::string& key,
                const std::vector<std::string>& modifiers) override {
    ++key_press_count;
    last_key = key;
    last_modifiers = modifiers;
    return true;
  }

  int file_upload_count = 0;
  std::string last_upload_path;
  int last_upload_tab_id = 0;
  bool upload_should_succeed = true;
  bool SelectFileForPendingChooser(int tab_id,
                                   const std::string& path) override {
    ++file_upload_count;
    last_upload_path = path;
    last_upload_tab_id = tab_id;
    return upload_should_succeed;
  }
  void SelectFileForPendingChooserAsync(
      int tab_id,
      const std::string& path,
      base::OnceCallback<void(bool)> callback) override {
    std::move(callback).Run(SelectFileForPendingChooser(tab_id, path));
  }

  int file_input_count = 0;
  std::string last_file_input_css;
  std::string last_file_input_path;
  int last_file_input_tab_id = 0;
  bool file_input_should_succeed = true;
  bool SelectFileForInput(int tab_id,
                          const std::string& css,
                          const std::string& path) override {
    ++file_input_count;
    last_file_input_tab_id = tab_id;
    last_file_input_css = css;
    last_file_input_path = path;
    return file_input_should_succeed;
  }
  void SelectFileForInputAsync(
      int tab_id,
      const std::string& css,
      const std::string& path,
      base::OnceCallback<void(bool)> callback) override {
    std::move(callback).Run(SelectFileForInput(tab_id, css, path));
  }

  int go_back_count = 0;
  int last_go_back_tab_id = 0;
  bool go_back_should_succeed = true;
  bool GoBack(int tab_id) override {
    ++go_back_count;
    last_go_back_tab_id = tab_id;
    return go_back_should_succeed;
  }

  bool ActivateTab(int tab_id) override { return true; }

  bool Navigate(int tab_id, const GURL& url) override {
    ++navigate_count;
    last_navigate_url = url.spec();
    active_url = url.spec();
    return true;
  }

  bool SetViewportSize(int tab_id, int width, int height) override {
    return true;
  }

  void StartNetworkCapture(const std::string& capture_id,
                           int tab_id,
                           const maho::ResolvedMahoMcpTarget& target,
                           StartNetworkCaptureCallback callback) override {
    StartNetworkCaptureResult result;
    std::move(callback).Run(result);
  }

  void StopNetworkCapture(const std::string& capture_id,
                          const maho::ResolvedMahoMcpTarget& target,
                          StopNetworkCaptureCallback callback) override {
    std::move(callback).Run(StopNetworkCaptureResult{});
  }

  void CancelNetworkCapture(const std::string& capture_id) override {}

  void MailListAccounts(MailReadCallback callback) override {
    ++mail_list_accounts_count;
    if (defer_mail_reads) {
      pending_mail_read = std::move(callback);
      return;
    }
    std::move(callback).Run(true, R"([{"id":"account-1"}])");
  }

  void CompleteMailRead(bool ok = true,
                        std::string result = R"([{"id":"account-1"}])") {
    ASSERT_TRUE(pending_mail_read);
    std::move(pending_mail_read).Run(ok, std::move(result));
  }

  int mail_send_count = 0;
  int mail_save_draft_count = 0;
  int mail_update_draft_count = 0;
  std::string last_write_request_json;
  std::string last_update_draft_id;
  bool last_write_already_authorized = false;

  void MailSendEmail(const std::string& request_json,
                     bool already_authorized,
                     MailReadCallback callback) override {
    ++mail_send_count;
    last_write_request_json = request_json;
    last_write_already_authorized = already_authorized;
    std::move(callback).Run(true, R"({"queued":true})");
  }

  void MailSaveDraft(const std::string& request_json,
                     bool already_authorized,
                     MailReadCallback callback) override {
    ++mail_save_draft_count;
    last_write_request_json = request_json;
    last_write_already_authorized = already_authorized;
    std::move(callback).Run(true, R"({"draft_id":"d1"})");
  }

  void MailUpdateDraft(const std::string& draft_id,
                       const std::string& request_json,
                       bool already_authorized,
                       MailReadCallback callback) override {
    ++mail_update_draft_count;
    last_update_draft_id = draft_id;
    last_write_request_json = request_json;
    last_write_already_authorized = already_authorized;
    std::move(callback).Run(true, R"({"updated":true})");
  }

  maho::MahoMcpFieldMetadata GetFieldMetadata(
      int tab_id,
      ui::AXNodeID ax_id) override {
    auto it = field_metadata.find(ax_id);
    return it == field_metadata.end() ? maho::MahoMcpFieldMetadata()
                                      : it->second;
  }
};

class MahoBrowserToolExecutorTest : public testing::Test {
 protected:
  static bool IsLeaseRequiredActionTool(const std::string& tool_name) {
    static constexpr auto kTools = std::to_array<std::string_view>({
        "browser_click",        "browser_type",
        "browser_navigate",     "browser_history_back",
        "browser_scroll",       "browser_key_press",
        "browser_select",       "browser_hover",
        "browser_file_upload_select",
        "browser_visual_click", "browser_act_and_observe",
    });
    for (std::string_view name : kTools) {
      if (name == tool_name) {
        return true;
      }
    }
    return false;
  }

  MahoBrowserToolExecutor MakeExecutor(
      MahoBrowserToolExecutor::BrowserActionApprovalCallback approval_callback =
          MahoBrowserToolExecutor::BrowserActionApprovalCallback(),
      maho::MahoMcpLeaseRegistry* lease_registry = nullptr,
      std::string lease_holder_id = "agent-session",
      const MahoAiRuntimeConfig* runtime_config = nullptr) {
    if (!lease_registry) {
      lease_registry = &lease_registry_;
    }
    return MahoBrowserToolExecutor(
        nullptr, true, base::BindRepeating([] { return true; }),
        std::move(approval_callback), &delegate_, lease_registry,
        std::move(lease_holder_id), runtime_config);
  }

  base::DictValue Execute(MahoBrowserToolExecutor& executor,
                          const std::string& tool_name,
                          base::DictValue arguments = base::DictValue()) {
    // Fixture default: lease-required browser actions carry the fixture's active
    // tab (7), so each test exercises approval/lease/fallback semantics rather
    // than tab resolution. Read-only and mail tools stay untouched. The
    // tab-binding gate itself is covered by
    // LeaseRequiredActionWithoutTabIdFailsClosed, which calls executor.Execute
    // directly and omits the binding.
    if (IsLeaseRequiredActionTool(tool_name) &&
        !arguments.FindInt("tab_id").has_value()) {
      arguments.Set("tab_id", delegate_.active_tab_id);
    }
    bool fired = false;
    base::DictValue result;
    executor.Execute(
        tool_name, arguments,
        base::BindLambdaForTesting([&](base::DictValue tool_result) {
          fired = true;
          result = std::move(tool_result);
        }));
    EXPECT_TRUE(fired);
    return result;
  }

  base::OnceClosure ExecuteDeferredMailRead(
      MahoBrowserToolExecutor& executor,
      base::DictValue* result,
      bool* fired) {
    delegate_.defer_mail_reads = true;
    executor.Execute(
        "mail_list_accounts", base::DictValue(),
        base::BindLambdaForTesting(
            [result, fired](base::DictValue tool_result) {
              *fired = true;
              *result = std::move(tool_result);
            }));
    EXPECT_FALSE(*fired);
    EXPECT_TRUE(delegate_.pending_mail_read);
    return base::BindLambdaForTesting([this] { delegate_.CompleteMailRead(); });
  }

  void PopulateRef(MahoBrowserToolExecutor& executor,
                   int ref,
                   ui::AXNodeID ax_id) {
    delegate_.snapshot_refs[ref] = ax_id;
    Execute(executor, "browser_accessibility_snapshot");
  }

  base::test::TaskEnvironment task_environment_;
  maho::MahoMcpLeaseRegistry lease_registry_;
  FakeBrowserActionDelegate delegate_;
};

// Tab-binding contract: a lease-required browser action without an explicit
// tab_id fails closed instead of acting on whichever tab is focused. Reads keep
// the active-tab fallback (PopulateRef above relies on it).
TEST_F(MahoBrowserToolExecutorTest, LeaseRequiredActionWithoutTabIdFailsClosed) {
  auto executor = MakeExecutor();
  PopulateRef(executor, 1, 101);

  base::DictValue arguments;
  arguments.Set("ref", 1);
  base::DictValue result;
  bool fired = false;
  executor.Execute(
      "browser_click", arguments,
      base::BindLambdaForTesting([&](base::DictValue tool_result) {
        fired = true;
        result = std::move(tool_result);
      }));
  ASSERT_TRUE(fired);
  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  const std::string* error = result.FindString("error");
  ASSERT_TRUE(error);
  EXPECT_NE(error->find("Tab binding required"), std::string::npos);
  EXPECT_EQ(delegate_.click_count, 0);
  EXPECT_FALSE(lease_registry_.IsHeldBy(7, "agent-session"));

  // The same call with an explicit binding still reaches the click path.
  base::DictValue bound;
  bound.Set("ref", 1);
  bound.Set("tab_id", 7);
  base::DictValue bound_result =
      Execute(executor, "browser_click", std::move(bound));
  EXPECT_TRUE(bound_result.FindBool("ok").value_or(false));
  EXPECT_EQ(delegate_.click_count, 1);
}

// Driving the browser needs no approval authorization at all: a sensitive
// in-browser action runs with no approval callback wired. Only an action that
// leaves the browser still fails closed when authorization is missing.
TEST_F(MahoBrowserToolExecutorTest, SensitiveActionRunsWithoutAuthorization) {
  auto executor = MakeExecutor();
  PopulateRef(executor, 1, 101);

  base::DictValue arguments;
  arguments.Set("ref", 1);
  base::DictValue result = Execute(executor, "browser_click", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  EXPECT_EQ(delegate_.click_count, 1);

  base::DictValue upload_arguments;
  upload_arguments.Set("ref", 1);
  base::DictValue upload_result = Execute(
      executor, "browser_file_upload_select", std::move(upload_arguments));

  EXPECT_FALSE(upload_result.FindBool("ok").value_or(true));
  EXPECT_EQ(upload_result.FindInt("error_code").value_or(0), -32008);
}

// The denial path keeps its invariants where the ask still exists: nothing is
// dispatched and no lease is taken on the way out.
TEST_F(MahoBrowserToolExecutorTest, ApprovalDenialBlocksLeaseAndDispatch) {
  auto executor = MakeExecutor(base::BindRepeating(
      [](const Authorization& metadata) {
        ApprovalDecision decision;
        decision.error = "denied by test approval";
        return decision;
      }));
  PopulateRef(executor, 1, 101);

  base::DictValue arguments;
  arguments.Set("ref", 1);
  base::DictValue result =
      Execute(executor, "browser_file_upload_select", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_NE(result.FindString("error")->find("denied by test approval"),
            std::string::npos);
  EXPECT_FALSE(lease_registry_.IsHeldBy(7, "agent-session"));
  EXPECT_EQ(delegate_.click_count, 0);
}

TEST_F(MahoBrowserToolExecutorTest,
       TargetGenerationChangeBeforeDispatchFailsClosed) {
  auto executor = MakeExecutor();
  PopulateRef(executor, 1, 101);
  delegate_.advance_generation_on_revalidate = true;

  base::DictValue arguments;
  arguments.Set("ref", 1);
  base::DictValue result =
      Execute(executor, "browser_click", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  const std::string* error = result.FindString("error");
  ASSERT_TRUE(error);
  EXPECT_NE(error->find("target changed"), std::string::npos);
  EXPECT_EQ(delegate_.click_count, 0);
}

TEST_F(MahoBrowserToolExecutorTest, ApprovedClickDispatchesViaMcpHandler) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  PopulateRef(executor, 1, 101);

  base::DictValue arguments;
  arguments.Set("ref", 1);
  base::DictValue result = Execute(executor, "browser_click", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value());
  EXPECT_TRUE(result.FindBool("clicked").value());
  EXPECT_EQ(result.FindInt("ref").value(), 1);
  EXPECT_TRUE(lease_registry_.IsHeldBy(7, "agent-session"));
  EXPECT_EQ(delegate_.click_count, 1);
}

TEST_F(MahoBrowserToolExecutorTest, ExternalLeaseContentionForceSteals) {
  auto external_lease = lease_registry_.Acquire(
      7, "external-session", base::Seconds(60), false);
  ASSERT_TRUE(external_lease.ok);
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  PopulateRef(executor, 1, 101);

  base::DictValue arguments;
  arguments.Set("ref", 1);
  base::DictValue result = Execute(executor, "browser_click", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  EXPECT_TRUE(lease_registry_.IsHeldBy(7, "agent-session"));
  EXPECT_FALSE(lease_registry_.IsHeldBy(7, "external-session"));
  EXPECT_EQ(delegate_.click_count, 1);
}

TEST_F(MahoBrowserToolExecutorTest, LeaseRequiredActionFailsWithoutLeaseHolder) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction),
                               &lease_registry_, std::string());
  PopulateRef(executor, 1, 101);

  base::DictValue arguments;
  arguments.Set("ref", 1);
  base::DictValue result = Execute(executor, "browser_click", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_NE(result.FindString("error")->find("lease"),
            std::string::npos);
  EXPECT_EQ(delegate_.click_count, 0);
}

TEST_F(MahoBrowserToolExecutorTest, UnknownRefUsesMcpCompatibleError) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  PopulateRef(executor, 1, 101);

  base::DictValue arguments;
  arguments.Set("ref", 404);
  base::DictValue result = Execute(executor, "browser_click", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(result.FindInt("error_code").value(), -32000);
  EXPECT_NE(result.FindString("error")->find("Unknown ref"), std::string::npos);
  EXPECT_EQ(delegate_.click_count, 0);
}

class MahoBrowserToolExecutorFallbackTest
    : public MahoBrowserToolExecutorTest {};

TEST_F(MahoBrowserToolExecutorFallbackTest,
       DirectRefActionRequiresAccessibilitySnapshotState) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  delegate_.snapshot_refs[1] = 101;

  base::DictValue arguments;
  arguments.Set("ref", 1);
  base::DictValue result = Execute(executor, "browser_click", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_NE(result.FindString("error")->find("accessibility_snapshot"),
            std::string::npos);
  const base::DictValue* fallback = result.FindDict("fallback_decision");
  ASSERT_TRUE(fallback);
  EXPECT_EQ(*fallback->FindString("selected_tier"), "accessibility_snapshot");
  EXPECT_EQ(*fallback->FindString("reason"),
            "accessibility_snapshot_required");
  EXPECT_EQ(*fallback->FindString("safety_state"), "blocked");
  EXPECT_EQ(delegate_.click_count, 0);
}

TEST_F(MahoBrowserToolExecutorFallbackTest,
       SnapshotSelectedTierStillRunsApprovalAndLease) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  PopulateRef(executor, 1, 101);

  base::DictValue arguments;
  arguments.Set("ref", 1);
  base::DictValue result = Execute(executor, "browser_click", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value());
  const base::DictValue* fallback = result.FindDict("fallback_decision");
  ASSERT_TRUE(fallback);
  EXPECT_EQ(*fallback->FindString("attempted_tier"), "dom_ref_locator");
  EXPECT_EQ(*fallback->FindString("selected_tier"),
            "accessibility_snapshot");
  EXPECT_TRUE(lease_registry_.IsHeldBy(7, "agent-session"));
  EXPECT_EQ(delegate_.click_count, 1);
}

TEST_F(MahoBrowserToolExecutorFallbackTest,
       ScreenshotCuaRequiresExplicitApprovalBeforeDispatch) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  PopulateRef(executor, 1, 101);

  base::DictValue arguments;
  arguments.Set("ref", 1);
  arguments.Set("fallback_tier", "screenshot_cua");
  base::DictValue result = Execute(executor, "browser_click", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_NE(result.FindString("error")->find("explicit approval"),
            std::string::npos);
  const base::DictValue* fallback = result.FindDict("fallback_decision");
  ASSERT_TRUE(fallback);
  EXPECT_EQ(*fallback->FindString("selected_tier"), "screenshot_cua");
  EXPECT_EQ(*fallback->FindString("reason"),
            "screenshot_cua_approval_required");
  EXPECT_EQ(*fallback->FindString("safety_state"),
            "escalation_approval_required");
  EXPECT_EQ(delegate_.click_count, 0);
}

// A denied approval cannot veto ordinary browser driving: the snapshot tier
// still dispatches, because clicking never asks in the first place. The tier
// that genuinely leaves the browser — screenshot_cua, which drives OS-level
// input — stays refused either way, which is what the denial invariant is
// actually protecting.
TEST_F(MahoBrowserToolExecutorFallbackTest,
       ApprovalDenialOnlyBlocksTheEscalatedFallbackTier) {
  auto executor = MakeExecutor(base::BindRepeating(
      [](const Authorization& metadata) {
        ApprovalDecision decision;
        decision.error = "fallback selected tier denied";
        return decision;
      }));
  PopulateRef(executor, 1, 101);

  base::DictValue arguments;
  arguments.Set("ref", 1);
  base::DictValue result = Execute(executor, "browser_click", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  const base::DictValue* fallback = result.FindDict("fallback_decision");
  ASSERT_TRUE(fallback);
  EXPECT_EQ(*fallback->FindString("selected_tier"),
            "accessibility_snapshot");
  EXPECT_TRUE(lease_registry_.IsHeldBy(7, "agent-session"));
  EXPECT_EQ(delegate_.click_count, 1);

  base::DictValue escalated;
  escalated.Set("ref", 1);
  escalated.Set("fallback_tier", "screenshot_cua");
  base::DictValue escalated_result =
      Execute(executor, "browser_click", std::move(escalated));

  EXPECT_FALSE(escalated_result.FindBool("ok").value_or(true));
  EXPECT_EQ(delegate_.click_count, 1);
}

TEST_F(MahoBrowserToolExecutorTest, CredentialFieldRejectsWithoutTextEcho) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  PopulateRef(executor, 2, 202);
  maho::MahoMcpFieldMetadata metadata;
  metadata.classified = true;
  metadata.is_protected = true;
  delegate_.field_metadata[202] = metadata;

  base::DictValue arguments;
  arguments.Set("ref", 2);
  arguments.Set("text", "S3NTINEL-secret-password");
  base::DictValue result = Execute(executor, "browser_type", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(delegate_.type_count, 0);
  EXPECT_EQ(result.DebugString().find("S3NTINEL"), std::string::npos);
}

TEST_F(MahoBrowserToolExecutorTest,
       CredentialFieldRemainsVaultOnlyAfterExistingAgentApprovalWithoutSecondPrompt) {
  int agent_approval_count = 0;
  auto executor = MakeExecutor(base::BindRepeating(
      [](int* approval_count, const Authorization& metadata) {
        ++*approval_count;
        EXPECT_EQ(metadata.kind, "credential_typing");
        EXPECT_EQ(metadata.sensitivity, "credential");
        EXPECT_TRUE(metadata.requires_approval);
        ApprovalDecision decision;
        decision.approved = true;
        return decision;
      },
      &agent_approval_count));
  PopulateRef(executor, 2, 202);
  maho::MahoMcpFieldMetadata metadata;
  metadata.classified = true;
  metadata.is_protected = true;
  delegate_.field_metadata[202] = metadata;

  base::DictValue arguments;
  arguments.Set("ref", 2);
  arguments.Set("text", "S3NTINEL-approved-password");
  arguments.Set("allow_credentials", true);
  base::DictValue result = Execute(executor, "browser_type", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(agent_approval_count, 1);
  EXPECT_EQ(delegate_.credential_typing_approval_count, 0);
  EXPECT_EQ(delegate_.type_count, 0);
  EXPECT_EQ(result.DebugString().find("S3NTINEL"), std::string::npos);
}

TEST_F(MahoBrowserToolExecutorTest,
       CredentialFieldNativeApprovalAllowsCredentialTyping) {
  auto executor = MakeExecutor();
  PopulateRef(executor, 2, 202);
  maho::MahoMcpFieldMetadata metadata;
  metadata.classified = true;
  metadata.is_protected = true;
  delegate_.field_metadata[202] = metadata;

  base::DictValue arguments;
  arguments.Set("ref", 2);
  arguments.Set("text", "S3NTINEL-native-approved-password");
  arguments.Set("allow_credentials", true);
  base::DictValue result = Execute(executor, "browser_type", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  EXPECT_EQ(delegate_.credential_typing_approval_count, 1);
  EXPECT_EQ(delegate_.type_count, 1);
  EXPECT_EQ(result.DebugString().find("S3NTINEL"), std::string::npos);
}

TEST_F(MahoBrowserToolExecutorTest,
       CredentialFieldNativeApprovalDenialReturnsStableReason) {
  auto executor = MakeExecutor();
  PopulateRef(executor, 2, 202);
  delegate_.credential_typing_approval_result = false;
  maho::MahoMcpFieldMetadata metadata;
  metadata.classified = true;
  metadata.is_protected = true;
  delegate_.field_metadata[202] = metadata;

  base::DictValue arguments;
  arguments.Set("ref", 2);
  arguments.Set("text", "S3NTINEL-native-denied-password");
  arguments.Set("allow_credentials", true);
  base::DictValue result = Execute(executor, "browser_type", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(result.FindInt("error_code").value(), -32008);
  EXPECT_EQ(*result.FindString("reason_code"),
            maho::ai::kCredentialTypingDenied);
  EXPECT_EQ(delegate_.credential_typing_approval_count, 1);
  EXPECT_EQ(delegate_.type_count, 0);
  EXPECT_EQ(result.DebugString().find("S3NTINEL"), std::string::npos);
}

TEST(CredentialTypingAuthorizationTest, StableApprovalRequiredContract) {
  const auto requires_approval =
      maho::ai::AuthorizeCredentialTyping(true);
  EXPECT_EQ(requires_approval.action,
            maho::ai::CredentialTypingAuthorizationAction::kRequireApproval);
  EXPECT_EQ(requires_approval.reason_code,
            maho::ai::kCredentialTypingApprovalRequired);
  EXPECT_EQ(maho::ai::AuthorizeCredentialTyping(false).action,
            maho::ai::CredentialTypingAuthorizationAction::kAllow);
}

TEST_F(MahoBrowserToolExecutorTest, InvalidDirectionAndKeyFailBeforeDispatch) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));

  base::DictValue scroll_arguments;
  scroll_arguments.Set("direction", "diagonal");
  base::DictValue scroll_result =
      Execute(executor, "browser_scroll", std::move(scroll_arguments));
  EXPECT_FALSE(scroll_result.FindBool("ok").value_or(true));
  EXPECT_EQ(scroll_result.FindInt("error_code").value(), -32602);
  EXPECT_EQ(delegate_.scroll_count, 0);

  base::DictValue key_arguments;
  key_arguments.Set("key", "LaunchMissiles");
  base::DictValue key_result =
      Execute(executor, "browser_key_press", std::move(key_arguments));
  EXPECT_FALSE(key_result.FindBool("ok").value_or(true));
  EXPECT_EQ(key_result.FindInt("error_code").value(), -32602);
  EXPECT_EQ(delegate_.key_press_count, 0);
}

TEST_F(MahoBrowserToolExecutorTest, NavigateEmptyActiveOriginFailsClosed) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  delegate_.active_url = "about:blank";

  base::DictValue arguments;
  arguments.Set("url", "https://example.test/");
  base::DictValue result = Execute(executor, "browser_navigate", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_NE(result.FindString("error")->find("failed closed"),
            std::string::npos);
  EXPECT_EQ(delegate_.navigate_count, 0);
}

// Driving the browser is full access: navigating — cross-origin included —
// dispatches without any approval ask. Neither the contract, nor the
// controller identity, nor the user's final-confirm setting gates it; the
// second half runs the identical call with final_confirm on to pin that.
TEST_F(MahoBrowserToolExecutorTest,
       BrowserNavigationNeverAsksUnderAnyRuntimeConfig) {
  MahoAiRuntimeConfig final_confirm_off;
  final_confirm_off.final_confirm = false;
  std::optional<Authorization> captured;
  auto executor = MakeExecutor(
      base::BindRepeating(
          [](std::optional<Authorization>* out, const Authorization& metadata) {
            *out = metadata;
            return ApproveAction(metadata);
          },
          &captured),
      nullptr, "agent-session", &final_confirm_off);

  base::DictValue arguments;
  arguments.Set("url", "https://other.example/path");
  base::DictValue result = Execute(executor, "browser_navigate", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  EXPECT_FALSE(captured.has_value());
  EXPECT_EQ(delegate_.navigate_count, 1);

  MahoAiRuntimeConfig final_confirm_on;
  final_confirm_on.final_confirm = true;
  std::optional<Authorization> confirmed;
  auto confirming_executor = MakeExecutor(
      base::BindRepeating(
          [](std::optional<Authorization>* out, const Authorization& metadata) {
            *out = metadata;
            return ApproveAction(metadata);
          },
          &confirmed),
      nullptr, "agent-session", &final_confirm_on);

  base::DictValue confirmed_arguments;
  confirmed_arguments.Set("url", "https://other.example/path");
  base::DictValue confirmed_result = Execute(
      confirming_executor, "browser_navigate", std::move(confirmed_arguments));

  EXPECT_TRUE(confirmed_result.FindBool("ok").value_or(false));
  EXPECT_FALSE(confirmed.has_value());
  EXPECT_EQ(delegate_.navigate_count, 2);
}

TEST_F(MahoBrowserToolExecutorTest, HistoryBackDispatchesToDelegate) {
  auto executor = MakeExecutor();
  PopulateRef(executor, 1, 101);

  base::DictValue arguments;
  arguments.Set("tab_id", 7);
  base::DictValue result =
      Execute(executor, "browser_history_back", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  EXPECT_TRUE(result.FindBool("navigated").value_or(false));
  EXPECT_TRUE(result.FindBool("went_back").value_or(false));
  EXPECT_EQ(delegate_.go_back_count, 1);
  EXPECT_EQ(delegate_.last_go_back_tab_id, 7);

  // History navigation invalidates outstanding element references on the tab.
  base::DictValue click_args;
  click_args.Set("ref", 1);
  click_args.Set("tab_id", 7);
  base::DictValue click_result =
      Execute(executor, "browser_click", std::move(click_args));
  EXPECT_FALSE(click_result.FindBool("ok").value_or(true));
  EXPECT_EQ(click_result.FindInt("error_code").value_or(0),
            maho::kMahoMcpErrorStaleReference);
}

TEST_F(MahoBrowserToolExecutorTest, HistoryBackFailsWhenNoHistory) {
  auto executor = MakeExecutor();
  delegate_.go_back_should_succeed = false;

  base::DictValue arguments;
  arguments.Set("tab_id", 7);
  base::DictValue result =
      Execute(executor, "browser_history_back", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(result.FindInt("error_code").value_or(0), -32000);
  const std::string* error = result.FindString("error");
  ASSERT_TRUE(error);
  EXPECT_NE(error->find("no history back entry"), std::string::npos);
  EXPECT_EQ(delegate_.go_back_count, 1);
}

TEST_F(MahoBrowserToolExecutorTest, HistoryBackWithoutTabIdFailsClosed) {
  auto executor = MakeExecutor();

  base::DictValue arguments;
  base::DictValue result;
  bool fired = false;
  executor.Execute(
      "browser_history_back", arguments,
      base::BindLambdaForTesting([&](base::DictValue tool_result) {
        fired = true;
        result = std::move(tool_result);
      }));
  ASSERT_TRUE(fired);
  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  const std::string* error = result.FindString("error");
  ASSERT_TRUE(error);
  EXPECT_NE(error->find("Tab binding required"), std::string::npos);
  EXPECT_EQ(delegate_.go_back_count, 0);
}

TEST_F(MahoBrowserToolExecutorTest, HistoryBackFailsClosedWithoutActiveOrigin) {
  auto executor = MakeExecutor();
  delegate_.active_url = "about:blank";

  base::DictValue arguments;
  arguments.Set("tab_id", 7);
  base::DictValue result =
      Execute(executor, "browser_history_back", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  const std::string* error = result.FindString("error");
  ASSERT_TRUE(error);
  EXPECT_NE(error->find("failed closed"), std::string::npos);
  EXPECT_EQ(delegate_.go_back_count, 0);
}

TEST_F(MahoBrowserToolExecutorTest, HistoryBackNeedsNoApprovalPrompt) {
  // Driving the browser needs no approval prompt; browser_history_back runs
  // with no approval callback wired under the ActiveTabOrigin contract.
  auto executor = MakeExecutor();

  base::DictValue arguments;
  arguments.Set("tab_id", 7);
  base::DictValue result =
      Execute(executor, "browser_history_back", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  EXPECT_EQ(delegate_.go_back_count, 1);
}

// Leaving the browser still asks, and the ask carries typed metadata.
TEST_F(MahoBrowserToolExecutorTest,
       AuthorizationReceivesTypedMetadataForBoundaryAction) {
  std::optional<Authorization> captured;
  auto executor = MakeExecutor(base::BindRepeating(
      [](std::optional<Authorization>* out, const Authorization& metadata) {
        *out = metadata;
        return ApproveAction(metadata);
      },
      &captured));
  PopulateRef(executor, 1, 101);

  base::DictValue arguments;
  arguments.Set("ref", 1);
  arguments.Set("path", "/tmp/maho-upload.png");
  base::DictValue result =
      Execute(executor, "browser_file_upload_select", std::move(arguments));

  ASSERT_TRUE(captured.has_value()) << result.DebugString();
  EXPECT_EQ(captured->tool_name, "browser_file_upload_select");
  EXPECT_EQ(captured->kind, "action");
  EXPECT_EQ(captured->sensitivity, "sensitive");
  EXPECT_TRUE(captured->requires_approval);
  EXPECT_TRUE(captured->requires_lease);
  EXPECT_EQ(captured->active_origin, "https://example.com");
  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  EXPECT_EQ(delegate_.file_upload_count, 1);
  EXPECT_EQ(delegate_.last_upload_path, "/tmp/maho-upload.png");
  EXPECT_EQ(delegate_.last_upload_tab_id, 7);
  EXPECT_TRUE(result.FindBool("file_selected").value_or(false));
}

TEST_F(MahoBrowserToolExecutorTest, FileUploadFailsWhenDelegateRejects) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  PopulateRef(executor, 1, 101);
  delegate_.upload_should_succeed = false;

  base::DictValue arguments;
  arguments.Set("path", "/tmp/nonexistent.bin");
  base::DictValue result =
      Execute(executor, "browser_file_upload_select", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_NE(result.FindString("error")->find("file upload could not be dispatched"),
            std::string::npos);
  EXPECT_EQ(delegate_.file_upload_count, 1);
}

TEST_F(MahoBrowserToolExecutorTest, FileUploadSelectWithSelectorRoutesToInputAsync) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));

  base::DictValue arguments;
  arguments.Set("path", "/tmp/document.pdf");
  arguments.Set("selector", "input[type='file']");
  base::DictValue result =
      Execute(executor, "browser_file_upload_select", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  EXPECT_TRUE(result.FindBool("file_selected").value_or(false));
  EXPECT_EQ(delegate_.file_input_count, 1);
  EXPECT_EQ(delegate_.last_file_input_css, "input[type='file']");
  EXPECT_EQ(delegate_.last_file_input_path, "/tmp/document.pdf");
  EXPECT_EQ(delegate_.last_file_input_tab_id, 7);
  EXPECT_EQ(delegate_.file_upload_count, 0);
}

TEST_F(MahoBrowserToolExecutorTest, FileUploadSelectWithCssAliasRoutesToInputAsync) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));

  base::DictValue arguments;
  arguments.Set("path", "/tmp/avatar.png");
  arguments.Set("css", "#profile-photo-input");
  base::DictValue result =
      Execute(executor, "browser_file_upload_select", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  EXPECT_TRUE(result.FindBool("file_selected").value_or(false));
  EXPECT_EQ(delegate_.file_input_count, 1);
  EXPECT_EQ(delegate_.last_file_input_css, "#profile-photo-input");
  EXPECT_EQ(delegate_.last_file_input_path, "/tmp/avatar.png");
  EXPECT_EQ(delegate_.last_file_input_tab_id, 7);
  EXPECT_EQ(delegate_.file_upload_count, 0);
}

TEST_F(MahoBrowserToolExecutorTest, FileUploadSelectWithSelectorFailsWhenDelegateRejects) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  delegate_.file_input_should_succeed = false;

  base::DictValue arguments;
  arguments.Set("path", "/tmp/nonexistent.bin");
  arguments.Set("selector", "input[type='file']");
  base::DictValue result =
      Execute(executor, "browser_file_upload_select", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_NE(result.FindString("error")->find("file upload could not be dispatched"),
            std::string::npos);
  EXPECT_EQ(delegate_.file_input_count, 1);
  EXPECT_EQ(delegate_.file_upload_count, 0);
}

TEST_F(MahoBrowserToolExecutorTest, FileUploadSelectInvalidSelectorParams) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));

  base::DictValue empty_selector;
  empty_selector.Set("path", "/tmp/file.txt");
  empty_selector.Set("selector", "");
  base::DictValue empty_res =
      Execute(executor, "browser_file_upload_select", std::move(empty_selector));
  EXPECT_FALSE(empty_res.FindBool("ok").value_or(true));
  EXPECT_EQ(empty_res.FindInt("error_code").value_or(0), -32602);
  EXPECT_NE(empty_res.FindString("error")->find("selector cannot be empty"),
            std::string::npos);

  base::DictValue non_string_selector;
  non_string_selector.Set("path", "/tmp/file.txt");
  non_string_selector.Set("selector", 12345);
  base::DictValue non_string_res =
      Execute(executor, "browser_file_upload_select", std::move(non_string_selector));
  EXPECT_FALSE(non_string_res.FindBool("ok").value_or(true));
  EXPECT_EQ(non_string_res.FindInt("error_code").value_or(0), -32602);
  EXPECT_NE(non_string_res.FindString("error")->find("selector must be a string"),
            std::string::npos);

  EXPECT_EQ(delegate_.file_input_count, 0);
}

TEST_F(MahoBrowserToolExecutorTest, FileUploadSelectWithSelectorRequiresApproval) {
  // File upload crosses the browser boundary to the filesystem, so direct
  // selector inputs still enforce required typed approval.
  auto executor = MakeExecutor();

  base::DictValue arguments;
  arguments.Set("path", "/tmp/document.pdf");
  arguments.Set("selector", "input[type='file']");
  base::DictValue result =
      Execute(executor, "browser_file_upload_select", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(result.FindInt("error_code").value_or(0), -32008);
  const std::string* error = result.FindString("error");
  ASSERT_TRUE(error);
  EXPECT_NE(error->find("typed approval"), std::string::npos);
  EXPECT_EQ(delegate_.file_input_count, 0);
}

TEST_F(MahoBrowserToolExecutorTest, VaultToolIsNotDispatchable) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));

  base::DictValue result = Execute(executor, "vault_fill_credential");

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_NE(result.FindString("error")->find("Unknown browser tool"),
            std::string::npos);
}

TEST_F(MahoBrowserToolExecutorTest,
       MailListAccountsFailsClosedWithoutBrowser) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  delegate_.mail_authorization_context.feature_enabled = false;

  base::DictValue result = Execute(executor, "mail_list_accounts");

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  ASSERT_TRUE(result.FindString("error"));
  EXPECT_EQ(*result.FindString("reason_code"), "mail_feature_disabled");
  EXPECT_EQ(delegate_.mail_list_accounts_count, 0);
}

TEST_F(MahoBrowserToolExecutorTest, MailAuthorizationDecisionCoversExactToolTables) {
  struct GateCase {
    maho::ai::MailAuthorizationContext context;
    std::string_view read_reason;
    std::string_view write_reason;
  };
  const std::array gate_cases = {
      GateCase{{false, false, false, false,
                maho::ai::MailGlobalPolicy::kPrompt, 0},
               "mail_feature_disabled", "mail_feature_disabled"},
      GateCase{{true, false, true, false,
                maho::ai::MailGlobalPolicy::kPrompt, 1},
               "mail_helper_starting", "mail_helper_starting"},
      GateCase{{true, false, false, false,
                maho::ai::MailGlobalPolicy::kPrompt, 1},
               "mail_helper_unavailable", "mail_helper_unavailable"},
      GateCase{{true, true, false, false,
                maho::ai::MailGlobalPolicy::kAllow, 1},
               "mail_read_consent_required", "mail_typed_approval_required"},
      GateCase{{true, true, false, true,
                maho::ai::MailGlobalPolicy::kPrompt, 1},
               "mail_read_allowed", "mail_typed_approval_required"},
  };
  for (const GateCase& gate_case : gate_cases) {
    for (std::string_view tool : maho::ai::kMailReadTools) {
      EXPECT_EQ(maho::ai::ClassifyMailTool(tool),
                maho::ai::MailToolClass::kRead)
          << tool;
      EXPECT_EQ(maho::ai::AuthorizeMailTool(tool, gate_case.context).reason_code,
                gate_case.read_reason)
          << tool;
    }
    for (std::string_view tool : maho::ai::kMailWriteAccountTools) {
      EXPECT_EQ(maho::ai::ClassifyMailTool(tool),
                maho::ai::MailToolClass::kWriteAccount)
          << tool;
      EXPECT_EQ(maho::ai::AuthorizeMailTool(tool, gate_case.context).reason_code,
                gate_case.write_reason)
          << tool;
    }
  }
}

TEST_F(MahoBrowserToolExecutorTest,
       EveryDeniedMailToolStopsBeforeDelegateDispatch) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  const std::array denied_states = {
      maho::ai::MailAuthorizationContext{
          false, false, false, false, maho::ai::MailGlobalPolicy::kPrompt, 0},
      maho::ai::MailAuthorizationContext{
          true, false, true, false, maho::ai::MailGlobalPolicy::kPrompt, 1},
      maho::ai::MailAuthorizationContext{
          true, false, false, false, maho::ai::MailGlobalPolicy::kPrompt, 1},
  };
  for (const auto& state : denied_states) {
    delegate_.mail_authorization_context = state;
    for (std::string_view tool : maho::ai::kMailReadTools) {
      base::DictValue result = Execute(executor, std::string(tool));
      EXPECT_FALSE(result.FindBool("ok").value_or(true)) << tool;
      EXPECT_EQ(delegate_.mail_list_accounts_count, 0) << tool;
    }
    for (std::string_view tool : maho::ai::kMailWriteAccountTools) {
      base::DictValue result = Execute(executor, std::string(tool));
      EXPECT_FALSE(result.FindBool("ok").value_or(true)) << tool;
      EXPECT_EQ(delegate_.mail_send_count, 0) << tool;
      EXPECT_EQ(delegate_.mail_save_draft_count, 0) << tool;
      EXPECT_EQ(delegate_.mail_update_draft_count, 0) << tool;
    }
  }
  delegate_.mail_authorization_context = {
      true, true, false, false, maho::ai::MailGlobalPolicy::kAllow, 1};
  for (std::string_view tool : maho::ai::kMailReadTools) {
    base::DictValue result = Execute(executor, std::string(tool));
    EXPECT_EQ(*result.FindString("reason_code"),
              "mail_read_consent_required")
        << tool;
    EXPECT_EQ(delegate_.mail_list_accounts_count, 0) << tool;
  }
}

TEST_F(MahoBrowserToolExecutorTest, DeniedMailArgumentsAreRedacted) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  delegate_.mail_authorization_context.read_allowed = false;
  delegate_.mail_authorization_context.global_policy =
      maho::ai::MailGlobalPolicy::kAllow;
  base::DictValue arguments;
  arguments.Set("email_id", "S3NTINEL-mail-secret");
  base::DictValue result =
      Execute(executor, "mail_get_email", std::move(arguments));
  EXPECT_EQ(*result.FindString("reason_code"), "mail_read_consent_required");
  EXPECT_EQ(*result.FindString("arguments"), "[REDACTED]");
  EXPECT_EQ(result.DebugString().find("S3NTINEL"), std::string::npos);
}

TEST_F(MahoBrowserToolExecutorTest,
       InFlightMailReadFailsClosedAfterConsentRevocation) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  bool fired = false;
  base::DictValue result;
  base::OnceClosure release = ExecuteDeferredMailRead(executor, &result, &fired);

  delegate_.mail_authorization_context.read_allowed = false;
  std::move(release).Run();

  ASSERT_TRUE(fired);
  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(*result.FindString("reason_code"),
            "mail_read_consent_required");
  EXPECT_EQ(result.DebugString().find("account-1"), std::string::npos);
}

TEST_F(MahoBrowserToolExecutorTest,
       InFlightMailReadFailsClosedAfterFeatureDisable) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  bool fired = false;
  base::DictValue result;
  base::OnceClosure release = ExecuteDeferredMailRead(executor, &result, &fired);

  delegate_.mail_authorization_context.feature_enabled = false;
  std::move(release).Run();

  ASSERT_TRUE(fired);
  EXPECT_EQ(*result.FindString("reason_code"), "mail_feature_disabled");
  EXPECT_EQ(result.DebugString().find("account-1"), std::string::npos);
}

TEST_F(MahoBrowserToolExecutorTest,
       InFlightMailReadFailsClosedAfterHelperGenerationChange) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  bool fired = false;
  base::DictValue result;
  base::OnceClosure release = ExecuteDeferredMailRead(executor, &result, &fired);

  ++delegate_.mail_authorization_context.helper_generation;
  std::move(release).Run();

  ASSERT_TRUE(fired);
  EXPECT_EQ(*result.FindString("reason_code"),
            "mail_helper_generation_changed");
  EXPECT_EQ(result.DebugString().find("account-1"), std::string::npos);
}

TEST_F(MahoBrowserToolExecutorTest,
       InFlightMailReadFailsClosedAfterGlobalPolicyRevocation) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  bool fired = false;
  base::DictValue result;
  base::OnceClosure release = ExecuteDeferredMailRead(executor, &result, &fired);

  delegate_.mail_authorization_context.global_policy =
      maho::ai::MailGlobalPolicy::kDeny;
  std::move(release).Run();

  ASSERT_TRUE(fired);
  EXPECT_EQ(*result.FindString("reason_code"), "mail_global_policy_denied");
  EXPECT_EQ(result.DebugString().find("account-1"), std::string::npos);
}

TEST_F(MahoBrowserToolExecutorTest,
       MailCompletionRecheckInventoryIsReadOnly) {
  for (std::string_view tool : maho::ai::kMailReadTools) {
    EXPECT_TRUE(MahoBrowserToolExecutor::RequiresMailCompletionAuthorization(
        tool)) << tool;
  }
  for (std::string_view tool : maho::ai::kMailWriteAccountTools) {
    EXPECT_FALSE(MahoBrowserToolExecutor::RequiresMailCompletionAuthorization(
        tool)) << tool;
  }
}

TEST_F(MahoBrowserToolExecutorTest, MailSendRequiresApprovalAndDispatches) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));

  base::DictValue arguments;
  arguments.Set("account_id", "account-1");
  base::ListValue to;
  to.Append("dest@example.com");
  arguments.Set("to", std::move(to));
  arguments.Set("subject", "Hello");
  arguments.Set("body_text", "Body");
  base::ListValue attachments;
  base::DictValue attachment;
  attachment.Set("filename", "a.txt");
  attachment.Set("mime_type", "text/plain");
  attachment.Set("data_base64", "QQ==");
  attachments.Append(std::move(attachment));
  arguments.Set("attachments", std::move(attachments));

  base::DictValue result = Execute(executor, "mail_send", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  EXPECT_EQ(delegate_.mail_send_count, 1);
  EXPECT_TRUE(delegate_.last_write_already_authorized);

  std::optional<base::Value> parsed = base::JSONReader::Read(
      delegate_.last_write_request_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  const base::DictValue& request = parsed->GetDict();
  EXPECT_EQ(*request.FindString("account_id"), "account-1");
  EXPECT_EQ(*request.FindString("subject"), "Hello");
  const base::ListValue* to_list = request.FindList("to");
  ASSERT_TRUE(to_list);
  ASSERT_EQ(to_list->size(), 1u);
  EXPECT_EQ((*to_list)[0].GetString(), "dest@example.com");
  const base::ListValue* atts = request.FindList("attachments");
  ASSERT_TRUE(atts);
  ASSERT_EQ(atts->size(), 1u);
  const base::DictValue& a0 = (*atts)[0].GetDict();
  EXPECT_EQ(*a0.FindString("filename"), "a.txt");
  EXPECT_EQ(*a0.FindString("mime_type"), "text/plain");
  // maho-core ComposeAttachment carries base64 bytes under `data`.
  EXPECT_EQ(*a0.FindString("data"), "QQ==");
}

TEST_F(MahoBrowserToolExecutorTest, MailSendDeniedDoesNotDispatch) {
  auto executor = MakeExecutor(base::BindRepeating([](const Authorization&) {
    ApprovalDecision decision;
    decision.approved = false;
    decision.error = "denied by policy";
    return decision;
  }));

  base::DictValue arguments;
  arguments.Set("account_id", "account-1");
  base::ListValue to;
  to.Append("dest@example.com");
  arguments.Set("to", std::move(to));
  arguments.Set("subject", "Hello");

  base::DictValue result = Execute(executor, "mail_send", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(delegate_.mail_send_count, 0);
}

TEST_F(MahoBrowserToolExecutorTest, MailSendWithoutApprovalCallbackFailsClosed) {
  auto executor = MakeExecutor();  // No approval callback wired.

  base::DictValue arguments;
  arguments.Set("account_id", "account-1");
  base::ListValue to;
  to.Append("dest@example.com");
  arguments.Set("to", std::move(to));
  arguments.Set("subject", "Hello");

  base::DictValue result = Execute(executor, "mail_send", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(delegate_.mail_send_count, 0);
}

TEST_F(MahoBrowserToolExecutorTest, MailSendMissingRecipientsFails) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));

  base::DictValue arguments;
  arguments.Set("account_id", "account-1");
  arguments.Set("subject", "Hello");

  base::DictValue result = Execute(executor, "mail_send", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(delegate_.mail_send_count, 0);
}

TEST_F(MahoBrowserToolExecutorTest, MailUpdateDraftRequiresDraftId) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));

  base::DictValue arguments;
  arguments.Set("account_id", "account-1");
  base::ListValue to;
  to.Append("dest@example.com");
  arguments.Set("to", std::move(to));
  arguments.Set("subject", "Hello");

  base::DictValue result =
      Execute(executor, "mail_update_draft", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(delegate_.mail_update_draft_count, 0);
}

TEST_F(MahoBrowserToolExecutorTest, MailSaveDraftDispatchesWithAuthorization) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));

  base::DictValue arguments;
  arguments.Set("account_id", "account-1");
  base::ListValue to;
  to.Append("dest@example.com");
  arguments.Set("to", std::move(to));
  arguments.Set("subject", "Draft subject");

  base::DictValue result =
      Execute(executor, "mail_save_draft", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  EXPECT_EQ(delegate_.mail_save_draft_count, 1);
  EXPECT_EQ(delegate_.mail_send_count, 0);
  EXPECT_TRUE(delegate_.last_write_already_authorized);
}

TEST_F(MahoBrowserToolExecutorTest, MailUpdateDraftDispatchesWithDraftId) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));

  base::DictValue arguments;
  arguments.Set("draft_id", "draft-123");
  arguments.Set("account_id", "account-1");
  base::ListValue to;
  to.Append("dest@example.com");
  arguments.Set("to", std::move(to));
  arguments.Set("subject", "Updated subject");

  base::DictValue result =
      Execute(executor, "mail_update_draft", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  EXPECT_EQ(delegate_.mail_update_draft_count, 1);
  EXPECT_EQ(delegate_.last_update_draft_id, "draft-123");
  EXPECT_TRUE(delegate_.last_write_already_authorized);
}

TEST_F(MahoBrowserToolExecutorTest, MailSendMapsCcBccAndReplyHeaders) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));

  base::DictValue arguments;
  arguments.Set("account_id", "account-1");
  base::ListValue to;
  to.Append("dest@example.com");
  arguments.Set("to", std::move(to));
  arguments.Set("subject", "Hello");
  base::ListValue cc;
  cc.Append("cc@example.com");
  arguments.Set("cc", std::move(cc));
  base::ListValue bcc;
  bcc.Append("bcc@example.com");
  arguments.Set("bcc", std::move(bcc));
  arguments.Set("in_reply_to", "<orig@example.com>");
  arguments.Set("references", "<a@example.com> <b@example.com>");

  base::DictValue result = Execute(executor, "mail_send", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  ASSERT_EQ(delegate_.mail_send_count, 1);
  std::optional<base::Value> parsed = base::JSONReader::Read(
      delegate_.last_write_request_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  const base::DictValue& request = parsed->GetDict();
  const base::ListValue* cc_list = request.FindList("cc");
  ASSERT_TRUE(cc_list);
  ASSERT_EQ(cc_list->size(), 1u);
  EXPECT_EQ((*cc_list)[0].GetString(), "cc@example.com");
  const base::ListValue* bcc_list = request.FindList("bcc");
  ASSERT_TRUE(bcc_list);
  ASSERT_EQ(bcc_list->size(), 1u);
  EXPECT_EQ((*bcc_list)[0].GetString(), "bcc@example.com");
  EXPECT_EQ(*request.FindString("in_reply_to"), "<orig@example.com>");
  EXPECT_EQ(*request.FindString("references"),
            "<a@example.com> <b@example.com>");
}

TEST_F(MahoBrowserToolExecutorTest, MailSendRejectsAttachmentMissingField) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));

  base::DictValue arguments;
  arguments.Set("account_id", "account-1");
  base::ListValue to;
  to.Append("dest@example.com");
  arguments.Set("to", std::move(to));
  arguments.Set("subject", "Hello");
  base::ListValue attachments;
  base::DictValue attachment;
  attachment.Set("filename", "a.txt");
  attachment.Set("mime_type", "text/plain");
  // Missing data_base64 -> must be rejected before dispatch.
  attachments.Append(std::move(attachment));
  arguments.Set("attachments", std::move(attachments));

  base::DictValue result = Execute(executor, "mail_send", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(delegate_.mail_send_count, 0);
}

class MahoBrowserToolExecutorParityTest : public MahoBrowserToolExecutorTest {};

TEST_F(MahoBrowserToolExecutorParityTest,
       NavigateEmptyAllowedDomainFailsClosed_NotAdr0016Divergence) {
  // ADR 0016 states empty allowed-domain state fails closed on both MCP and
  // in-browser paths; this is parity, not an intentional divergence.
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  delegate_.active_url = "about:blank";

  base::DictValue arguments;
  arguments.Set("url", "https://example.test/");
  base::DictValue result =
      Execute(executor, "browser_navigate", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_TRUE(result.FindInt("error_code").has_value());
  EXPECT_NE(result.FindString("error")->find("failed closed"),
            std::string::npos);
  EXPECT_EQ(delegate_.navigate_count, 0);
}

TEST_F(MahoBrowserToolExecutorParityTest,
       AccessibilitySnapshotRequiredBeforeRefActionMatchesMcp) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  delegate_.snapshot_refs[1] = 101;

  base::DictValue arguments;
  arguments.Set("ref", 1);
  base::DictValue result =
      Execute(executor, "browser_click", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(result.FindInt("error_code").value(), -32008);
  EXPECT_NE(result.FindString("error")->find("accessibility_snapshot"),
            std::string::npos);
  EXPECT_EQ(delegate_.click_count, 0);
}

TEST_F(MahoBrowserToolExecutorParityTest,
       ClickUnknownRefFailureMatchesMcpErrorCode) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  PopulateRef(executor, 1, 101);

  base::DictValue arguments;
  arguments.Set("ref", 404);
  base::DictValue result =
      Execute(executor, "browser_click", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(result.FindInt("error_code").value(), -32000);
  ASSERT_TRUE(result.FindString("error"));
  EXPECT_EQ(*result.FindString("error"),
            "Unknown ref: call browser_accessibility_snapshot first");
  EXPECT_EQ(delegate_.click_count, 0);
}

TEST_F(MahoBrowserToolExecutorParityTest,
       TypeCredentialFieldDenialMatchesMcpAndDoesNotEchoText) {
  static constexpr char kSecret[] = "S3NTINEL-parity-secret";
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  PopulateRef(executor, 2, 202);
  maho::MahoMcpFieldMetadata metadata;
  metadata.classified = true;
  metadata.is_protected = true;
  delegate_.field_metadata[202] = metadata;

  base::DictValue arguments;
  arguments.Set("ref", 2);
  arguments.Set("text", kSecret);
  base::DictValue result =
      Execute(executor, "browser_type", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(result.FindInt("error_code").value(), -32002);
  ASSERT_TRUE(result.FindString("error"));
  EXPECT_NE(result.FindString("error")->find("Credential fields"),
            std::string::npos);
  EXPECT_EQ(delegate_.type_count, 0);
  EXPECT_EQ(result.DebugString().find(kSecret), std::string::npos);
}

TEST_F(MahoBrowserToolExecutorParityTest, SelectResultValueMatchesMcp) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  PopulateRef(executor, 3, 303);

  base::DictValue arguments;
  arguments.Set("ref", 3);
  arguments.Set("value", "compact");
  base::DictValue result =
      Execute(executor, "browser_select", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value());
  EXPECT_TRUE(result.FindBool("selected").value());
  EXPECT_EQ(result.FindInt("ref").value(), 3);
  EXPECT_EQ(*result.FindString("value"), "compact");
  EXPECT_EQ(delegate_.select_count, 1);
  EXPECT_EQ(delegate_.last_selected_value, "compact");
}

TEST_F(MahoBrowserToolExecutorParityTest,
       ScrollDirectionAndPixelBehaviorMatchesMcp) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));

  base::DictValue down;
  down.Set("direction", "down");
  down.Set("pixels", 200);
  base::DictValue down_result =
      Execute(executor, "browser_scroll", std::move(down));

  EXPECT_TRUE(down_result.FindBool("ok").value());
  EXPECT_TRUE(down_result.FindBool("scrolled").value());
  EXPECT_EQ(down_result.FindInt("new_scroll_y").value(), 200);
  EXPECT_EQ(delegate_.scroll_count, 1);
  EXPECT_EQ(delegate_.last_scroll_direction, "down");
  EXPECT_EQ(delegate_.last_scroll_pixels, 200);
  EXPECT_FALSE(delegate_.last_scroll_ax_id.has_value());

  base::DictValue up;
  up.Set("direction", "up");
  up.Set("pixels", 50);
  base::DictValue up_result =
      Execute(executor, "browser_scroll", std::move(up));

  EXPECT_TRUE(up_result.FindBool("scrolled").value());
  EXPECT_EQ(up_result.FindInt("new_scroll_y").value(), 150);
  EXPECT_EQ(delegate_.scroll_count, 2);
  EXPECT_EQ(delegate_.last_scroll_direction, "up");
  EXPECT_EQ(delegate_.last_scroll_pixels, 50);
}

TEST_F(MahoBrowserToolExecutorParityTest,
       Adr0016BrowserHoverSensitiveApprovalWithoutLease) {
  // ADR 0016: hover mirrors MCP's no-lease boundary, but the in-browser path
  // intentionally adds sensitive approval gating before dispatch.
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  PopulateRef(executor, 4, 404);

  base::DictValue arguments;
  arguments.Set("ref", 4);
  base::DictValue result =
      Execute(executor, "browser_hover", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value());
  EXPECT_TRUE(result.FindBool("hovered").value());
  EXPECT_EQ(result.FindInt("ref").value(), 4);
  EXPECT_EQ(delegate_.hover_count, 1);
  EXPECT_EQ(delegate_.last_hover_ax_id, 404);
  EXPECT_FALSE(lease_registry_.IsHeldBy(7, "agent-session"));
}

TEST_F(MahoBrowserToolExecutorParityTest, KeyPressValidationMatchesMcp) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));

  base::DictValue valid;
  valid.Set("key", "Enter");
  base::ListValue modifiers;
  modifiers.Append("shift");
  valid.Set("modifiers", std::move(modifiers));
  base::DictValue valid_result =
      Execute(executor, "browser_key_press", std::move(valid));

  EXPECT_TRUE(valid_result.FindBool("ok").value());
  EXPECT_TRUE(valid_result.FindBool("key_pressed").value());
  EXPECT_EQ(*valid_result.FindString("key"), "Enter");
  EXPECT_EQ(delegate_.key_press_count, 1);
  EXPECT_EQ(delegate_.last_key, "Enter");
  ASSERT_EQ(delegate_.last_modifiers.size(), 1u);
  EXPECT_EQ(delegate_.last_modifiers.front(), "shift");

  base::DictValue invalid_key;
  invalid_key.Set("key", "LaunchMissiles");
  base::DictValue invalid_key_result =
      Execute(executor, "browser_key_press", std::move(invalid_key));

  EXPECT_FALSE(invalid_key_result.FindBool("ok").value_or(true));
  EXPECT_EQ(invalid_key_result.FindInt("error_code").value(), -32602);
  ASSERT_TRUE(invalid_key_result.FindString("error"));
  EXPECT_NE(invalid_key_result.FindString("error")->find("unknown key"),
            std::string::npos);
  EXPECT_EQ(delegate_.key_press_count, 1);

  base::DictValue invalid_modifier;
  invalid_modifier.Set("key", "Enter");
  base::ListValue bad_modifiers;
  bad_modifiers.Append("hyper");
  invalid_modifier.Set("modifiers", std::move(bad_modifiers));
  base::DictValue invalid_modifier_result =
      Execute(executor, "browser_key_press", std::move(invalid_modifier));

  EXPECT_FALSE(invalid_modifier_result.FindBool("ok").value_or(true));
  EXPECT_EQ(invalid_modifier_result.FindInt("error_code").value(), -32602);
  ASSERT_TRUE(invalid_modifier_result.FindString("error"));
  EXPECT_NE(invalid_modifier_result.FindString("error")->find(
                "unknown modifier"),
            std::string::npos);
  EXPECT_EQ(delegate_.key_press_count, 1);
}

// Parity default-deny applies to mutations that leave the browser: with no
// typed-approval authorization wired the request is refused, and nothing is
// dispatched or leased on the way out. In-browser mutation needs no
// authorization at all, so the second half asserts the opposite.
TEST_F(MahoBrowserToolExecutorParityTest,
       BoundaryMutationApprovalDefaultDenyPreventsDispatch) {
  auto executor = MakeExecutor();
  PopulateRef(executor, 5, 505);

  base::DictValue arguments;
  arguments.Set("ref", 5);
  base::DictValue result =
      Execute(executor, "browser_file_upload_select", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(result.FindInt("error_code").value_or(0), -32008);
  ASSERT_TRUE(result.FindString("error"));
  EXPECT_NE(result.FindString("error")->find("typed approval"),
            std::string::npos);
  EXPECT_EQ(delegate_.click_count, 0);
  EXPECT_FALSE(lease_registry_.IsHeldBy(7, "agent-session"));

  base::DictValue click_arguments;
  click_arguments.Set("ref", 5);
  base::DictValue click_result =
      Execute(executor, "browser_click", std::move(click_arguments));

  EXPECT_TRUE(click_result.FindBool("ok").value_or(false));
  EXPECT_EQ(delegate_.click_count, 1);
}

TEST_F(MahoBrowserToolExecutorParityTest,
       Adr0016ActiveTabOriginScopingNeedsNoDestinationApproval) {
  // ADR 0016 scopes the agent to the active tab origin for the turn. Driving
  // the browser is full access, so cross-origin navigation raises no approval
  // ask of its own — the origin machinery (domain policy plus the controller's
  // granted origins) is what scopes it, and a host-less scheme that no origin
  // rule could constrain is still refused outright.
  std::optional<Authorization> captured;
  auto executor = MakeExecutor(base::BindRepeating(
      [](std::optional<Authorization>* out, const Authorization& metadata) {
        *out = metadata;
        ApprovalDecision decision;
        decision.error = "ADR 0016 cross-origin approval denied by test";
        return decision;
      },
      &captured));

  base::DictValue arguments;
  arguments.Set("url", "https://other.example/path");
  arguments.Set("page_derived_justification", true);
  base::DictValue result =
      Execute(executor, "browser_navigate", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  EXPECT_FALSE(captured.has_value());
  EXPECT_EQ(delegate_.navigate_count, 1);

  base::DictValue blocked;
  blocked.Set("url", "javascript:alert(1)");
  base::DictValue blocked_result =
      Execute(executor, "browser_navigate", std::move(blocked));

  EXPECT_FALSE(blocked_result.FindBool("ok").value_or(true));
  EXPECT_EQ(delegate_.navigate_count, 1);
}

TEST_F(MahoBrowserToolExecutorParityTest,
       Adr0016ExternalLeaseConflictForceSteals) {
  // ADR 0016 now permits force-steal for this user-driven agent session.
  auto external_lease = lease_registry_.Acquire(
      7, "external-session", base::Seconds(60), false);
  ASSERT_TRUE(external_lease.ok);
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  PopulateRef(executor, 6, 606);

  base::DictValue arguments;
  arguments.Set("ref", 6);
  base::DictValue result =
      Execute(executor, "browser_click", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  EXPECT_TRUE(lease_registry_.IsHeldBy(7, "agent-session"));
  EXPECT_FALSE(lease_registry_.IsHeldBy(7, "external-session"));
  EXPECT_EQ(delegate_.click_count, 1);
}

TEST_F(MahoBrowserToolExecutorParityTest,
       ScreenshotProofDispatchesToCapturePath) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  base::DictValue arguments;
  base::DictValue result =
      Execute(executor, "screenshot_proof", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  EXPECT_EQ(*result.FindString("tool"), "screenshot_proof");
  EXPECT_EQ(*result.FindString("content_type"), "image/png");
  EXPECT_EQ(*result.FindString("data"), delegate_.screenshot_b64);
  EXPECT_EQ(delegate_.capture_full_page_count, 1);
  EXPECT_TRUE(lease_registry_.IsHeldBy(7, "agent-session"));
}

TEST_F(MahoBrowserToolExecutorParityTest,
       LookupPageAdapterForSeamInterceptsAndFallsThrough) {
  maho::ai::MahoPageAdapterRegistry::GetGlobalRegistry().Clear();
  maho::ai::MahoPageAdapterRegistry::GetGlobalRegistry().RegisterSimple(
      "test-adapter-1",
      {"https://example.com"},
      {"page_get_text", "browser_click"},
      "isolated_world_1",
      4096);

  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
  PopulateRef(executor, 10, 1010);

  // 1. Registered adapter on matching origin falls through to real DOM execution tier
  base::DictValue arguments;
  arguments.Set("ref_id", "ref-10");
  base::DictValue adapter_result =
      Execute(executor, "page_get_text", std::move(arguments));

  EXPECT_TRUE(adapter_result.FindBool("ok").value_or(false));
  EXPECT_EQ(*adapter_result.FindString("tool"), "page_get_text");
  EXPECT_EQ(*adapter_result.FindString("text"), "element text");
  EXPECT_EQ(delegate_.get_element_text_count, 1);

  // 2. Unregistered origin also executes through generic DOM execution path
  delegate_.active_url = "https://unregistered-domain.org/page";
  base::DictValue generic_args;
  generic_args.Set("ref_id", "ref-10");
  base::DictValue generic_result =
      Execute(executor, "page_get_text", std::move(generic_args));

  EXPECT_TRUE(generic_result.FindBool("ok").value_or(false));
  EXPECT_EQ(*generic_result.FindString("tool"), "page_get_text");
  EXPECT_EQ(*generic_result.FindString("text"), "element text");
  EXPECT_EQ(delegate_.get_element_text_count, 2);

  EXPECT_TRUE(generic_result.FindBool("ok").value_or(false));
  EXPECT_EQ(*generic_result.FindString("tool"), "page_get_text");
  EXPECT_EQ(generic_result.FindString("adapter_id"), nullptr);
  const std::string* generic_tier = generic_result.FindString("tier");
  EXPECT_TRUE(!generic_tier || *generic_tier != "page_adapter");

  maho::ai::MahoPageAdapterRegistry::GetGlobalRegistry().Clear();
}

// Wave 2A (G6 read-back): the executor must stamp the adapter's live session
// runtime config into the broker request context at BOTH broker-gate sites
// (screenshot proof + browser action authorization), so the tier file gate
// and the final-confirm gate react to the panel card's flags instead of the
// struct defaults. SC1: read_only denies sensitive, guard prompts,
// full_access allows; final_confirm true fires the gate, false suppresses it.
class MahoBrowserToolExecutorRuntimeConfigTest
    : public MahoBrowserToolExecutorTest {};

TEST_F(MahoBrowserToolExecutorRuntimeConfigTest,
       ReadOnlyTierDeniesSensitiveFileScopedActionWithTierMessage) {
  MahoAiRuntimeConfig config;
  config.permission_tier = "read_only";
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction), nullptr,
                               "agent-session", &config);
  PopulateRef(executor, 1, 101);

  base::DictValue arguments;
  arguments.Set("ref", 1);
  arguments.Set("path", "/etc/hosts");
  base::DictValue result =
      Execute(executor, "browser_file_upload_select", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(result.FindInt("error_code").value(), -32008);
  ASSERT_TRUE(result.FindString("error"));
  EXPECT_NE(result.FindString("error")->find(
                "Permission tier read_only denies local file writes"),
            std::string::npos);
  EXPECT_FALSE(lease_registry_.IsHeldBy(7, "agent-session"));
}

TEST_F(MahoBrowserToolExecutorRuntimeConfigTest,
       GuardTierPromptsForFileScopeOutsideWhitelist) {
  // Guard tier is free inside the whitelist roots and ask-gates outside.
  // browser_accessibility_snapshot carries no contract-level approval
  // requirement, so the prompt below can only come from the broker tier ask
  // surfacing through the executor.
  MahoAiRuntimeConfig config;
  config.permission_tier = "guard";
  config.fs_whitelist_roots = {"/tmp/maho-allow"};
  int approval_calls = 0;
  auto executor = MakeExecutor(
      base::BindRepeating(
          [](int* calls, const Authorization&) {
            ++*calls;
            ApprovalDecision decision;
            decision.approved = true;
            return decision;
          },
          &approval_calls),
      nullptr, "agent-session", &config);
  PopulateRef(executor, 1, 101);

  base::DictValue outside;
  outside.Set("path", "/etc/hosts");
  base::DictValue result =
      Execute(executor, "browser_accessibility_snapshot", std::move(outside));
  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  EXPECT_EQ(approval_calls, 1);
}

TEST_F(MahoBrowserToolExecutorRuntimeConfigTest,
       GuardTierFileScopeInsideWhitelistNeedsNoPrompt) {
  MahoAiRuntimeConfig config;
  config.permission_tier = "guard";
  config.fs_whitelist_roots = {"/tmp/maho-allow"};
  int approval_calls = 0;
  auto executor = MakeExecutor(
      base::BindRepeating(
          [](int* calls, const Authorization&) {
            ++*calls;
            ApprovalDecision decision;
            decision.approved = true;
            return decision;
          },
          &approval_calls),
      nullptr, "agent-session", &config);
  PopulateRef(executor, 1, 101);

  base::DictValue inside;
  inside.Set("path", "/tmp/maho-allow/report.pdf");
  base::DictValue result =
      Execute(executor, "browser_accessibility_snapshot", std::move(inside));
  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  EXPECT_EQ(approval_calls, 0);
}

TEST_F(MahoBrowserToolExecutorRuntimeConfigTest,
       GuardTierFileScopeFailsClosedWithoutApprovalCallback) {
  MahoAiRuntimeConfig config;
  config.permission_tier = "guard";
  config.fs_whitelist_roots = {"/tmp/maho-allow"};
  auto executor = MakeExecutor(
      MahoBrowserToolExecutor::BrowserActionApprovalCallback(), nullptr,
      "agent-session", &config);
  PopulateRef(executor, 1, 101);

  base::DictValue outside;
  outside.Set("path", "/etc/hosts");
  base::DictValue result =
      Execute(executor, "browser_accessibility_snapshot", std::move(outside));
  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(result.FindInt("error_code").value(), -32008);
}

TEST_F(MahoBrowserToolExecutorRuntimeConfigTest,
       FullAccessTierAllowsFileScopeWithoutTierAsk) {
  MahoAiRuntimeConfig config;
  config.permission_tier = "full_access";
  int approval_calls = 0;
  auto executor = MakeExecutor(
      base::BindRepeating(
          [](int* calls, const Authorization&) {
            ++*calls;
            ApprovalDecision decision;
            decision.approved = true;
            return decision;
          },
          &approval_calls),
      nullptr, "agent-session", &config);
  PopulateRef(executor, 1, 101);

  // Contract-free read with a file-scope outside any whitelist: full_access
  // falls through the tier gate, so no broker ask is owed at all.
  base::DictValue outside;
  outside.Set("path", "/etc/hosts");
  base::DictValue snapshot_result =
      Execute(executor, "browser_accessibility_snapshot", std::move(outside));
  EXPECT_TRUE(snapshot_result.FindBool("ok").value_or(false));
  EXPECT_EQ(approval_calls, 0);

  // A sensitive in-browser action owes no ask either: driving the browser is
  // full access, and the tier adds nothing on top of that.
  base::DictValue click_arguments;
  click_arguments.Set("ref", 1);
  base::DictValue click_result =
      Execute(executor, "browser_click", std::move(click_arguments));
  EXPECT_TRUE(click_result.FindBool("ok").value_or(false));
  EXPECT_EQ(delegate_.click_count, 1);
  EXPECT_EQ(approval_calls, 0);
}

TEST_F(MahoBrowserToolExecutorRuntimeConfigTest,
       ReadOnlyTierDeniesFileScopedScreenshotProofRead) {
  // Site 1 (screenshot proof): read_only + file-scoped request outside the
  // (empty) whitelist denies the read with the tier message.
  MahoAiRuntimeConfig config;
  config.permission_tier = "read_only";
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction), nullptr,
                               "agent-session", &config);

  base::DictValue arguments;
  arguments.Set("path", "/etc/hosts");
  base::DictValue result =
      Execute(executor, "screenshot_proof", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(result.FindInt("error_code").value(), -32008);
  ASSERT_TRUE(result.FindString("error"));
  EXPECT_NE(result.FindString("error")->find(
                "Permission tier read_only denies reads outside the session "
                "file whitelist"),
            std::string::npos);
  EXPECT_EQ(delegate_.capture_full_page_count, 0);
}

TEST_F(MahoBrowserToolExecutorRuntimeConfigTest,
       GuardTierScreenshotAskFailsClosedWithoutCallback) {
  MahoAiRuntimeConfig config;
  config.permission_tier = "guard";
  config.fs_whitelist_roots = {"/tmp/maho-allow"};
  auto executor = MakeExecutor(
      MahoBrowserToolExecutor::BrowserActionApprovalCallback(), nullptr,
      "agent-session", &config);

  base::DictValue arguments;
  arguments.Set("path", "/etc/hosts");
  base::DictValue result =
      Execute(executor, "screenshot_proof", std::move(arguments));

  // The broker ask must not be silently dropped on a tool with no contract
  // approval path; with no callback wired it fails closed.
  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(result.FindInt("error_code").value(), -32008);
  EXPECT_EQ(delegate_.capture_full_page_count, 0);
}

TEST_F(MahoBrowserToolExecutorRuntimeConfigTest,
       GuardTierScreenshotAskPromptsThenCaptures) {
  MahoAiRuntimeConfig config;
  config.permission_tier = "guard";
  config.fs_whitelist_roots = {"/tmp/maho-allow"};
  int approval_calls = 0;
  auto executor = MakeExecutor(
      base::BindRepeating(
          [](int* calls, const Authorization& metadata) {
            ++*calls;
            EXPECT_EQ(metadata.tool_name, "screenshot_proof");
            EXPECT_TRUE(metadata.requires_approval);
            ApprovalDecision decision;
            decision.approved = true;
            return decision;
          },
          &approval_calls),
      nullptr, "agent-session", &config);

  base::DictValue outside;
  outside.Set("path", "/etc/hosts");
  base::DictValue result =
      Execute(executor, "screenshot_proof", std::move(outside));
  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  EXPECT_EQ(approval_calls, 1);
  EXPECT_EQ(delegate_.capture_full_page_count, 1);

  // Inside the whitelist the tier gate is inert: no second ask.
  base::DictValue inside;
  inside.Set("path", "/tmp/maho-allow/proof.png");
  base::DictValue inside_result =
      Execute(executor, "screenshot_proof", std::move(inside));
  EXPECT_TRUE(inside_result.FindBool("ok").value_or(false));
  EXPECT_EQ(approval_calls, 1);
  EXPECT_EQ(delegate_.capture_full_page_count, 2);
}

TEST_F(MahoBrowserToolExecutorRuntimeConfigTest,
       FullAccessScreenshotCapturesOutsideWhitelistWithoutAsk) {
  MahoAiRuntimeConfig config;
  config.permission_tier = "full_access";
  int approval_calls = 0;
  auto executor = MakeExecutor(
      base::BindRepeating(
          [](int* calls, const Authorization&) {
            ++*calls;
            ApprovalDecision decision;
            decision.approved = true;
            return decision;
          },
          &approval_calls),
      nullptr, "agent-session", &config);

  base::DictValue arguments;
  arguments.Set("path", "/etc/hosts");
  base::DictValue result =
      Execute(executor, "screenshot_proof", std::move(arguments));
  EXPECT_TRUE(result.FindBool("ok").value_or(false));
  EXPECT_EQ(approval_calls, 0);
  EXPECT_EQ(delegate_.capture_full_page_count, 1);
}

TEST_F(MahoBrowserToolExecutorRuntimeConfigTest,
       RuntimeConfigIsReadLiveBetweenCalls) {
  // D9 freshness: the executor holds a live pointer to the session config,
  // so tightening the tier takes effect on the next tool call without
  // re-constructing the executor.
  MahoAiRuntimeConfig config;
  config.permission_tier = "guard";
  config.fs_whitelist_roots = {"/tmp/maho-allow"};
  int approval_calls = 0;
  auto executor = MakeExecutor(
      base::BindRepeating(
          [](int* calls, const Authorization&) {
            ++*calls;
            ApprovalDecision decision;
            decision.approved = true;
            return decision;
          },
          &approval_calls),
      nullptr, "agent-session", &config);

  base::DictValue arguments;
  arguments.Set("path", "/etc/hosts");
  base::DictValue guarded =
      Execute(executor, "screenshot_proof", std::move(arguments));
  EXPECT_TRUE(guarded.FindBool("ok").value_or(false));
  EXPECT_EQ(approval_calls, 1);

  config.permission_tier = "full_access";
  base::DictValue full_arguments;
  full_arguments.Set("path", "/etc/hosts");
  base::DictValue full =
      Execute(executor, "screenshot_proof", std::move(full_arguments));
  EXPECT_TRUE(full.FindBool("ok").value_or(false));
  EXPECT_EQ(approval_calls, 1);
  EXPECT_EQ(delegate_.capture_full_page_count, 2);
}

TEST(MahoBrowserToolExecutorRuntimeConfigStampTest,
     StampPinsLiveConfigFieldsAndFileScope) {
  MahoAiRuntimeConfig config;
  config.permission_tier = "full_access";
  config.final_confirm = false;
  config.proactive_mode = true;
  config.fs_whitelist_roots = {"/Users/u/Downloads"};

  maho::ai::CapabilityRequestContext req_ctx;
  base::DictValue arguments;
  arguments.Set("path", "/Users/u/Downloads/a.pdf");
  MahoBrowserToolExecutor::StampRuntimeConfig(&req_ctx, &config, arguments);
  EXPECT_EQ(req_ctx.permission_tier, "full_access");
  EXPECT_FALSE(req_ctx.final_confirm);
  EXPECT_TRUE(req_ctx.proactive_mode);
  EXPECT_EQ(req_ctx.fs_whitelist_roots, config.fs_whitelist_roots);
  EXPECT_EQ(req_ctx.fs_path, "/Users/u/Downloads/a.pdf");

  // No live config keeps the broker defaults; no path keeps the tier gate
  // inert.
  maho::ai::CapabilityRequestContext defaults;
  base::DictValue no_path;
  MahoBrowserToolExecutor::StampRuntimeConfig(&defaults, nullptr, no_path);
  EXPECT_EQ(defaults.permission_tier, "guard");
  EXPECT_TRUE(defaults.final_confirm);
  EXPECT_FALSE(defaults.proactive_mode);
  EXPECT_TRUE(defaults.fs_whitelist_roots.empty());
  EXPECT_TRUE(defaults.fs_path.empty());
}

// Builds the request context exactly as the executor stamps it at its broker
// gate sites, then completes the structural fields the executor sets before
// Evaluate. Used to pin the final_confirm gate's response to the stamped
// flag. The capability has to be an off-browser read: reads carry no
// contract-level approval requirement, so any ask can only come from the
// gate, and the gate itself is scoped to capabilities that leave the browser.
maho::ai::CapabilityRequestContext StampedReadContext(
    const MahoAiRuntimeConfig& config) {
  maho::ai::CapabilityRequestContext req_ctx;
  base::DictValue arguments;
  MahoBrowserToolExecutor::StampRuntimeConfig(&req_ctx, &config, arguments);
  req_ctx.surface = maho::ai::CapabilitySurface::kDesktopAgent;
  req_ctx.principal =
      maho::ai::CapabilityPrincipal::MakeInternalAgent("agent-session");
  req_ctx.capability_id = "mail.accounts.list";
  req_ctx.active_tab_id = 7;
  req_ctx.action_consequence = "submit";  // browser-minted classification
  return req_ctx;
}

TEST(MahoBrowserToolExecutorRuntimeConfigStampTest,
     FinalConfirmTrueFiresGateOnStampedContext) {
  maho::ai::MahoCapabilityBroker broker;
  MahoAiRuntimeConfig config;  // final_confirm defaults to true.
  maho::ai::CapabilityRequestContext req_ctx = StampedReadContext(config);

  maho::ai::CapabilityEvaluationResult gated = broker.Evaluate(req_ctx);
  EXPECT_TRUE(gated.RequiresApproval());

  // The ask routes through the approval-token handshake: a granted token
  // satisfies the gate.
  req_ctx.approval_token = "user-ok";
  EXPECT_TRUE(broker.Evaluate(req_ctx).IsPermitted());
}

TEST(MahoBrowserToolExecutorRuntimeConfigStampTest,
     FinalConfirmFalseSuppressesGateOnStampedContext) {
  maho::ai::MahoCapabilityBroker broker;
  MahoAiRuntimeConfig config;
  config.final_confirm = false;
  maho::ai::CapabilityRequestContext req_ctx = StampedReadContext(config);

  // Gate suppressed: the submit classification no longer owes an approval.
  EXPECT_TRUE(broker.Evaluate(req_ctx).IsPermitted());
}

}
