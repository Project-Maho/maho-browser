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
#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_key.h"
#include "chrome/browser/ui/side_panel/side_panel_enums.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
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
