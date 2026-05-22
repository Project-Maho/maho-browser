// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/link_preview/maho_link_preview_manager.h"

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace {

class WidgetDestroyedWaiter : public views::WidgetObserver {
 public:
  explicit WidgetDestroyedWaiter(views::Widget* widget) {
    observation_.Observe(widget);
  }

  void OnWidgetDestroyed(views::Widget* widget) override {
    destroyed_ = true;
    observation_.Reset();
  }

  bool destroyed() const { return destroyed_; }

 private:
  bool destroyed_ = false;
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{
      this};
};

class MahoLinkPreviewManagerInteractiveUiTest : public InProcessBrowserTest {
 public:
  MahoLinkPreviewManagerInteractiveUiTest() = default;
  ~MahoLinkPreviewManagerInteractiveUiTest() override = default;

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());

    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("/title1.html")));
    base::RunLoop().RunUntilIdle();

    content::WebContents* wc =
        browser()->tab_strip_model()->GetActiveWebContents();
    ASSERT_TRUE(wc);
    MahoLinkPreviewManager::CreateForWebContents(wc);
    manager_ = MahoLinkPreviewManager::FromWebContents(wc);
    ASSERT_TRUE(manager_);
  }

 protected:
  MahoLinkPreviewManager* manager() { return manager_; }

  content::WebContents* web_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  static gfx::Rect AnchorRect() { return gfx::Rect(100, 100, 120, 20); }

 private:
  raw_ptr<MahoLinkPreviewManager> manager_ = nullptr;
};

}  // namespace

IN_PROC_BROWSER_TEST_F(MahoLinkPreviewManagerInteractiveUiTest,
                       HoverEventShowsLinkPreviewBubble) {
  EXPECT_FALSE(manager()->bubble_widget_for_testing());

  manager()->HandleLinkHoverForTesting("https://example.com", "Example",
                                       AnchorRect());
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return manager()->bubble_widget_for_testing() != nullptr;
  }));
  views::Widget* widget = manager()->bubble_widget_for_testing();
  ASSERT_TRUE(widget);
  EXPECT_TRUE(widget->IsVisible());
}

IN_PROC_BROWSER_TEST_F(MahoLinkPreviewManagerInteractiveUiTest,
                       DismissClosesLinkPreviewBubble) {
  manager()->HandleLinkHoverForTesting("https://example.com", "Example",
                                       AnchorRect());
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return manager()->bubble_widget_for_testing() != nullptr;
  }));
  views::Widget* widget = manager()->bubble_widget_for_testing();
  ASSERT_TRUE(widget);

  WidgetDestroyedWaiter waiter(widget);

  manager()->HandleLinkDismissForTesting();
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(manager()->bubble_widget_for_testing());
  ASSERT_TRUE(base::test::RunUntil([&]() { return waiter.destroyed(); }));
}

IN_PROC_BROWSER_TEST_F(MahoLinkPreviewManagerInteractiveUiTest,
                       NavigationClosesBubbleAndPreventsStaleRetention) {
  manager()->HandleLinkHoverForTesting("https://example.com", "Example",
                                       AnchorRect());
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return manager()->bubble_widget_for_testing() != nullptr;
  }));

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title2.html")));
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return manager()->bubble_widget_for_testing() == nullptr;
  }));
  EXPECT_FALSE(manager()->bubble_widget_for_testing());
}
