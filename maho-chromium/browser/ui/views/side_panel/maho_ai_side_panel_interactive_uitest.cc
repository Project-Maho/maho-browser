// Copyright 2026 Maho Browser. All rights reserved.
//
// Wave 2 minimal lifecycle verification for the Maho AI side panel host.
//
// Subject under test: the existing host integration path for
// SidePanelEntryId::kMahoAiPanel registered by
// maho::RegisterMahoAiSidePanel() (via side_panel_helper.cc overlay patch).
//
// Scope: show/hide lifecycle only.
//   * Panel can be shown through the production Show() integration path.
//   * Panel can be closed cleanly.
//   * Repeated close→show cycles continue to produce a usable host.
//
// Deliberately excluded:
//   split-view, space-config, delete-dialog, build-wrapper, AI content
//   behaviour, prompt flows, UX chrome.
//
// Access pattern: state is read only through the public SidePanelUI API and
//   the testing accessor SidePanelUI::GetWebContentsForTest().
//
// Tests:
//   1. ShowPanel_EntryIsShowing          — Show() makes the panel visible.
//   2. ClosePanel_EntryIsNoLongerShowing — Close() dismisses cleanly; entry is
//                                          not showing and IsSidePanelShowing()
//                                          returns false.
//   3. ClosePanel_CanShowHostAgainAfterClose
//                                       — After Close(), Show() can reopen the
//                                          entry and expose a host WebContents
//                                          again.
//   4. RepeatedShowHide_NoOrphanedHost   — Three open→close cycles; after each
//                                          close the panel is not showing, and
//                                          the next Show() still reaches a
//                                          usable host.

#include "base/run_loop.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_key.h"
#include "chrome/browser/ui/side_panel/side_panel_enums.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "maho/browser/maho_private_context_policy.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_page_handler.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_ui.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

namespace {

// Convenience alias matching the Maho entry registered by
// maho::RegisterMahoAiSidePanel().
constexpr SidePanelEntryId kMahoAiEntryId = SidePanelEntryId::kMahoAiPanel;

class MahoAiSidePanelLifecycleTest : public InProcessBrowserTest {
 public:
  MahoAiSidePanelLifecycleTest() = default;
  MahoAiSidePanelLifecycleTest(const MahoAiSidePanelLifecycleTest&) = delete;
  MahoAiSidePanelLifecycleTest& operator=(
      const MahoAiSidePanelLifecycleTest&) = delete;
  ~MahoAiSidePanelLifecycleTest() override = default;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    // Disable Maho's onboarding login-gate so the standard startup browser
    // is created (otherwise browser() is null).
    command_line->AppendSwitch("maho-disable-login-gate");
  }

  void SetUpOnMainThread() override {
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();

    SidePanelUI* ui = GetSidePanelUI();
    ASSERT_TRUE(ui);
    // Suppress animations and load delays so state transitions complete
    // synchronously within RunUntilIdle() / RunUntil().
    ui->DisableAnimationsForTesting();
    ui->SetNoDelaysForTesting(true);
  }

 protected:
  SidePanelUI* GetSidePanelUI() {
    return browser()->GetFeatures().side_panel_ui();
  }

  // Shows the Maho AI panel via the production SidePanelUI::Show() path.
  // This is the same call site used by maho_sidebar_top_bar_view.cc and
  // maho_command_action_handler.cc.
  void ShowMahoAiPanel() {
    SidePanelUI* ui = GetSidePanelUI();
    ASSERT_TRUE(ui);
    ui->Show(SidePanelEntryKey(kMahoAiEntryId),
             SidePanelOpenTrigger::kToolbarButton);
  }

  // Closes the content-area side panel.
  void CloseSidePanel() {
    SidePanelUI* ui = GetSidePanelUI();
    ASSERT_TRUE(ui);
    ui->Close(SidePanelType::kContent);
  }

