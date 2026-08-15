// Copyright 2026 Maho Browser. All rights reserved.

// Baseline characterization coverage for the sidebar top bar as it exists
// before the control activity indicator is hosted there. These expectations
// pin the pre-existing action set and layout order so the indicator work
// cannot silently move or drop an existing top-bar control.

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "chrome/test/base/browser_with_test_window_test.h"
#include "maho/browser/ui/views/sidebar/maho_control_activity_indicator_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_top_bar_view.h"
#include "maho/browser/ui/views/sidebar/maho_toolbar_button_provider.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace maho {
namespace {

// Child index of each structural cluster inside the top bar's BoxLayout.
constexpr size_t kTrafficLightSpacerIndex = 0;
constexpr size_t kLeadingClusterIndex = 1;

class MahoSidebarTopBarCharacterizationTest : public BrowserWithTestWindowTest {
 public:
  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    widget_ = std::make_unique<views::Widget>();
    views::Widget::InitParams params(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
    params.bounds = gfx::Rect(0, 0, 320, 200);
    widget_->Init(std::move(params));
    top_bar_ = widget_->SetContentsView(
        std::make_unique<MahoSidebarTopBarView>(browser()));
  }

  void TearDown() override {
    top_bar_ = nullptr;
    widget_.reset();
    BrowserWithTestWindowTest::TearDown();
  }

 protected:
  MahoSidebarTopBarView* top_bar() { return top_bar_; }

