// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/tabs/maho_ctrl_tab_switcher_view.h"

#include <memory>
#include <vector>

#include "content/public/browser/web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/test/views_test_base.h"

namespace maho {
namespace {

class MahoCtrlTabSwitcherViewTest : public views::ViewsTestBase {
 public:
  std::vector<content::WebContents*> NullTabs(size_t count) {
    return std::vector<content::WebContents*>(count, nullptr);
  }
};

TEST_F(MahoCtrlTabSwitcherViewTest, BuildsOneCardPerVisibleTab) {
  MahoCtrlTabSwitcherView view(NullTabs(2), /*initial_selected_index=*/0,
                               /*max_visible=*/7);
  EXPECT_EQ(2u, view.card_count());
  EXPECT_FALSE(view.has_more_affordance());
  EXPECT_EQ(2u, view.total_tab_count());
}

TEST_F(MahoCtrlTabSwitcherViewTest, CapsCardsAndAppendsMoreAffordance) {
  MahoCtrlTabSwitcherView view(NullTabs(5), /*initial_selected_index=*/1,
                               /*max_visible=*/3);
  EXPECT_EQ(3u, view.card_count());
  EXPECT_TRUE(view.has_more_affordance());
  EXPECT_EQ(3u, view.more_affordance_index());
  EXPECT_EQ(5u, view.total_tab_count());
}

TEST_F(MahoCtrlTabSwitcherViewTest, SelectedIndexClampsToLastSlot) {
  MahoCtrlTabSwitcherView view(NullTabs(5), /*initial_selected_index=*/99,
                               /*max_visible=*/3);
  EXPECT_EQ(3u, view.selected_index());
}

TEST_F(MahoCtrlTabSwitcherViewTest, SetSelectedIndexRejectsOutOfRange) {
  MahoCtrlTabSwitcherView view(NullTabs(2), /*initial_selected_index=*/0,
                               /*max_visible=*/7);
  view.SetSelectedIndex(1);
  EXPECT_EQ(1u, view.selected_index());
  view.SetSelectedIndex(50);
  EXPECT_EQ(1u, view.selected_index());
}

TEST_F(MahoCtrlTabSwitcherViewTest, MoreAffordanceSlotHasNoWebContents) {
  MahoCtrlTabSwitcherView view(NullTabs(5), /*initial_selected_index=*/0,
                               /*max_visible=*/3);
  EXPECT_EQ(nullptr, view.GetWebContentsAt(3));
  EXPECT_EQ(nullptr, view.GetWebContentsAt(99));
}

TEST_F(MahoCtrlTabSwitcherViewTest, SingleTabHasNoAffordance) {
  MahoCtrlTabSwitcherView view(NullTabs(1), /*initial_selected_index=*/0,
                               /*max_visible=*/7);
  EXPECT_EQ(1u, view.card_count());
  EXPECT_FALSE(view.has_more_affordance());
}

}  // namespace
}  // namespace maho
