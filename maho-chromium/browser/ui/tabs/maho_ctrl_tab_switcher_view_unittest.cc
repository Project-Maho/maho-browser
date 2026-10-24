// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/tabs/maho_ctrl_tab_switcher_view.h"

#include <memory>
#include <vector>

#include "content/public/browser/web_contents.h"
#include "maho/browser/ui/tabs/maho_ctrl_tab_switcher_card_view.h"
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
                               /*visible_cards=*/7);
  EXPECT_EQ(2u, view.card_count());
  EXPECT_FALSE(view.has_more_affordance());
  EXPECT_EQ(2u, view.total_tab_count());
}

TEST_F(MahoCtrlTabSwitcherViewTest, CapsCardsAndAppendsMoreAffordance) {
  MahoCtrlTabSwitcherView view(NullTabs(5), /*initial_selected_index=*/1,
                               /*visible_cards=*/3);
  EXPECT_EQ(3u, view.card_count());
  EXPECT_TRUE(view.has_more_affordance());
  EXPECT_EQ(3u, view.more_affordance_index());
  EXPECT_EQ(5u, view.total_tab_count());
}

TEST_F(MahoCtrlTabSwitcherViewTest, SelectedIndexClampsToLastSlot) {
  MahoCtrlTabSwitcherView view(NullTabs(5), /*initial_selected_index=*/99,
                               /*visible_cards=*/3);
  EXPECT_EQ(3u, view.selected_index());
  // The clamped index is the "+N more" slot, and the constructor paints it
  // selected (R3-2).
  EXPECT_TRUE(view.more_affordance_selected());
}

TEST_F(MahoCtrlTabSwitcherViewTest, SetSelectedIndexRejectsOutOfRange) {
  MahoCtrlTabSwitcherView view(NullTabs(2), /*initial_selected_index=*/0,
                               /*visible_cards=*/7);
  view.SetSelectedIndex(1);
  EXPECT_EQ(1u, view.selected_index());
  view.SetSelectedIndex(50);
  EXPECT_EQ(1u, view.selected_index());
}

TEST_F(MahoCtrlTabSwitcherViewTest, MoreAffordanceSlotHasNoWebContents) {
  MahoCtrlTabSwitcherView view(NullTabs(5), /*initial_selected_index=*/0,
                               /*visible_cards=*/3);
  EXPECT_EQ(nullptr, view.GetWebContentsAt(3));
  EXPECT_EQ(nullptr, view.GetWebContentsAt(99));
}

TEST_F(MahoCtrlTabSwitcherViewTest, SingleTabHasNoAffordance) {
  MahoCtrlTabSwitcherView view(NullTabs(1), /*initial_selected_index=*/0,
                               /*visible_cards=*/7);
  EXPECT_EQ(1u, view.card_count());
  EXPECT_FALSE(view.has_more_affordance());
}

TEST_F(MahoCtrlTabSwitcherViewTest, PreferredWidthMatchesLayout) {
  MahoCtrlTabSwitcherView view(NullTabs(10), /*initial_selected_index=*/0,
                               /*visible_cards=*/4);
  ASSERT_TRUE(view.has_more_affordance());
  EXPECT_EQ(MahoCtrlTabSwitcherView::PreferredWidthFor(
                4, true, MahoCtrlTabSwitcherCardView::kCardWidth),
            view.GetPreferredSize().width());
}

TEST_F(MahoCtrlTabSwitcherViewTest, NarrowedCardWidthReachesTheCards) {
  MahoCtrlTabSwitcherView view(NullTabs(5), /*initial_selected_index=*/0,
                               /*visible_cards=*/2, /*card_width=*/150);
  EXPECT_EQ(2u, view.card_count());
  EXPECT_TRUE(view.has_more_affordance());
  EXPECT_EQ(MahoCtrlTabSwitcherView::PreferredWidthFor(2, true, 150),
            view.GetPreferredSize().width());
}

TEST_F(MahoCtrlTabSwitcherViewTest, SingleCardLayoutKeepsTheMoreSlotSelectable) {
  // The fitted layout can drop to one card in a narrow window; the "+N more"
  // slot must stay reachable so the hidden tabs are still openable.
  MahoCtrlTabSwitcherView view(NullTabs(5), /*initial_selected_index=*/0,
                               /*visible_cards=*/1, /*card_width=*/140);
  ASSERT_EQ(1u, view.card_count());
  ASSERT_TRUE(view.has_more_affordance());
  EXPECT_EQ(1u, view.more_affordance_index());
  view.SetSelectedIndex(1);
  EXPECT_EQ(1u, view.selected_index());
  EXPECT_TRUE(view.more_affordance_selected());
  EXPECT_EQ(nullptr, view.GetSelectedWebContents());
}

// R3-2: a rebuilt overlay is constructed with the cursor's slot index; when
// that is the "+N more" slot, the constructor itself must paint it selected -
// SetSelectedIndex() cannot repair it afterwards because the index already
// equals selected_index().
TEST_F(MahoCtrlTabSwitcherViewTest, ConstructorPaintsTheMoreSlotSelected) {
  MahoCtrlTabSwitcherView view(NullTabs(8), /*initial_selected_index=*/1,
                               /*visible_cards=*/1, /*card_width=*/140);
  ASSERT_EQ(1u, view.card_count());
  ASSERT_TRUE(view.has_more_affordance());
  EXPECT_EQ(1u, view.selected_index());
  EXPECT_TRUE(view.more_affordance_selected());
}

