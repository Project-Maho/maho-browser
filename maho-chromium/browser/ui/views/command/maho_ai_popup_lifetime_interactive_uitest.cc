// Copyright 2026 Maho Browser. All rights reserved.

#include <utility>

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"

// These desktop fixtures use CreateBrowserWindow, whose non-Android factory
// creates Browser instances. Borrowing the concrete pointer does not own it.
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "maho/browser/maho_ai_popup_lifetime_tracker.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

namespace {

class MahoAiPopupLifetimeTrackerTest : public InProcessBrowserTest {
 public:
  MahoAiPopupLifetimeTrackerTest() = default;
  ~MahoAiPopupLifetimeTrackerTest() override = default;

  void SetUpOnMainThread() override {
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
  }

 protected:
  Browser* CreateAiFloatingPopup(Browser* opener) {
    BrowserWindowCreateParams popup_params(Browser::TYPE_POPUP,
                                       opener->GetProfile(),
                                       /*user_gesture=*/true);
    popup_params.is_trusted_source = true;
    popup_params.omit_from_session_restore = true;
    Browser* popup = static_cast<Browser*>(CreateBrowserWindow(std::move(popup_params)));
    chrome::AddTabAt(popup, GURL("chrome://maho-ai/"),
                     /*index=*/-1, /*foreground=*/true);
    maho::MahoAiPopupLifetimeTracker::Get()->TrackPopup(opener, popup);
    popup->GetWindow()->Show();
    return popup;
  }

  Browser* CreateNormalBrowser() {
    BrowserWindowCreateParams params(browser()->GetProfile(), /*user_gesture=*/true);
    Browser* b = static_cast<Browser*>(CreateBrowserWindow(std::move(params)));
    chrome::AddTabAt(b, GURL("about:blank"), /*index=*/-1, /*foreground=*/true);
    b->GetWindow()->Show();
    return b;
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(MahoAiPopupLifetimeTrackerTest,
                       OpenerClose_CascadesToPopup) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  const size_t initial_count =
      GlobalBrowserCollection::GetInstance()->GetSize();

  Browser* opener = CreateNormalBrowser();
  ASSERT_TRUE(opener);

  Browser* popup = CreateAiFloatingPopup(opener);
  ASSERT_TRUE(popup);

  ASSERT_EQ(GlobalBrowserCollection::GetInstance()->GetSize(),
            initial_count + 2);

  EXPECT_EQ(
      maho::MahoAiPopupLifetimeTracker::Get()->GetPopupForOpener(opener),
      popup);
  EXPECT_EQ(
      maho::MahoAiPopupLifetimeTracker::Get()->GetOpenerForPopup(popup),
      opener);

  CloseBrowserSynchronously(opener);

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return GlobalBrowserCollection::GetInstance()->GetSize() <= initial_count;
  })) << "Popup must be closed when opener is closed";

  EXPECT_EQ(GlobalBrowserCollection::GetInstance()->GetSize(), initial_count)
      << "Harness browser must still be alive";
}

IN_PROC_BROWSER_TEST_F(MahoAiPopupLifetimeTrackerTest,
                       PopupCloseFirst_MappingCleaned) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  Browser* popup = CreateAiFloatingPopup(static_cast<Browser*>(browser()));
  ASSERT_TRUE(popup);

  EXPECT_EQ(
      maho::MahoAiPopupLifetimeTracker::Get()->GetPopupForOpener(static_cast<Browser*>(browser())),
      popup);
  EXPECT_EQ(
      maho::MahoAiPopupLifetimeTracker::Get()->GetOpenerForPopup(popup),
      browser());
  EXPECT_EQ(
      maho::MahoAiPopupLifetimeTracker::Get()->GetOpenerForPopup(nullptr),
      nullptr);

  CloseBrowserSynchronously(popup);

  EXPECT_EQ(
      maho::MahoAiPopupLifetimeTracker::Get()->GetPopupForOpener(static_cast<Browser*>(browser())),
      nullptr);
  EXPECT_EQ(
      maho::MahoAiPopupLifetimeTracker::Get()->GetOpenerForPopup(popup),
      nullptr);
}