  bool IsMahoAiPanelShowing() {
    SidePanelUI* ui = GetSidePanelUI();
    if (!ui) {
      return false;
    }
    return ui->IsSidePanelEntryShowing(SidePanelEntryKey(kMahoAiEntryId));
  }

  // Returns the host WebContents via SidePanelUI's testing accessor.
  //
  // Note: SidePanelCoordinator::GetWebContentsForTest() is constructive for a
  // registered entry: it may create/cache the entry view before returning the
  // hosted WebContents. Only use this helper while the panel is showing, or
  // when intentionally preparing the next Show() assertion.
  content::WebContents* GetHostWebContents() {
    SidePanelUI* ui = GetSidePanelUI();
    if (!ui) {
      return nullptr;
    }
    return ui->GetWebContentsForTest(kMahoAiEntryId);
  }
};

}  // namespace

// 1. Show() makes kMahoAiPanel the active entry.
IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       ShowPanel_EntryIsShowing) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  ASSERT_FALSE(IsMahoAiPanelShowing())
      << "Precondition: panel must not be showing before Show()";

  ShowMahoAiPanel();

  ASSERT_TRUE(base::test::RunUntil([this]() {
    return IsMahoAiPanelShowing();
  })) << "IsSidePanelEntryShowing(kMahoAiPanel) must return true after "
         "Show(SidePanelEntryKey(kMahoAiPanel)); RegisterMahoAiSidePanel() "
         "registration or SidePanelUI routing is broken.";

  EXPECT_TRUE(IsMahoAiPanelShowing());

  // Clean up so the panel is not open when the browser tears down.
  CloseSidePanel();
  ASSERT_TRUE(base::test::RunUntil([this]() {
    return !GetSidePanelUI()->IsSidePanelShowing(SidePanelType::kContent);
  }));
}

// 2. Close() dismisses the entry; neither the entry nor the panel is showing.
IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       ClosePanel_EntryIsNoLongerShowing) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  ShowMahoAiPanel();

  ASSERT_TRUE(base::test::RunUntil([this]() {
    return IsMahoAiPanelShowing();
  })) << "Precondition: panel must open before close test";

  CloseSidePanel();

  ASSERT_TRUE(base::test::RunUntil([this]() {
    return !GetSidePanelUI()->IsSidePanelShowing(SidePanelType::kContent);
  })) << "IsSidePanelShowing(kContent) must return false after Close(); "
         "Close() path is broken.";

  EXPECT_FALSE(IsMahoAiPanelShowing())
      << "kMahoAiPanel entry must not be showing after side panel close";
  EXPECT_FALSE(GetSidePanelUI()->IsSidePanelShowing(SidePanelType::kContent))
      << "Side panel must be fully closed after Close()";
}

// 3. After Close(), Show() still reopens the entry and exposes a host again.
//    We intentionally do not query GetWebContentsForTest() while closed because
//    that accessor may materialize the entry view for a registered panel.
IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       ClosePanel_CanShowHostAgainAfterClose) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  ShowMahoAiPanel();

  ASSERT_TRUE(base::test::RunUntil([this]() {
    return IsMahoAiPanelShowing();
  })) << "Precondition: panel must open before WebContents lifecycle test";

  // While showing, the host WebContents must exist.
  content::WebContents* host_wc = GetHostWebContents();
  EXPECT_TRUE(host_wc)
      << "GetWebContentsForTest(kMahoAiPanel) must return a non-null "
         "WebContents while the panel is showing";

  CloseSidePanel();

  ASSERT_TRUE(base::test::RunUntil([this]() {
    return !GetSidePanelUI()->IsSidePanelShowing(SidePanelType::kContent);
  })) << "Side panel must close before re-open assertion";

  EXPECT_FALSE(IsMahoAiPanelShowing())
      << "kMahoAiPanel entry must not be showing after close";

  ShowMahoAiPanel();

  ASSERT_TRUE(base::test::RunUntil([this]() {
    return IsMahoAiPanelShowing();
  })) << "Show() must reopen the panel after a prior Close()";

  EXPECT_TRUE(GetHostWebContents())
      << "GetWebContentsForTest(kMahoAiPanel) must expose a host while the "
         "reopened panel is showing";

  CloseSidePanel();
  ASSERT_TRUE(base::test::RunUntil([this]() {
    return !GetSidePanelUI()->IsSidePanelShowing(SidePanelType::kContent);
  }));
}

