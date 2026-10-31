// Copyright 2026 Maho Browser. All rights reserved.

#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "maho/browser/ui/views/command/maho_command_action_handler.h"
#include "maho/browser/ui/views/maho_mini/maho_mini_top_bar_view.h"
#include "maho/browser/ui/views/maho_mini/maho_mini_window.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/view_observer.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

class PopupCreatedWaiter : public BrowserCollectionObserver {
 public:
  PopupCreatedWaiter() {
    observation_.Observe(GlobalBrowserCollection::GetInstance());
  }
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    if (browser->GetType() == BrowserWindowInterface::TYPE_POPUP) {
      popup_ = browser;
      run_loop_.Quit();
    }
  }
  BrowserWindowInterface* Wait() {
    base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(30));
    if (!popup_) {
      run_loop_.Run();
    }
    return popup_;
  }
 private:
  BrowserWindowInterface* popup_ = nullptr;
  base::RunLoop run_loop_;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      observation_{this};
};

class BoundsChangedWaiter : public views::ViewObserver {
 public:
  explicit BoundsChangedWaiter(views::View* view) { observation_.Observe(view); }
  void OnViewBoundsChanged(views::View* view) override {
    changed_ = true;
    run_loop_.Quit();
  }
  void Wait() {
    base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(30));
    if (!changed_) {
      run_loop_.Run();
    }
  }
 private:
  bool changed_ = false;
  base::RunLoop run_loop_;
  base::ScopedObservation<views::View, views::ViewObserver> observation_{this};
};

void CheckMiniLayout(BrowserWindowInterface* popup) {
  ASSERT_EQ(BrowserWindowInterface::TYPE_POPUP, popup->GetType());
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(popup);
  ASSERT_TRUE(view);
  maho::MahoMiniTopBarView* top_bar = nullptr;
  for (views::View* child : view->children()) {
    if (auto* candidate = views::AsViewClass<maho::MahoMiniTopBarView>(child)) {
      top_bar = candidate;
    }
  }
  ASSERT_TRUE(top_bar);
  views::View* contents = view->contents_container();
  ASSERT_TRUE(contents);
  BoundsChangedWaiter changed(contents);
  gfx::Rect bounds = view->GetWidget()->GetWindowBoundsInScreen();
  bounds.set_width(bounds.width() + 37);
  view->GetWidget()->SetBounds(bounds);
  view->GetWidget()->LayoutRootViewIfNecessary();
  changed.Wait();
  view->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_TRUE(top_bar->GetVisible());
  EXPECT_EQ(46, top_bar->height());
  EXPECT_EQ(0, view->top_container()->height());
  EXPECT_EQ(top_bar->GetBoundsInScreen().bottom(),
            contents->GetBoundsInScreen().y());
  ASSERT_EQ(1, popup->GetTabStripModel()->count());
  auto* web_contents = popup->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(web_contents);
  EXPECT_FALSE(web_contents->IsCrashed());
}

}  // namespace

class MahoMiniRuntimeProofTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(MahoMiniRuntimeProofTest,
                       MahoMini_OpensPopupFromNormalBrowser) {
  PopupCreatedWaiter created;
  maho::ExecuteCommandAction(static_cast<Browser*>(browser()), "maho_mini");
  auto* popup = created.Wait();
  ASSERT_TRUE(popup);
  CheckMiniLayout(popup);
  CloseBrowserSynchronously(popup);
}

IN_PROC_BROWSER_TEST_F(MahoMiniRuntimeProofTest,
                       LaunchMahoMini_WithNoOpenBrowser) {
  Profile* profile = browser()->GetProfile();
  ScopedKeepAlive process_alive(KeepAliveOrigin::BROWSER,
                                KeepAliveRestartOption::DISABLED);
  ScopedProfileKeepAlive profile_alive(profile,
                                       ProfileKeepAliveOrigin::kBrowserWindow);
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(30));
  ASSERT_EQ(1u, GlobalBrowserCollection::GetInstance()->GetSize());
  CloseBrowserSynchronously(browser());
  ASSERT_EQ(0u, GlobalBrowserCollection::GetInstance()->GetSize());
  PopupCreatedWaiter created;
  maho::LaunchMahoMini(profile,
                      maho::MahoMiniRequest{GURL("about:blank")});
  auto* popup = created.Wait();
  ASSERT_TRUE(popup);
  CheckMiniLayout(popup);
  CloseBrowserSynchronously(popup);
}

IN_PROC_BROWSER_TEST_F(MahoMiniRuntimeProofTest,
                       MahoMini_PopupNavigatesIndependently) {
  auto* opener_contents = browser()->GetTabStripModel()->GetActiveWebContents();
  const int opener_tabs = browser()->GetTabStripModel()->count();
  PopupCreatedWaiter created;
  maho::LaunchMahoMini(browser()->GetProfile(),
                      maho::MahoMiniRequest{GURL("about:blank")});
  auto* popup = created.Wait();
  ASSERT_TRUE(popup);
  CheckMiniLayout(popup);
  EXPECT_NE(opener_contents, popup->GetTabStripModel()->GetActiveWebContents());
  EXPECT_EQ(opener_tabs, browser()->GetTabStripModel()->count());
  CloseBrowserSynchronously(popup);
}

IN_PROC_BROWSER_TEST_F(MahoMiniRuntimeProofTest,
                       MahoMini_PromoteClosesPopupAndMovesTabToNormalBrowser) {
  PopupCreatedWaiter created;
  maho::LaunchMahoMini(browser()->GetProfile(),
                      maho::MahoMiniRequest{GURL("about:blank")});
  auto* popup = created.Wait();
  ASSERT_TRUE(popup);
  CheckMiniLayout(popup);
  const int tabs_before = browser()->GetTabStripModel()->count() +
                          popup->GetTabStripModel()->count();
  ASSERT_EQ(browser(),
            ProfileBrowserCollection::GetForProfile(popup->GetProfile())
                ->FindTabbedBrowser(/*match_original_profiles=*/false));
  ui_test_utils::BrowserDestroyedObserver closed(popup);
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(30));
  // LaunchMahoMini uses the non-Android CreateBrowserWindow overload, which
  // returns Browser::Create; the observed popup is therefore a concrete Browser.
  MahoMiniWindow::PromoteToTab(static_cast<Browser*>(popup));
  closed.Wait();
  EXPECT_EQ(tabs_before, browser()->GetTabStripModel()->count());
}
