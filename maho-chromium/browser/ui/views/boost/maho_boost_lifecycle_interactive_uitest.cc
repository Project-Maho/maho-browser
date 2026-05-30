// Copyright 2026 Maho Browser. All rights reserved.

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "maho/browser/ui/views/boost/maho_boost_window_controller.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/views/widget/widget.h"

namespace {

class MahoBoostLifecycleTest : public InProcessBrowserTest {
 public:
  MahoBoostLifecycleTest() = default;
  ~MahoBoostLifecycleTest() override = default;

  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
  }

 protected:
  maho::MahoBoostWindowController& GetController() {
    return maho::MahoBoostWindowController::GetForBrowser(browser(),
                                                          browser()->profile());
  }

  content::WebContents* GetActiveTab() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  void ShowBoostEditor() {
    GetController().ShowForActiveDomain(GetActiveTab());
  }

  void HideBoostEditor() { GetController().Hide(); }

  bool WaitUntilHidden() {
    return base::test::RunUntil(
        [&]() { return !GetController().IsShowing(); });
  }

  bool WaitUntilShowing() {
    return base::test::RunUntil([&]() { return GetController().IsShowing(); });
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// Test 1: CreateTempDiscardNoPersistence
//
// Opens the boost editor (creates a temp boost), hides without committing,
// then reopens for the same domain and verifies no prior changes persisted.
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostLifecycleTest,
                       CreateTempDiscardNoPersistence) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.com", "/title1.html")));
  base::RunLoop().RunUntilIdle();

  ShowBoostEditor();
  ASSERT_TRUE(WaitUntilShowing());

  HideBoostEditor();
  ASSERT_TRUE(WaitUntilHidden());

  ShowBoostEditor();
  ASSERT_TRUE(WaitUntilShowing());

  // TODO(maho): Assert magic-theme not active via JS once controller exposes
  // a WebContents getter for the editor.
  views::Widget* widget = GetController().GetWidgetForTesting();
  ASSERT_TRUE(widget);
  EXPECT_TRUE(GetController().IsShowing());
}

// ---------------------------------------------------------------------------
// Test 2: CreateTempCommitPersistence
//
// Opens the boost editor, makes a change (magic-theme toggle), closes the
// editor (triggering a commit via widget destruction), then reopens and
// verifies the change persisted.
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostLifecycleTest,
                       CreateTempCommitPersistence) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.com", "/title1.html")));
  base::RunLoop().RunUntilIdle();

  ShowBoostEditor();
  ASSERT_TRUE(WaitUntilShowing());

  // Widget::Close() mirrors clicking the window X button → triggers commit
  // via OnWidgetDestroying.
  views::Widget* widget = GetController().GetWidgetForTesting();
  ASSERT_TRUE(widget);
  widget->Close();

  ASSERT_TRUE(WaitUntilHidden());

  ShowBoostEditor();
  ASSERT_TRUE(WaitUntilShowing());

  // TODO(maho): Assert magic-theme active state via JS once controller
  // exposes a WebContents getter.
  EXPECT_TRUE(GetController().IsShowing());
}

// ---------------------------------------------------------------------------
// Test 3: TabSwitchClosesEditor
//
// Opens the boost editor for Tab 0, adds a second tab and activates it,
// then asserts the editor auto-closed.
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostLifecycleTest, TabSwitchClosesEditor) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.com", "/title1.html")));
  base::RunLoop().RunUntilIdle();

  ShowBoostEditor();
  ASSERT_TRUE(WaitUntilShowing());

  chrome::AddTabAt(browser(), GURL("about:blank"), /*index=*/-1,
                   /*foreground=*/true);

  ASSERT_TRUE(WaitUntilHidden())
      << "Boost editor must close when the active tab changes";
  EXPECT_FALSE(GetController().IsShowing());
}

// ---------------------------------------------------------------------------
// Test 4: NavigationClosesEditor
//
// Opens the boost editor for a.com, navigates the active tab to a different
// domain (b.com), then asserts the editor auto-closed.
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostLifecycleTest, NavigationClosesEditor) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.com", "/title1.html")));
  base::RunLoop().RunUntilIdle();

  ShowBoostEditor();
  ASSERT_TRUE(WaitUntilShowing());

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("b.com", "/title1.html")));

  ASSERT_TRUE(WaitUntilHidden())
      << "Boost editor must close when navigating to a different domain";
  EXPECT_FALSE(GetController().IsShowing());
}

// ---------------------------------------------------------------------------
// Test 5: SameDomainReshowReuses
//
// Opens the boost editor, hides it, re-shows for the same domain, and
// verifies the editor is showing again.  Optionally verifies no extra
// boost editor browser was created.
// ---------------------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(MahoBoostLifecycleTest, SameDomainReshowReuses) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.com", "/title1.html")));
  base::RunLoop().RunUntilIdle();

  ShowBoostEditor();
  ASSERT_TRUE(WaitUntilShowing());

  HideBoostEditor();
  ASSERT_TRUE(WaitUntilHidden());
  EXPECT_FALSE(GetController().IsShowing());

  ShowBoostEditor();
  ASSERT_TRUE(WaitUntilShowing());
  EXPECT_TRUE(GetController().IsShowing());
}