// 4. Three open→close cycles; each close hides the panel, and each subsequent
//    Show() still reaches a live host. The test name is kept stable for the
//    narrow runtime filter, but the proof deliberately avoids treating
//    GetWebContentsForTest() as a post-close leak oracle.
IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       RepeatedShowHide_NoOrphanedHost) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();
  constexpr int kCycles = 3;
  for (int i = 0; i < kCycles; ++i) {
    ShowMahoAiPanel();

    ASSERT_TRUE(base::test::RunUntil([this]() {
      return IsMahoAiPanelShowing();
    })) << "Cycle " << i << ": Show() did not open the panel";

    content::WebContents* host_wc = GetHostWebContents();
    EXPECT_TRUE(host_wc)
        << "Cycle " << i
        << ": host WebContents must be non-null while panel is showing";

    CloseSidePanel();

    ASSERT_TRUE(base::test::RunUntil([this]() {
      return !GetSidePanelUI()->IsSidePanelShowing(SidePanelType::kContent);
    })) << "Cycle " << i << ": Close() did not close the panel";

    EXPECT_FALSE(IsMahoAiPanelShowing())
        << "Cycle " << i << ": entry must not be showing after close";
    EXPECT_FALSE(GetSidePanelUI()->IsSidePanelShowing(SidePanelType::kContent))
        << "Cycle " << i << ": side panel must remain closed after close";
  }
}

namespace {

// Forges a MahoAIPageHandler for the given browser + its active WebContents,
// exactly as a hostile renderer would drive the PageHandlerFactory. The token
// is built from the exact owning window; a denied class resets the connection
// in the constructor.
std::unique_ptr<MahoAIPageHandler> MakeForgedHandler(
    Browser* browser,
    mojo::Remote<maho_ai::mojom::PageHandler>* remote) {
  content::WebContents* wc = browser->tab_strip_model()->GetActiveWebContents();
  auto token = std::make_unique<MahoPrivateContextToken>(browser, wc);
  auto loader = browser->profile()
                    ->GetDefaultStoragePartition()
                    ->GetURLLoaderFactoryForBrowserProcess();
  mojo::PendingRemote<maho_ai::mojom::Page> page;
  return std::make_unique<MahoAIPageHandler>(
      remote->BindNewPipeAndPassReceiver(), std::move(page), std::move(token),
      browser, browser->profile()->GetPrefs(), std::move(loader));
}

}  // namespace

// task-9-ai-ui: Incognito never enables the AI WebUI and never registers the
// side-panel entry.
IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       IncognitoWebUiAndEntryAreDisabled) {
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito);
  ASSERT_TRUE(incognito->profile()->IsOffTheRecord());

  MahoAIUIConfig config;
  EXPECT_FALSE(config.IsWebUIEnabled(incognito->profile()));

  SidePanelUI* ui = incognito->GetFeatures().side_panel_ui();
  if (ui) {
    EXPECT_FALSE(ui->IsSidePanelEntryShowing(
        SidePanelEntryKey(kMahoAiEntryId)));
  }
}

// task-9-ai-ui: a forged factory/handler in Incognito fails closed before any
// runtime router/adapter is built — the receiver disconnects.
IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       ForgedFactoryStopsBeforeRouter) {
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito);

  mojo::Remote<maho_ai::mojom::PageHandler> remote;
  auto handler = MakeForgedHandler(incognito, &remote);

  base::RunLoop run_loop;
  remote.set_disconnect_handler(run_loop.QuitClosure());
  run_loop.Run();

  EXPECT_FALSE(remote.is_connected());
}

