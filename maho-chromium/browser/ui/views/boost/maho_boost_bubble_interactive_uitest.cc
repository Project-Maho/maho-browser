// Copyright 2026 Maho Browser. All rights reserved.

#include <set>

#include "base/memory/raw_ptr.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "maho/browser/ui/site_control/maho_page_info_ui.h"
#include "maho/browser/ui/views/boost/maho_boost_bubble_coordinator.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/window_open_disposition.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace {

class FocusRecorder : public views::FocusChangeListener {
 public:
  explicit FocusRecorder(views::FocusManager* fm) : fm_(fm) {
    fm_->AddFocusChangeListener(this);
  }
  ~FocusRecorder() override { fm_->RemoveFocusChangeListener(this); }

  void OnWillChangeFocus(views::View* focused_before,
                         views::View* focused_now) override {}
  void OnDidChangeFocus(views::View* focused_before,
                        views::View* focused_now) override {
    ever_focused_.insert(focused_now);
  }

  bool EverFocused(views::View* view) const {
    return ever_focused_.count(view) > 0;
  }

 private:
  raw_ptr<views::FocusManager> fm_;
  std::set<raw_ptr<views::View>> ever_focused_;
};

class MahoBoostBubbleInteractiveUiTest : public InProcessBrowserTest {
 public:
  MahoBoostBubbleInteractiveUiTest() = default;
  MahoBoostBubbleInteractiveUiTest(const MahoBoostBubbleInteractiveUiTest&) =
      delete;
  MahoBoostBubbleInteractiveUiTest& operator=(
      const MahoBoostBubbleInteractiveUiTest&) = delete;
  ~MahoBoostBubbleInteractiveUiTest() override = default;

  void SetUpOnMainThread() override {
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
  }

 protected:
  views::View* GetAnchorView() {
    auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
    EXPECT_TRUE(browser_view);
    if (!browser_view) {
      return nullptr;
    }
    return browser_view->toolbar();
  }

  content::WebContents* GetActiveWebContents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  maho::MahoBoostBubbleCoordinator& GetCoordinator() {
    return maho::MahoBoostBubbleCoordinator::GetForBrowser(browser());
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(MahoBoostBubbleInteractiveUiTest,
                       ShowBubbleIsShowingHide) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  browser()->window()->Activate();

  maho::MahoBoostBubbleCoordinator& coordinator = GetCoordinator();
  EXPECT_FALSE(coordinator.IsShowing());

  coordinator.Show(GetAnchorView(), browser(), GetActiveWebContents());

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return coordinator.IsShowing();
  }));
  EXPECT_TRUE(coordinator.IsShowing());

  coordinator.Hide();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !coordinator.IsShowing();
  }));
  EXPECT_FALSE(coordinator.IsShowing());
}

IN_PROC_BROWSER_TEST_F(MahoBoostBubbleInteractiveUiTest,
                       HideBeforeShowIsNoOp) {
  maho::MahoBoostBubbleCoordinator& coordinator = GetCoordinator();
  EXPECT_FALSE(coordinator.IsShowing());

  coordinator.Hide();

  EXPECT_FALSE(coordinator.IsShowing());
}

IN_PROC_BROWSER_TEST_F(MahoBoostBubbleInteractiveUiTest,
                       PageInfoRouteShowsAndHidesBubble) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  browser()->window()->Activate();

  maho::MahoBoostBubbleCoordinator& coordinator = GetCoordinator();
  ASSERT_FALSE(coordinator.IsShowing());

  MahoPageInfoUI::ShowBoostBubble(browser());

  ASSERT_TRUE(base::test::RunUntil([&]() { return coordinator.IsShowing(); }));
  EXPECT_TRUE(coordinator.IsShowing());

  coordinator.Hide();

  ASSERT_TRUE(
      base::test::RunUntil([&]() { return !coordinator.IsShowing(); }));
  EXPECT_FALSE(coordinator.IsShowing());
}

