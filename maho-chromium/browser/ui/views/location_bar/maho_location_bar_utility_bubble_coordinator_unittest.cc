// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_bubble_coordinator.h"

#include <memory>

#include "base/functional/bind.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"

namespace maho {
namespace {

// Minimal stub so tests can exercise OnTabStripModelChanged without a real
// Browser or TabStripModel.  Passes non-null but distinct WebContents pointers
// so that TabStripSelectionChange::active_tab_changed() returns true.
TabStripSelectionChange MakeActiveTabChangedSelection() {
  TabStripSelectionChange sel;
  // Using distinct non-null sentinel addresses for old/new contents so that
  // active_tab_changed() == (old_contents != new_contents) evaluates to true.
  // The coordinator only reads active_tab_changed(); it never dereferences the
  // WebContents pointers in OnTabStripModelChanged.
  static int sentinel_a = 0;
  static int sentinel_b = 0;
  sel.old_contents = reinterpret_cast<content::WebContents*>(&sentinel_a);
  sel.new_contents = reinterpret_cast<content::WebContents*>(&sentinel_b);
  return sel;
}

TabStripSelectionChange MakeSameTabSelection() {
  TabStripSelectionChange sel;
  static int sentinel = 0;
  sel.old_contents = reinterpret_cast<content::WebContents*>(&sentinel);
  sel.new_contents = reinterpret_cast<content::WebContents*>(&sentinel);
  return sel;
}

class MahoLocationBarUtilityBubbleCoordinatorTest
    : public views::ViewsTestBase {
 protected:
  MahoLocationBarUtilityBubbleCoordinatorTest() = default;
  ~MahoLocationBarUtilityBubbleCoordinatorTest() override = default;

  MahoLocationBarUtilityBubbleCoordinator& coordinator() {
    return coordinator_;
  }

  void SimulateWidgetDestroyed(views::Widget* widget) {
    coordinator_.OnWidgetDestroying(widget);
  }

  void SimulateActiveTabChanged() {
    TabStripModelChange change;
    TabStripSelectionChange sel = MakeActiveTabChangedSelection();
    coordinator_.OnTabStripModelChanged(nullptr, change, sel);
  }

  void SimulateSameTabSelection() {
    TabStripModelChange change;
    TabStripSelectionChange sel = MakeSameTabSelection();
    coordinator_.OnTabStripModelChanged(nullptr, change, sel);
  }

  void SimulateTabStripModelDestroyed() {
    coordinator_.OnTabStripModelDestroyed(nullptr);
  }

 private:
  MahoLocationBarUtilityBubbleCoordinator coordinator_;
};

TEST_F(MahoLocationBarUtilityBubbleCoordinatorTest,
       IsNotShowingInitially) {
  EXPECT_FALSE(coordinator().IsShowing());
  EXPECT_EQ(coordinator().GetBubble(), nullptr);
}

TEST_F(MahoLocationBarUtilityBubbleCoordinatorTest,
       HideBeforeShowIsNoOp) {
  ASSERT_FALSE(coordinator().IsShowing());
  coordinator().Hide();
  EXPECT_FALSE(coordinator().IsShowing());
}

TEST_F(MahoLocationBarUtilityBubbleCoordinatorTest,
       ShowBubbleWithNullAnchorIsNoOp) {
  MahoLocationBarUtilityPanelModel model;
  coordinator().ShowBubble(nullptr, model);
  EXPECT_FALSE(coordinator().IsShowing());
}

TEST_F(MahoLocationBarUtilityBubbleCoordinatorTest,
       ShowBubbleWithValidAnchorSetsShowingState) {
  std::unique_ptr<views::Widget> anchor_widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  anchor_widget->Show();

  MahoLocationBarUtilityPanelModel model;
  coordinator().ShowBubble(anchor_widget->GetRootView(), model);

  EXPECT_TRUE(coordinator().IsShowing());
  EXPECT_NE(coordinator().GetBubble(), nullptr);

  coordinator().Hide();
  RunPendingMessages();
}

TEST_F(MahoLocationBarUtilityBubbleCoordinatorTest,
       HideAfterShowClearsStateAfterWidgetCloses) {
  std::unique_ptr<views::Widget> anchor_widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  anchor_widget->Show();

  MahoLocationBarUtilityPanelModel model;
  coordinator().ShowBubble(anchor_widget->GetRootView(), model);
  ASSERT_TRUE(coordinator().IsShowing());

  coordinator().Hide();
  RunPendingMessages();

  EXPECT_FALSE(coordinator().IsShowing());
  EXPECT_EQ(coordinator().GetBubble(), nullptr);
}

TEST_F(MahoLocationBarUtilityBubbleCoordinatorTest,
       OnWidgetDestroyingClearsShowingStateAndFiresCallbacks) {
  std::unique_ptr<views::Widget> anchor_widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  anchor_widget->Show();

  int close_callback_count = 0;
  auto subscription = coordinator().RegisterOnCloseCallback(
      base::BindRepeating([](int* count) { ++(*count); }, &close_callback_count));

  MahoLocationBarUtilityPanelModel model;
  coordinator().ShowBubble(anchor_widget->GetRootView(), model);
  ASSERT_TRUE(coordinator().IsShowing());

  coordinator().Hide();
  RunPendingMessages();

  EXPECT_FALSE(coordinator().IsShowing());
  EXPECT_EQ(coordinator().GetBubble(), nullptr);
  EXPECT_EQ(close_callback_count, 1);
}

TEST_F(MahoLocationBarUtilityBubbleCoordinatorTest,
       OnWidgetDestroyingWhenNotShowingIsNoOp) {
  int close_callback_count = 0;
  auto subscription = coordinator().RegisterOnCloseCallback(
      base::BindRepeating([](int* count) { ++(*count); }, &close_callback_count));

  coordinator().OnWidgetDestroying(nullptr);
  EXPECT_EQ(close_callback_count, 0);
}

TEST_F(MahoLocationBarUtilityBubbleCoordinatorTest,
       RegisterOnCloseCallbackCancelledBeforeClose) {
  std::unique_ptr<views::Widget> anchor_widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  anchor_widget->Show();

  int count = 0;
  base::CallbackListSubscription sub =
      coordinator().RegisterOnCloseCallback(
          base::BindRepeating([](int* c) { ++(*c); }, &count));

  MahoLocationBarUtilityPanelModel model;
  coordinator().ShowBubble(anchor_widget->GetRootView(), model);
  ASSERT_TRUE(coordinator().IsShowing());

  sub = {};

  coordinator().Hide();
  RunPendingMessages();

  EXPECT_EQ(count, 0);
}

TEST_F(MahoLocationBarUtilityBubbleCoordinatorTest,
       MultipleSubscriptionsAllFiredOnClose) {
  std::unique_ptr<views::Widget> anchor_widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  anchor_widget->Show();

  int count_a = 0;
  int count_b = 0;
  auto sub_a = coordinator().RegisterOnCloseCallback(
      base::BindRepeating([](int* c) { ++(*c); }, &count_a));
  auto sub_b = coordinator().RegisterOnCloseCallback(
      base::BindRepeating([](int* c) { ++(*c); }, &count_b));

  MahoLocationBarUtilityPanelModel model;
  coordinator().ShowBubble(anchor_widget->GetRootView(), model);
  ASSERT_TRUE(coordinator().IsShowing());

  coordinator().Hide();
  RunPendingMessages();

  EXPECT_EQ(count_a, 1);
  EXPECT_EQ(count_b, 1);
}

TEST_F(MahoLocationBarUtilityBubbleCoordinatorTest,
       ActiveTabChangedDismissesBubble) {
  std::unique_ptr<views::Widget> anchor_widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  anchor_widget->Show();

  MahoLocationBarUtilityPanelModel model;
  coordinator().ShowBubble(anchor_widget->GetRootView(), model);
  ASSERT_TRUE(coordinator().IsShowing());

  SimulateActiveTabChanged();
  RunPendingMessages();

  EXPECT_FALSE(coordinator().IsShowing());
  EXPECT_EQ(coordinator().GetBubble(), nullptr);
}

TEST_F(MahoLocationBarUtilityBubbleCoordinatorTest,
       ActiveTabChangedFiresCloseCallbacks) {
  std::unique_ptr<views::Widget> anchor_widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  anchor_widget->Show();

  int close_count = 0;
  auto sub = coordinator().RegisterOnCloseCallback(
      base::BindRepeating([](int* c) { ++(*c); }, &close_count));

  MahoLocationBarUtilityPanelModel model;
  coordinator().ShowBubble(anchor_widget->GetRootView(), model);
  ASSERT_TRUE(coordinator().IsShowing());

  SimulateActiveTabChanged();
  RunPendingMessages();

  EXPECT_FALSE(coordinator().IsShowing());
  EXPECT_EQ(close_count, 1);
}

TEST_F(MahoLocationBarUtilityBubbleCoordinatorTest,
       SameTabSelectionChangeDoesNotDismissBubble) {
  std::unique_ptr<views::Widget> anchor_widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  anchor_widget->Show();

  MahoLocationBarUtilityPanelModel model;
  coordinator().ShowBubble(anchor_widget->GetRootView(), model);
  ASSERT_TRUE(coordinator().IsShowing());

  SimulateSameTabSelection();

  EXPECT_TRUE(coordinator().IsShowing());

  coordinator().Hide();
  RunPendingMessages();
}

TEST_F(MahoLocationBarUtilityBubbleCoordinatorTest,
       ActiveTabChangedWhenNotShowingIsNoOp) {
  ASSERT_FALSE(coordinator().IsShowing());

  int close_count = 0;
  auto sub = coordinator().RegisterOnCloseCallback(
      base::BindRepeating([](int* c) { ++(*c); }, &close_count));

  SimulateActiveTabChanged();

  EXPECT_FALSE(coordinator().IsShowing());
  EXPECT_EQ(close_count, 0);
}

TEST_F(MahoLocationBarUtilityBubbleCoordinatorTest,
       TabStripModelDestroyedDismissesBubble) {
  std::unique_ptr<views::Widget> anchor_widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  anchor_widget->Show();

  MahoLocationBarUtilityPanelModel model;
  coordinator().ShowBubble(anchor_widget->GetRootView(), model);
  ASSERT_TRUE(coordinator().IsShowing());

  SimulateTabStripModelDestroyed();
  RunPendingMessages();

  EXPECT_FALSE(coordinator().IsShowing());
}

TEST_F(MahoLocationBarUtilityBubbleCoordinatorTest,
       DetachFromBrowserIsIdempotent) {
  coordinator().DetachFromBrowser();
  coordinator().DetachFromBrowser();
  EXPECT_FALSE(coordinator().IsShowing());
}

}  // namespace
}  // namespace maho
