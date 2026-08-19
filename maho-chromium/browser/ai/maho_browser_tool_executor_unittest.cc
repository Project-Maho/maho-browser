#include "maho/browser/ai/maho_browser_tool_executor.h"

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/json/json_reader.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "base/values.h"
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

  int active_tab_id = 7;
  int64_t target_generation = 1;
  bool advance_generation_on_approval = false;
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

  std::string GetElementText(int tab_id, const std::string& ref_id) override {
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
  void CaptureFullPagePngBase64(
      int tab_id,
      base::OnceCallback<void(std::string)> callback) override {
    std::move(callback).Run({});
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
  MahoBrowserToolExecutor MakeExecutor(
      MahoBrowserToolExecutor::BrowserActionApprovalCallback approval_callback =
          MahoBrowserToolExecutor::BrowserActionApprovalCallback(),
      maho::MahoMcpLeaseRegistry* lease_registry = nullptr,
      std::string lease_holder_id = "agent-session") {
    if (!lease_registry) {
      lease_registry = &lease_registry_;
    }
    return MahoBrowserToolExecutor(
        nullptr, true, base::BindRepeating([] { return true; }),
        std::move(approval_callback), &delegate_, lease_registry,
        std::move(lease_holder_id));
  }

  base::DictValue Execute(MahoBrowserToolExecutor& executor,
                          const std::string& tool_name,
                          base::DictValue arguments = base::DictValue()) {
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

TEST_F(MahoBrowserToolExecutorTest, SensitiveActionDeniesWithoutAuthorization) {
  auto executor = MakeExecutor();
  PopulateRef(executor, 1, 101);

  base::DictValue arguments;
  arguments.Set("ref", 1);
  base::DictValue result = Execute(executor, "browser_click", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(result.FindInt("error_code").value(), -32008);
  EXPECT_EQ(delegate_.click_count, 0);
}

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
  base::DictValue result = Execute(executor, "browser_click", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_NE(result.FindString("error")->find("denied by test approval"),
            std::string::npos);
  EXPECT_FALSE(lease_registry_.IsHeldBy(7, "agent-session"));
  EXPECT_EQ(delegate_.click_count, 0);
}

TEST_F(MahoBrowserToolExecutorTest,
       TargetGenerationChangeBeforeDispatchFailsClosed) {
  auto executor = MakeExecutor(base::BindRepeating(
      [](FakeBrowserActionDelegate* delegate, const Authorization& metadata) {
        ++delegate->target_generation;
        return ApproveAction(metadata);
      },
      &delegate_));
  PopulateRef(executor, 1, 101);

  base::DictValue arguments;
  arguments.Set("ref", 1);
  base::DictValue result =
      Execute(executor, "browser_click", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_NE(result.FindString("error")->find("target changed"),
            std::string::npos);
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
  EXPECT_NE(result.FindString("error")->find("lease registry"),
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

TEST_F(MahoBrowserToolExecutorFallbackTest,
       ApprovalDenialStillBlocksSelectedFallbackTier) {
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

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_NE(result.FindString("error")->find("fallback selected tier denied"),
            std::string::npos);
  const base::DictValue* fallback = result.FindDict("fallback_decision");
  ASSERT_TRUE(fallback);
  EXPECT_EQ(*fallback->FindString("selected_tier"),
            "accessibility_snapshot");
  EXPECT_FALSE(lease_registry_.IsHeldBy(7, "agent-session"));
  EXPECT_EQ(delegate_.click_count, 0);
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
       CredentialFieldRejectsAllowCredentialsAfterApproval) {
  auto executor = MakeExecutor(base::BindRepeating(&ApproveAction));
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
  EXPECT_EQ(delegate_.type_count, 0);
  EXPECT_EQ(result.DebugString().find("S3NTINEL"), std::string::npos);
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

TEST_F(MahoBrowserToolExecutorTest,
       AuthorizationReceivesTypedMetadataForCrossOriginNavigate) {
  std::optional<Authorization> captured;
  auto executor = MakeExecutor(base::BindRepeating(
      [](std::optional<Authorization>* out, const Authorization& metadata) {
        *out = metadata;
        return ApproveAction(metadata);
      },
      &captured));

  base::DictValue arguments;
  arguments.Set("url", "https://other.example/path");
  arguments.Set("page_derived_justification", true);
  base::DictValue result = Execute(executor, "browser_navigate", std::move(arguments));

  EXPECT_TRUE(result.FindBool("ok").value());
  ASSERT_TRUE(captured.has_value());
  EXPECT_EQ(captured->tool_name, "browser_navigate");
  EXPECT_EQ(captured->kind, "action");
  EXPECT_EQ(captured->sensitivity, "sensitive");
  EXPECT_TRUE(captured->requires_approval);
  EXPECT_TRUE(captured->requires_lease);
  EXPECT_EQ(captured->active_origin, "https://example.com");
  EXPECT_EQ(captured->destination_origin, "https://other.example");
  EXPECT_TRUE(captured->page_derived_justification);
  EXPECT_EQ(delegate_.navigate_count, 1);
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

TEST_F(MahoBrowserToolExecutorParityTest,
       MutationApprovalDefaultDenyPreventsDispatch) {
  auto executor = MakeExecutor();
  PopulateRef(executor, 5, 505);

  base::DictValue arguments;
  arguments.Set("ref", 5);
  base::DictValue result =
      Execute(executor, "browser_click", std::move(arguments));

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(result.FindInt("error_code").value(), -32008);
  ASSERT_TRUE(result.FindString("error"));
  EXPECT_NE(result.FindString("error")->find("typed approval"),
            std::string::npos);
  EXPECT_EQ(delegate_.click_count, 0);
  EXPECT_FALSE(lease_registry_.IsHeldBy(7, "agent-session"));
}

TEST_F(MahoBrowserToolExecutorParityTest,
       Adr0016ActiveTabOriginScopingRequiresDestinationApproval) {
  // ADR 0016 intentionally specializes MCP's session allowlist: the agent uses
  // the active tab origin for the turn and cross-origin navigation must carry a
  // fresh approval request naming the destination origin.
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

  EXPECT_FALSE(result.FindBool("ok").value_or(true));
  EXPECT_EQ(result.FindInt("error_code").value(), -32008);
  ASSERT_TRUE(result.FindString("error"));
  EXPECT_NE(result.FindString("error")->find("ADR 0016"),
            std::string::npos);
  ASSERT_TRUE(captured.has_value());
  EXPECT_EQ(captured->active_origin, "https://example.com");
  EXPECT_EQ(captured->destination_origin, "https://other.example");
  EXPECT_TRUE(captured->page_derived_justification);
  EXPECT_EQ(delegate_.navigate_count, 0);
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

}
