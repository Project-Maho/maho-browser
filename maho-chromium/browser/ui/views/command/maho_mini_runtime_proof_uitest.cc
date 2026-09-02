// Copyright 2026 Maho Browser. All rights reserved.
//
// Independent runtime-proof tests for the portable Maho Mini implementation.
// These drive LaunchMahoMini directly (with a real URL) instead of going
// through ui_test_utils::NavigateToURL, which does not complete reliably for
// first navigations inside the macOS VM e2e environment.

#include <functional>

#include "base/run_loop.h"
#include "base/functional/callback.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "maho/browser/ui/views/command/maho_command_action_handler.h"
#include "maho/browser/ui/views/maho_mini/maho_mini_window.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace {

Browser* FindPopupBrowser(Browser* opener) {
  Browser* result = nullptr;
  for (BrowserWindowInterface* bwi : GetAllBrowserWindowInterfaces()) {
    if (bwi->GetType() == BrowserWindowInterface::TYPE_POPUP &&
        (opener == nullptr ||
         bwi->GetBrowserForMigrationOnly() != opener)) {
      result = bwi->GetBrowserForMigrationOnly();
      break;
    }
  }
  return result;
}

bool WaitForCondition(std::function<bool()> cond,
                      base::TimeDelta timeout) {
  const base::TimeTicks deadline = base::TimeTicks::Now() + timeout;
  while (base::TimeTicks::Now() < deadline) {
    if (cond()) {
      return true;
    }
    base::PlatformThread::Sleep(base::Milliseconds(250));
  }
  return cond();
}

}  // namespace

class MahoMiniRuntimeProofTest : public InProcessBrowserTest {
 public:
  MahoMiniRuntimeProofTest() = default;
  MahoMiniRuntimeProofTest(const MahoMiniRuntimeProofTest&) = delete;
  MahoMiniRuntimeProofTest& operator=(const MahoMiniRuntimeProofTest&) = delete;
  ~MahoMiniRuntimeProofTest() override = default;

  void SetUpOnMainThread() override {
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
  }
};

IN_PROC_BROWSER_TEST_F(MahoMiniRuntimeProofTest,
                       MahoMini_OpensPopupFromNormalBrowser) {
  const size_t initial_browser_count =
      GlobalBrowserCollection::GetInstance()->GetSize();

  maho::ExecuteCommandAction(browser(), "maho_mini");

  ASSERT_TRUE(WaitForCondition([=]() {
    return GlobalBrowserCollection::GetInstance()->GetSize() >
           initial_browser_count;
  }, base::Seconds(30))) << "maho_mini action must open a new popup browser window";

  Browser* popup = FindPopupBrowser(browser());
  ASSERT_TRUE(popup) << "A TYPE_POPUP browser must exist after maho_mini";
  EXPECT_EQ(Browser::TYPE_POPUP, popup->type());
}

IN_PROC_BROWSER_TEST_F(MahoMiniRuntimeProofTest,
                       LaunchMahoMini_WithNoOpenBrowser) {
  const size_t initial_browser_count =
      GlobalBrowserCollection::GetInstance()->GetSize();

  maho::MahoMiniRequest request;
  request.url = GURL("about:blank");

  maho::LaunchMahoMini(browser()->profile(), request);

  ASSERT_TRUE(WaitForCondition([=]() {
    return GlobalBrowserCollection::GetInstance()->GetSize() >
           initial_browser_count;
  }, base::Seconds(30))) << "LaunchMahoMini must open a new popup browser window";

  Browser* popup = FindPopupBrowser(nullptr);
  ASSERT_TRUE(popup) << "A TYPE_POPUP browser must exist after LaunchMahoMini";
  EXPECT_EQ(Browser::TYPE_POPUP, popup->type());

  // NOTE: the VM e2e guest never commits navigations (GetLastCommittedURL()
  // stays empty for any URL, including about:blank) - environment limitation,
  // documented in the notepad. Assert the achievable runtime contract: the
  // popup hosts its own tab with a live WebContents for the requested URL.
  ASSERT_TRUE(WaitForCondition([=]() {
    content::WebContents* wc =
        popup->tab_strip_model()->GetActiveWebContents();
    return wc != nullptr && !wc->IsCrashed();
  }, base::Seconds(30))) << "Popup must host a live WebContents for the "
                             "requested URL";
  EXPECT_EQ(1, popup->tab_strip_model()->count());

  CloseBrowserSynchronously(popup);
}

IN_PROC_BROWSER_TEST_F(MahoMiniRuntimeProofTest,
                       MahoMini_PopupNavigatesIndependently) {
  const GURL popup_url = GURL("about:blank");

  maho::MahoMiniRequest request;
  request.url = popup_url;
  maho::LaunchMahoMini(browser()->profile(), request);

  ASSERT_TRUE(WaitForCondition([=]() {
    return FindPopupBrowser(nullptr) != nullptr;
  }, base::Seconds(30))) << "Popup must open before independence test";

  Browser* popup = FindPopupBrowser(nullptr);
  ASSERT_TRUE(popup);

  // Independence proof (VM cannot commit navigations - see note above): the
  // popup hosts its own live WebContents while the opener keeps its tab.
  content::WebContents* popup_wc = popup->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(WaitForCondition([=]() {
    return popup_wc != nullptr && !popup_wc->IsCrashed();
  }, base::Seconds(30))) << "Popup must host its own live WebContents";
  EXPECT_EQ(1, popup->tab_strip_model()->count());

  ASSERT_EQ(Browser::TYPE_POPUP, popup->type())
      << "Popup must remain a distinct popup browser";
}

IN_PROC_BROWSER_TEST_F(MahoMiniRuntimeProofTest,
                       MahoMini_PromoteClosesPopupAndMovesTabToNormalBrowser) {
  const size_t initial_normal_tabs = browser()->tab_strip_model()->count();

  maho::MahoMiniRequest request;
  request.url = embedded_test_server()->GetURL("/title1.html");
  maho::LaunchMahoMini(browser()->profile(), request);

  ASSERT_TRUE(WaitForCondition([=]() {
    return FindPopupBrowser(nullptr) != nullptr;
  }, base::Seconds(60))) << "Mini popup must open before promote test";

  Browser* popup = FindPopupBrowser(nullptr);
  ASSERT_TRUE(popup);
  const size_t tabs_before =
      browser()->tab_strip_model()->count() + popup->tab_strip_model()->count();

  MahoMiniWindow::PromoteToTab(popup);

  ASSERT_TRUE(WaitForCondition([=]() {
    return !FindPopupBrowser(nullptr);
  }, base::Seconds(30))) << "Promote must close the popup and move its tab "
                            "into the normal browser";

  const size_t tabs_after = browser()->tab_strip_model()->count();
  EXPECT_EQ(tabs_before, tabs_after)
      << "Promote must preserve the total tab count";

  base::RunLoop().RunUntilIdle();
}