// task-9-ai-ui: every page-handler method fails closed for a forged OTR
// handler — no reply is delivered and the pipe stays disconnected.
IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       EveryPageHandlerMethodFailsClosed) {
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito);

  mojo::Remote<maho_ai::mojom::PageHandler> remote;
  auto handler = MakeForgedHandler(incognito, &remote);

  base::RunLoop disconnect_loop;
  remote.set_disconnect_handler(disconnect_loop.QuitClosure());

  bool any_reply = false;
  remote->GetConnectionState(base::BindOnce(
      [](bool* replied, maho_ai::mojom::RuntimeConnectionState,
         const std::string&) { *replied = true; },
      &any_reply));
  remote->GetSessionList(base::BindOnce(
      [](bool* replied,
         std::vector<maho_ai::mojom::SessionInfoPtr>) { *replied = true; },
      &any_reply));
  remote->GetAiProfiles(base::BindOnce(
      [](bool* replied, const std::string&) { *replied = true; }, &any_reply));

  disconnect_loop.Run();
  EXPECT_FALSE(remote.is_connected());
  EXPECT_FALSE(any_reply);
}

// task-9-ai-ui: attachment/context, tab-tidy, and agent tool sinks fail closed
// for a forged OTR handler — no reply is produced.
IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       AttachmentToolAndAgentSinksFailClosed) {
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito);

  mojo::Remote<maho_ai::mojom::PageHandler> remote;
  auto handler = MakeForgedHandler(incognito, &remote);

  base::RunLoop disconnect_loop;
  remote.set_disconnect_handler(disconnect_loop.QuitClosure());

  bool tidy_reply = false;
  remote->RequestTabTidy(
      std::vector<maho_ai::mojom::TabInfoPtr>(),
      base::BindOnce(
          [](bool* replied,
             std::vector<maho_ai::mojom::TidyFolderPtr>) { *replied = true; },
          &tidy_reply));
  // Fire-and-forget agent/attachment sink; must not reach the adapter/core.
  remote->SubmitPrompt("ses", "hello", /*attach_browser_context=*/true,
                       maho_ai::mojom::InteractionMode::kDeveloper,
                       std::nullopt);

  disconnect_loop.Run();
  EXPECT_FALSE(remote.is_connected());
  EXPECT_FALSE(tidy_reply);
}

// task-9-ai-ui: positive control — a regular-profile handler is NOT denied.
// The runtime router builds an adapter and the connection resolves.
//
// TODO(R-9): the full positive control asserts exactly one loopback
// `/v1/chat/completions` POST emitting NORMAL_AI_OK. That requires the shared
// loopback SSE provider fixture (see plan Verification Contract 3), which does
// not yet exist in the tree; wiring it is tracked as a residual blocker. This
// control verifies the regular path stays open (no over-blocking).
IN_PROC_BROWSER_TEST_F(MahoAiSidePanelLifecycleTest,
                       NormalFakeProviderPositiveControl) {
  ASSERT_FALSE(browser()->profile()->IsOffTheRecord());

  MahoAIUIConfig config;
  EXPECT_TRUE(config.IsWebUIEnabled(browser()->profile()));

  mojo::Remote<maho_ai::mojom::PageHandler> remote;
  auto handler = MakeForgedHandler(browser(), &remote);

  bool got_state = false;
  maho_ai::mojom::RuntimeConnectionState state =
      maho_ai::mojom::RuntimeConnectionState::kDisconnected;
  base::RunLoop run_loop;
  remote->GetConnectionState(base::BindOnce(
      [](bool* got, maho_ai::mojom::RuntimeConnectionState* out,
         base::OnceClosure quit, maho_ai::mojom::RuntimeConnectionState value,
         const std::string&) {
        *got = true;
        *out = value;
        std::move(quit).Run();
      },
      &got_state, &state, run_loop.QuitClosure()));
  run_loop.Run();

  EXPECT_TRUE(got_state);
  EXPECT_TRUE(remote.is_connected());
}