  std::unique_ptr<views::Widget> widget_;
  raw_ptr<MahoSidebarTopBarView> top_bar_ = nullptr;
};

TEST_F(MahoSidebarTopBarCharacterizationTest,
       NavClusterExposesNavigationAndPanelActions) {
  views::View* nav = top_bar()->nav_cluster_for_testing();
  ASSERT_TRUE(nav);

  // Back, Forward, Reload, Mail, AI in that construction order.
  ASSERT_EQ(nav->children().size(), 5u);
  EXPECT_EQ(nav->children()[0], top_bar()->back_button_for_testing());
  EXPECT_EQ(nav->children()[1], top_bar()->forward_button_for_testing());
  EXPECT_EQ(nav->children()[2], top_bar()->reload_button_for_testing());
  EXPECT_EQ(nav->children()[3], top_bar()->mail_button_for_testing());
  EXPECT_EQ(nav->children()[4], top_bar()->ai_button_for_testing());
}

TEST_F(MahoSidebarTopBarCharacterizationTest,
       LeadingClusterHostsSidebarToggle) {
  views::View* leading = top_bar()->leading_cluster_for_testing();
  ASSERT_TRUE(leading);
  ASSERT_FALSE(leading->children().empty());
  EXPECT_EQ(leading->children()[0],
            top_bar()->sidebar_toggle_button_for_testing());
}

TEST_F(MahoSidebarTopBarCharacterizationTest,
       StructuralChildOrderStartsWithTrafficLightSpacerThenLeading) {
  const auto& children = top_bar()->children();
  ASSERT_GE(children.size(), 4u);
  // The traffic-light spacer must stay first so macOS window controls keep
  // their reserved width, and the leading cluster must follow it.
  EXPECT_EQ(children[kLeadingClusterIndex],
            top_bar()->leading_cluster_for_testing());
  EXPECT_NE(children[kTrafficLightSpacerIndex],
            top_bar()->leading_cluster_for_testing());
  // The flexible spacer and nav cluster remain the trailing pair.
  EXPECT_EQ(children[children.size() - 2],
            top_bar()->flex_spacer_for_testing());
  EXPECT_EQ(children[children.size() - 1],
            top_bar()->nav_cluster_for_testing());
}

TEST_F(MahoSidebarTopBarCharacterizationTest,
       NavigationButtonsFollowModelEnabledState) {
  MahoSidebarTopBarModel model;
  model.navigation_enabled = true;
  model.can_go_back = true;
  model.can_go_forward = false;
  model.can_reload = true;
  top_bar()->Update(model);

  EXPECT_TRUE(top_bar()->back_button_for_testing()->GetEnabled());
  EXPECT_FALSE(top_bar()->forward_button_for_testing()->GetEnabled());
  EXPECT_TRUE(top_bar()->reload_button_for_testing()->GetEnabled());
  // The sidebar toggle and AI entry point are always operable.
  EXPECT_TRUE(top_bar()->sidebar_toggle_button_for_testing()->GetEnabled());
  EXPECT_TRUE(top_bar()->ai_button_for_testing()->GetEnabled());
}

TEST_F(MahoSidebarTopBarCharacterizationTest,
       ReloadButtonBecomesStopWhileLoading) {
  MahoSidebarTopBarModel model;
  model.navigation_enabled = true;
  model.can_reload = true;
  model.is_loading = true;
  top_bar()->Update(model);

  EXPECT_EQ(top_bar()
                ->reload_button_for_testing()
                ->GetViewAccessibility()
                .GetCachedName(),
            u"Stop");
}

// ---------------------------------------------------------------------------
// Host wiring: the top bar places and drives the chip but owns none of its
// controller/session truth.
// ---------------------------------------------------------------------------

TEST_F(MahoSidebarTopBarCharacterizationTest,
       TopBarHostsTheControlActivityChipBetweenLeadingAndTrailingClusters) {
  MahoControlActivityIndicatorView* chip =
      top_bar()->control_activity_indicator_for_testing();
  ASSERT_TRUE(chip);

  const auto& children = top_bar()->children();
  auto chip_pos = std::find(children.begin(), children.end(), chip);
  auto leading_pos = std::find(children.begin(), children.end(),
                               top_bar()->leading_cluster_for_testing());
  auto nav_pos = std::find(children.begin(), children.end(),
                           top_bar()->nav_cluster_for_testing());
  ASSERT_NE(chip_pos, children.end());
  EXPECT_LT(leading_pos, chip_pos);
  EXPECT_LT(chip_pos, nav_pos);
}

TEST_F(MahoSidebarTopBarCharacterizationTest,
       HostedChipStartsHiddenBecauseNoControllerIsAttached) {
  MahoControlActivityIndicatorView* chip =
      top_bar()->control_activity_indicator_for_testing();
  ASSERT_TRUE(chip);
  EXPECT_EQ(chip->model().state, MahoControlActivityState::kIdle);
  EXPECT_FALSE(chip->GetVisible());
}

TEST_F(MahoSidebarTopBarCharacterizationTest,
       SetControlActivityPushesStateIntoTheHostedChip) {
  MahoControlActivityIndicatorModel model;
  model.state = MahoControlActivityState::kActing;
  model.plane = MahoControlPlane::kCli;
  model.controller_name = u"Maho CLI";
  model.target_title = u"Probe tab";
  top_bar()->SetControlActivity(model);

  MahoControlActivityIndicatorView* chip =
      top_bar()->control_activity_indicator_for_testing();
  ASSERT_TRUE(chip);
  EXPECT_TRUE(chip->GetVisible());
  EXPECT_EQ(chip->model().state, MahoControlActivityState::kActing);
  EXPECT_EQ(chip->status_label_for_testing()->GetText(),
            u"AI - CLI - Acting on this tab");
  EXPECT_EQ(chip->GetViewAccessibility().GetCachedName(),
            u"Maho CLI - Acting on this tab - Probe tab");
}

TEST_F(MahoSidebarTopBarCharacterizationTest,
       HostingTheChipDoesNotDisturbExistingTopBarActions) {
  MahoControlActivityIndicatorModel model;
  model.state = MahoControlActivityState::kActing;
  model.controller_name = u"Maho CLI";
  top_bar()->SetControlActivity(model);

  // The pre-existing action set and its order are unchanged by the new child.
  views::View* nav = top_bar()->nav_cluster_for_testing();
  ASSERT_EQ(nav->children().size(), 5u);
  EXPECT_EQ(nav->children()[0], top_bar()->back_button_for_testing());
  EXPECT_EQ(nav->children()[4], top_bar()->ai_button_for_testing());
  EXPECT_EQ(top_bar()->leading_cluster_for_testing()->children()[0],
            top_bar()->sidebar_toggle_button_for_testing());
}

TEST_F(MahoSidebarTopBarCharacterizationTest,
       NarrowTopBarSwitchesTheChipToItsCompactForm) {
  MahoControlActivityIndicatorModel model;
  model.state = MahoControlActivityState::kActing;
  model.controller_name = u"Maho CLI";
  model.target_title = u"Probe tab";
  top_bar()->SetControlActivity(model);

  MahoControlActivityIndicatorView* chip =
      top_bar()->control_activity_indicator_for_testing();
  ASSERT_TRUE(chip);

  top_bar()->SetBounds(0, 0, sidebar_layout::kDefaultRailWidthDp, 40);
  views::test::RunScheduledLayout(top_bar());
  EXPECT_FALSE(chip->compact());

  // Below the shared compact-navigation threshold the chip sheds its prose but
  // must keep the Stop affordance reachable.
  top_bar()->SetBounds(0, 0, sidebar_layout::kCompactNavigationThresholdDp - 1,
                       40);
  views::test::RunScheduledLayout(top_bar());
  EXPECT_TRUE(chip->compact());
  EXPECT_TRUE(chip->stop_button_for_testing()->GetVisible());
}

TEST_F(MahoSidebarTopBarCharacterizationTest,
       PaletteChangesReachTheHostedChip) {
  MahoControlActivityIndicatorModel model;
  model.state = MahoControlActivityState::kReading;
  model.controller_name = u"Maho CLI";
  top_bar()->SetControlActivity(model);

  MahoSidebarPalette palette;
  palette.primary_text = SkColorSetRGB(0xEC, 0xED, 0xEF);
  palette.secondary_text = SkColorSetRGB(0xA7, 0xAB, 0xB1);
  palette.neutral_glyph = SkColorSetRGB(0xC7, 0xCA, 0xCE);
  palette.focus_ring = SkColorSetRGB(0x8A, 0xB4, 0xF8);
  palette.outline = SkColorSetRGB(0x85, 0x8A, 0x91);
  top_bar()->OnSidebarPaletteChanged(palette);

  MahoControlActivityIndicatorView* chip =
      top_bar()->control_activity_indicator_for_testing();
  ASSERT_TRUE(chip);
  EXPECT_EQ(chip->status_label_for_testing()->GetEnabledColor(),
            palette.primary_text);
}

// ---------------------------------------------------------------------------
// Collapsed-sidebar toolbar fallback host selection.
// ---------------------------------------------------------------------------

class MahoControlActivityFallbackHostTest : public views::ViewsTestBase {
 protected:
  MahoControlActivityIndicatorView sidebar_chip_;
  MahoControlActivityIndicatorView toolbar_chip_;
};

TEST_F(MahoControlActivityFallbackHostTest,
       DrawnSidebarChipOwnsTheIndicatorSurface) {
  EXPECT_EQ(
      MahoToolbarButtonProvider::ResolveControlActivityHost(
          &sidebar_chip_, /*sidebar_indicator_drawn=*/true, &toolbar_chip_),
      &sidebar_chip_);
}

TEST_F(MahoControlActivityFallbackHostTest,
       CollapsedSidebarFallsBackToTheToolbarChip) {
  EXPECT_EQ(
      MahoToolbarButtonProvider::ResolveControlActivityHost(
          &sidebar_chip_, /*sidebar_indicator_drawn=*/false, &toolbar_chip_),
      &toolbar_chip_);
}

TEST_F(MahoControlActivityFallbackHostTest,
       AbsentSidebarChipStillResolvesToTheToolbarChip) {
  EXPECT_EQ(MahoToolbarButtonProvider::ResolveControlActivityHost(
                nullptr, /*sidebar_indicator_drawn=*/true, &toolbar_chip_),
            &toolbar_chip_);
}

TEST_F(MahoControlActivityFallbackHostTest,
       NoHostResolvesToNullRatherThanADanglingChip) {
  EXPECT_EQ(MahoToolbarButtonProvider::ResolveControlActivityHost(
                nullptr, /*sidebar_indicator_drawn=*/false, nullptr),
            nullptr);
  EXPECT_EQ(MahoToolbarButtonProvider::ResolveControlActivityHost(
                &sidebar_chip_, /*sidebar_indicator_drawn=*/false, nullptr),
            nullptr);
}

}  // namespace
}  // namespace maho