// R3-2: re-selecting the current index repaints instead of no-oping.
TEST_F(MahoCtrlTabSwitcherViewTest, SetSelectedIndexRepaintsTheCurrentSlot) {
  MahoCtrlTabSwitcherView view(NullTabs(8), /*initial_selected_index=*/1,
                               /*visible_cards=*/1, /*card_width=*/140);
  ASSERT_TRUE(view.more_affordance_selected());
  view.SetSelectedIndex(0);
  EXPECT_EQ(0u, view.selected_index());
  EXPECT_FALSE(view.more_affordance_selected());
  view.SetSelectedIndex(1);
  EXPECT_TRUE(view.more_affordance_selected());
  // Same index: the early return is gone, the slot stays painted selected.
  view.SetSelectedIndex(1);
  EXPECT_EQ(1u, view.selected_index());
  EXPECT_TRUE(view.more_affordance_selected());
}

TEST_F(MahoCtrlTabSwitcherViewTest, FitToWidthKeepsPreferredCardsWhenWide) {
  const MahoCtrlTabSwitcherView::Layout layout =
      MahoCtrlTabSwitcherView::FitToWidth(/*available_width=*/5000,
                                          /*total_tabs=*/7,
                                          /*preferred_max=*/7);
  EXPECT_EQ(7u, layout.visible_cards);
  EXPECT_EQ(0, layout.card_width);  // natural card width
}

TEST_F(MahoCtrlTabSwitcherViewTest, FitToWidthNarrowsCardsBeforeDroppingThem) {
  // 1000px: seven natural cards do not fit, but five narrowed ones do, so no
  // tab slot is given up.
  const MahoCtrlTabSwitcherView::Layout layout =
      MahoCtrlTabSwitcherView::FitToWidth(/*available_width=*/1000,
                                          /*total_tabs=*/7,
                                          /*preferred_max=*/7);
  EXPECT_EQ(5u, layout.visible_cards);
  EXPECT_EQ(154, layout.card_width);
  EXPECT_EQ(1000, MahoCtrlTabSwitcherView::PreferredWidthFor(
                      5, /*has_more=*/true, layout.card_width));
}

TEST_F(MahoCtrlTabSwitcherViewTest, FitToWidthFitsBelowTheOldTwoCardFloor) {
  // 600px is below the 634px that two natural cards plus the "+N more" slot
  // need: the cards narrow instead of the overlay being clipped by the widget
  // bounds.
  const MahoCtrlTabSwitcherView::Layout layout =
      MahoCtrlTabSwitcherView::FitToWidth(/*available_width=*/600,
                                          /*total_tabs=*/7,
                                          /*preferred_max=*/7);
  EXPECT_EQ(2u, layout.visible_cards);
  EXPECT_EQ(203, layout.card_width);
  EXPECT_EQ(600, MahoCtrlTabSwitcherView::PreferredWidthFor(
                      2, /*has_more=*/true, layout.card_width));
}

TEST_F(MahoCtrlTabSwitcherViewTest, FitToWidthDropsToOneCardWhenNeeded) {
  const MahoCtrlTabSwitcherView::Layout layout =
      MahoCtrlTabSwitcherView::FitToWidth(/*available_width=*/400,
                                          /*total_tabs=*/7,
                                          /*preferred_max=*/7);
  EXPECT_EQ(1u, layout.visible_cards);
  EXPECT_EQ(218, layout.card_width);
  EXPECT_EQ(400, MahoCtrlTabSwitcherView::PreferredWidthFor(
                      1, /*has_more=*/true, layout.card_width));
}

TEST_F(MahoCtrlTabSwitcherViewTest, FitToWidthFallsBackToTheNarrowestCard) {
  // Below one minimum-width card plus the "+N more" slot nothing can fit; the
  // narrowest single card is the documented floor.
  const MahoCtrlTabSwitcherView::Layout layout =
      MahoCtrlTabSwitcherView::FitToWidth(/*available_width=*/100,
                                          /*total_tabs=*/7,
                                          /*preferred_max=*/7);
  EXPECT_EQ(1u, layout.visible_cards);
  EXPECT_EQ(MahoCtrlTabSwitcherCardView::kMinCardWidth, layout.card_width);
}

TEST_F(MahoCtrlTabSwitcherViewTest, FitToWidthFitsEveryWidthAboveTheFloor) {
  // The widget bounds clamp must never be what keeps the trailing card and the
  // "+N more" slot visible: every fitted layout fits its window, except the
  // documented single-narrowest-card floor.
  for (int width = 320; width <= 1400; width += 7) {
    for (size_t tabs : {2u, 3u, 8u, 20u}) {
      const MahoCtrlTabSwitcherView::Layout layout =
          MahoCtrlTabSwitcherView::FitToWidth(width, tabs,
                                              /*preferred_max=*/7);
      ASSERT_GE(layout.visible_cards, 1u) << width << "px / " << tabs;
      ASSERT_LE(layout.visible_cards, tabs) << width << "px / " << tabs;
      const bool has_more = tabs > layout.visible_cards;
      const int card_width = layout.card_width
                                 ? layout.card_width
                                 : MahoCtrlTabSwitcherCardView::kCardWidth;
      const int preferred = MahoCtrlTabSwitcherView::PreferredWidthFor(
          layout.visible_cards, has_more, card_width);
      const bool at_narrowest_floor =
          layout.visible_cards == 1 &&
          layout.card_width == MahoCtrlTabSwitcherCardView::kMinCardWidth;
      EXPECT_TRUE(preferred <= width || at_narrowest_floor)
          << width << "px / " << tabs << " tabs -> " << preferred;
    }
  }
}

}  // namespace
}  // namespace maho