IN_PROC_BROWSER_TEST_F(MahoBoostBubbleInteractiveUiTest,
                       PageInfoRouteEscapeDismissesBubble) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  maho::MahoBoostBubbleCoordinator& coordinator = GetCoordinator();
  ASSERT_FALSE(coordinator.IsShowing());

  MahoPageInfoUI::ShowBoostBubble(browser());

  ASSERT_TRUE(base::test::RunUntil([&]() { return coordinator.IsShowing(); }));
  views::Widget* bubble_widget = coordinator.GetBubbleWidgetForTesting();
  ASSERT_TRUE(bubble_widget);
  ASSERT_TRUE(bubble_widget->IsVisible());

  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      bubble_widget->GetNativeWindow(), ui::VKEY_ESCAPE,
      /*control=*/false, /*shift=*/false, /*alt=*/false,
      /*command=*/false));

  ASSERT_TRUE(
      base::test::RunUntil([&]() { return !coordinator.IsShowing(); }));
  EXPECT_FALSE(coordinator.IsShowing());
}

IN_PROC_BROWSER_TEST_F(MahoBoostBubbleInteractiveUiTest,
                       SecondShowReplacesBubble) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  maho::MahoBoostBubbleCoordinator& coordinator = GetCoordinator();
  coordinator.Show(GetAnchorView(), browser(), GetActiveWebContents());
  ASSERT_TRUE(base::test::RunUntil([&]() { return coordinator.IsShowing(); }));

  coordinator.Show(GetAnchorView(), browser(), GetActiveWebContents());
  ASSERT_TRUE(base::test::RunUntil([&]() { return coordinator.IsShowing(); }));
  EXPECT_TRUE(coordinator.IsShowing());

  coordinator.Hide();
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return !coordinator.IsShowing(); }));
}

// Cross-origin navigation in the observed WebContents must auto-close the
// bubble.
IN_PROC_BROWSER_TEST_F(MahoBoostBubbleInteractiveUiTest,
                       CrossOriginNavigationAutoCloses) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.com", "/title1.html")));
  base::RunLoop().RunUntilIdle();

  maho::MahoBoostBubbleCoordinator& coordinator = GetCoordinator();
  coordinator.Show(GetAnchorView(), browser(), GetActiveWebContents());
  ASSERT_TRUE(base::test::RunUntil([&]() { return coordinator.IsShowing(); }));

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("b.com", "/title1.html")));

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !coordinator.IsShowing();
  }));
  EXPECT_FALSE(coordinator.IsShowing());
}

// Same-origin navigation must NOT auto-close the bubble.
IN_PROC_BROWSER_TEST_F(MahoBoostBubbleInteractiveUiTest,
                       SameOriginNavigationKeepsBubble) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  maho::MahoBoostBubbleCoordinator& coordinator = GetCoordinator();
  coordinator.Show(GetAnchorView(), browser(), GetActiveWebContents());
  ASSERT_TRUE(base::test::RunUntil([&]() { return coordinator.IsShowing(); }));

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title2.html")));
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(coordinator.IsShowing());

  coordinator.Hide();
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return !coordinator.IsShowing(); }));
}

IN_PROC_BROWSER_TEST_F(MahoBoostBubbleInteractiveUiTest,
                       ActiveTabChangeDismissesBubble) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  maho::MahoBoostBubbleCoordinator& coordinator = GetCoordinator();
  coordinator.Show(GetAnchorView(), browser(), GetActiveWebContents());
  ASSERT_TRUE(base::test::RunUntil([&]() { return coordinator.IsShowing(); }));

  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("/title2.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));

  ASSERT_TRUE(
      base::test::RunUntil([&]() { return !coordinator.IsShowing(); }));
  EXPECT_FALSE(coordinator.IsShowing());
}

IN_PROC_BROWSER_TEST_F(MahoBoostBubbleInteractiveUiTest,
                       HideBubbleRestoresFocusToAnchorView) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  browser()->window()->Activate();
  base::RunLoop().RunUntilIdle();

  views::View* anchor = GetAnchorView();
  ASSERT_TRUE(anchor);
  anchor->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  anchor->RequestFocus();
  base::RunLoop().RunUntilIdle();

  maho::MahoBoostBubbleCoordinator& coordinator = GetCoordinator();
  coordinator.Show(anchor, browser(), GetActiveWebContents());
  ASSERT_TRUE(base::test::RunUntil([&]() { return coordinator.IsShowing(); }));

  views::FocusManager* fm = anchor->GetWidget()->GetFocusManager();
  ASSERT_TRUE(fm);
  FocusRecorder recorder(fm);

  coordinator.Hide();
  ASSERT_TRUE(base::test::RunUntil([&]() { return recorder.EverFocused(anchor); }))
      << "Focus must be restored to anchor view after bubble dismiss";
}
