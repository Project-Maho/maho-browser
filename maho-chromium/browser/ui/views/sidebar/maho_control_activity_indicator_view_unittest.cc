// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_control_activity_indicator_view.h"

#include <algorithm>
#include <memory>
#include <string>

#include "base/test/bind.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/color/color_variant.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/events/test/test_event.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace maho {
namespace {

using State = MahoControlActivityState;
using Plane = MahoControlPlane;
using Model = MahoControlActivityIndicatorModel;

Model MakeModel(State state, Plane plane = Plane::kCli) {
  Model model;
  model.state = state;
  model.plane = plane;
  model.controller_name = u"Maho CLI";
  model.target_title = u"Maho MCP Visual Feedback Probe";
  return model;
}

// A deterministic light/dark palette pair. Values mirror the shape produced by
// ResolveMahoSidebarPalette() without depending on live theme resolution.
MahoSidebarPalette MakePalette(bool dark) {
  MahoSidebarPalette palette;
  palette.dark_family = dark;
  palette.primary_text =
      dark ? SkColorSetRGB(0xEC, 0xED, 0xEF) : SkColorSetRGB(0x1F, 0x20, 0x22);
  palette.secondary_text =
      dark ? SkColorSetRGB(0xA7, 0xAB, 0xB1) : SkColorSetRGB(0x5F, 0x63, 0x68);
  palette.tertiary_text =
      dark ? SkColorSetRGB(0x85, 0x8A, 0x91) : SkColorSetRGB(0x7A, 0x7F, 0x86);
  palette.disabled_text =
      dark ? SkColorSetRGB(0x60, 0x64, 0x69) : SkColorSetRGB(0xA0, 0xA4, 0xA8);
  palette.neutral_glyph =
      dark ? SkColorSetRGB(0xC7, 0xCA, 0xCE) : SkColorSetRGB(0x3C, 0x40, 0x43);
  palette.outline =
      dark ? SkColorSetRGB(0x85, 0x8A, 0x91) : SkColorSetRGB(0xC4, 0xC7, 0xCB);
  palette.focus_ring =
      dark ? SkColorSetRGB(0x8A, 0xB4, 0xF8) : SkColorSetRGB(0x0B, 0x57, 0xD0);
  return palette;
}

class MahoControlActivityIndicatorViewTest : public views::ViewsTestBase {
 public:
  void SetUp() override {
    views::ViewsTestBase::SetUp();
    widget_ = std::make_unique<views::Widget>();
    views::Widget::InitParams params =
        CreateParams(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                     views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
    params.bounds = gfx::Rect(0, 0, 288, 120);
    widget_->Init(std::move(params));
    view_ = widget_->SetContentsView(
        std::make_unique<MahoControlActivityIndicatorView>());
    view_->OnSidebarPaletteChanged(MakePalette(/*dark=*/true));
  }

  void TearDown() override {
    view_ = nullptr;
    widget_.reset();
    views::ViewsTestBase::TearDown();
  }

 protected:
  MahoControlActivityIndicatorView* view() { return view_; }

  void LayoutAtWidth(int width) {
    view_->SetBounds(
        0, 0, width,
        view_->GetPreferredSize(views::SizeBounds(width, {})).height());
    views::test::RunScheduledLayout(view_);
  }

  std::unique_ptr<views::Widget> widget_;
  raw_ptr<MahoControlActivityIndicatorView> view_ = nullptr;
};

// --- State -> icon / text mapping ------------------------------------------

TEST_F(MahoControlActivityIndicatorViewTest, EveryStateMapsToADistinctIcon) {
  EXPECT_EQ(&MahoControlActivityIndicatorView::IconForState(State::kReading),
            &maho_lucide_icons::kGlassesIcon);
  EXPECT_EQ(&MahoControlActivityIndicatorView::IconForState(State::kActing),
            &maho_lucide_icons::kHammerIcon);
  EXPECT_EQ(
      &MahoControlActivityIndicatorView::IconForState(State::kWaitingApproval),
      &maho_lucide_icons::kShieldQuestionIcon);
  EXPECT_EQ(&MahoControlActivityIndicatorView::IconForState(State::kPaused),
            &maho_lucide_icons::kPauseIcon);
  EXPECT_EQ(
      &MahoControlActivityIndicatorView::IconForState(State::kDisconnected),
      &maho_lucide_icons::kShieldXIcon);
  EXPECT_EQ(&MahoControlActivityIndicatorView::IconForState(State::kError),
            &maho_lucide_icons::kShieldXIcon);
}

TEST_F(MahoControlActivityIndicatorViewTest, ReadingStateRendersReadingText) {
  view()->SetModel(MakeModel(State::kReading));
  EXPECT_EQ(view()->status_label_for_testing()->GetText(),
            u"AI - CLI - Reading this tab");
  EXPECT_TRUE(view()->GetVisible());
}

TEST_F(MahoControlActivityIndicatorViewTest, ActingStateRendersActingText) {
  view()->SetModel(MakeModel(State::kActing));
  EXPECT_EQ(view()->status_label_for_testing()->GetText(),
            u"AI - CLI - Acting on this tab");
}

TEST_F(MahoControlActivityIndicatorViewTest,
       WaitingApprovalStateRendersApprovalText) {
  view()->SetModel(MakeModel(State::kWaitingApproval));
  EXPECT_EQ(view()->status_label_for_testing()->GetText(),
            u"AI - CLI - Waiting for approval");
}

TEST_F(MahoControlActivityIndicatorViewTest, PausedStateRendersPausedText) {
  view()->SetModel(MakeModel(State::kPaused));
  EXPECT_EQ(view()->status_label_for_testing()->GetText(),
            u"AI - CLI - Paused");
}

TEST_F(MahoControlActivityIndicatorViewTest,
       DisconnectedStateRendersDisconnectedText) {
  view()->SetModel(MakeModel(State::kDisconnected));
  EXPECT_EQ(view()->status_label_for_testing()->GetText(),
            u"AI - CLI - Disconnected");
}

TEST_F(MahoControlActivityIndicatorViewTest, ErrorStateRendersErrorText) {
  view()->SetModel(MakeModel(State::kError));
  EXPECT_EQ(view()->status_label_for_testing()->GetText(),
            u"AI - CLI - Action failed");
}

TEST_F(MahoControlActivityIndicatorViewTest, IdleStateHidesTheIndicator) {
  view()->SetModel(MakeModel(State::kReading));
  ASSERT_TRUE(view()->GetVisible());

  view()->SetModel(MakeModel(State::kIdle));
  EXPECT_FALSE(view()->GetVisible());
}

TEST_F(MahoControlActivityIndicatorViewTest, PlanePrefixDistinguishesRuntimes) {
  view()->SetModel(MakeModel(State::kReading, Plane::kExternalMcp));
  EXPECT_EQ(view()->status_label_for_testing()->GetText(),
            u"AI - MCP - Reading this tab");

  view()->SetModel(MakeModel(State::kReading, Plane::kEmbedded));
  EXPECT_EQ(view()->status_label_for_testing()->GetText(),
            u"AI - Reading this tab");
}

// --- Background tab / other window attribution -----------------------------

TEST_F(MahoControlActivityIndicatorViewTest,
       BackgroundTabTargetChangesStatusText) {
  Model model = MakeModel(State::kActing);
  model.target_is_background_tab = true;
  view()->SetModel(model);

  EXPECT_EQ(view()->status_label_for_testing()->GetText(),
            u"AI - CLI - Working in another tab");
}

TEST_F(MahoControlActivityIndicatorViewTest,
       OtherWindowTargetChangesStatusText) {
  Model model = MakeModel(State::kActing);
  model.target_is_other_window = true;
  view()->SetModel(model);

  EXPECT_EQ(view()->status_label_for_testing()->GetText(),
            u"AI - CLI - Working in another Maho window");
}

TEST_F(MahoControlActivityIndicatorViewTest,
       OtherWindowOutranksBackgroundTabAttribution) {
  Model model = MakeModel(State::kReading);
  model.target_is_background_tab = true;
  model.target_is_other_window = true;
  view()->SetModel(model);

  EXPECT_EQ(view()->status_label_for_testing()->GetText(),
            u"AI - CLI - Working in another Maho window");
}

// --- Target title ----------------------------------------------------------

TEST_F(MahoControlActivityIndicatorViewTest, TargetTitleIsRendered) {
  view()->SetModel(MakeModel(State::kReading));
  EXPECT_EQ(view()->target_label_for_testing()->GetText(),
            u"Maho MCP Visual Feedback Probe");
  EXPECT_TRUE(view()->target_label_for_testing()->GetVisible());
}

TEST_F(MahoControlActivityIndicatorViewTest,
       MissingTargetTitleFallsBackToUntitledWithoutHidingControls) {
  Model model = MakeModel(State::kActing);
  model.target_title = std::u16string();
  view()->SetModel(model);

  EXPECT_EQ(view()->target_label_for_testing()->GetText(), u"Untitled tab");
  EXPECT_TRUE(view()->stop_button_for_testing()->GetVisible());
  EXPECT_TRUE(view()->details_button_for_testing()->GetVisible());
}

TEST_F(MahoControlActivityIndicatorViewTest,
       LongTargetTitleElidesInsteadOfPushingOutStop) {
  Model model = MakeModel(State::kActing);
  model.target_title =
      u"An extraordinarily long tab title that would otherwise consume the "
      u"entire sidebar width and push the stop control out of the chip";
  view()->SetModel(model);
  LayoutAtWidth(sidebar_layout::kDefaultRailWidthDp);

  // The title elides rather than growing unbounded.
  EXPECT_EQ(view()->target_label_for_testing()->GetElideBehavior(),
            gfx::ELIDE_TAIL);
  // Controller identity and Stop both survive.
  EXPECT_TRUE(view()->status_label_for_testing()->GetVisible());
  EXPECT_TRUE(view()->stop_button_for_testing()->GetVisible());
  EXPECT_GT(view()->stop_button_for_testing()->width(), 0);
  EXPECT_LE(view()->stop_button_for_testing()->bounds().right(),
            view()->width());
}

// --- Action availability ---------------------------------------------------

TEST_F(MahoControlActivityIndicatorViewTest,
       StopIsAvailableWhileAControllerCanStillAct) {
  EXPECT_TRUE(
      MahoControlActivityIndicatorView::StateAllowsStop(State::kReading));
  EXPECT_TRUE(
      MahoControlActivityIndicatorView::StateAllowsStop(State::kActing));
  EXPECT_TRUE(MahoControlActivityIndicatorView::StateAllowsStop(
      State::kWaitingApproval));
  EXPECT_TRUE(
      MahoControlActivityIndicatorView::StateAllowsStop(State::kPaused));
}

TEST_F(MahoControlActivityIndicatorViewTest,
       StopIsUnavailableOnceTheSessionCannotAct) {
  EXPECT_FALSE(
      MahoControlActivityIndicatorView::StateAllowsStop(State::kDisconnected));
  EXPECT_FALSE(
      MahoControlActivityIndicatorView::StateAllowsStop(State::kError));
  EXPECT_FALSE(MahoControlActivityIndicatorView::StateAllowsStop(State::kIdle));
}

TEST_F(MahoControlActivityIndicatorViewTest,
       DetailsRemainsAvailableAfterFailureAndDisconnect) {
  EXPECT_TRUE(
      MahoControlActivityIndicatorView::StateAllowsDetails(State::kError));
  EXPECT_TRUE(MahoControlActivityIndicatorView::StateAllowsDetails(
      State::kDisconnected));
  EXPECT_FALSE(
      MahoControlActivityIndicatorView::StateAllowsDetails(State::kIdle));
}

TEST_F(MahoControlActivityIndicatorViewTest,
       StopButtonHidesWhenTheStateForbidsIt) {
  view()->SetModel(MakeModel(State::kDisconnected));
  EXPECT_FALSE(view()->stop_button_for_testing()->GetVisible());
  EXPECT_TRUE(view()->details_button_for_testing()->GetVisible());
}

TEST_F(MahoControlActivityIndicatorViewTest,
       StopReadsRevokeForOngoingGrantStates) {
  EXPECT_EQ(
      MahoControlActivityIndicatorView::StopButtonLabelForState(State::kActing),
      u"Stop");
  EXPECT_EQ(MahoControlActivityIndicatorView::StopButtonLabelForState(
                State::kReading),
            u"Stop");
  // A paused or approval-blocked controller still holds its grant, so the
  // destructive affordance is a revoke, not an interrupt.
  EXPECT_EQ(
      MahoControlActivityIndicatorView::StopButtonLabelForState(State::kPaused),
      u"Revoke");
  EXPECT_EQ(MahoControlActivityIndicatorView::StopButtonLabelForState(
                State::kWaitingApproval),
            u"Revoke");
}

TEST_F(MahoControlActivityIndicatorViewTest, PressingButtonsInvokesCallbacks) {
  int details_calls = 0;
  int stop_calls = 0;
  view()->SetDetailsCallback(
      base::BindLambdaForTesting([&] { ++details_calls; }));
  view()->SetStopCallback(base::BindLambdaForTesting([&] { ++stop_calls; }));
  view()->SetModel(MakeModel(State::kActing));

  views::test::ButtonTestApi(view()->details_button_for_testing())
      .NotifyClick(ui::test::TestEvent());
  views::test::ButtonTestApi(view()->stop_button_for_testing())
      .NotifyClick(ui::test::TestEvent());

  EXPECT_EQ(details_calls, 1);
  EXPECT_EQ(stop_calls, 1);
}

// --- Accessibility ---------------------------------------------------------

TEST_F(MahoControlActivityIndicatorViewTest,
       AccessibleNameCarriesControllerStateAndTarget) {
  view()->SetModel(MakeModel(State::kActing));
  EXPECT_EQ(view()->GetViewAccessibility().GetCachedName(),
            u"Maho CLI - Acting on this tab - Maho MCP Visual Feedback Probe");
}

TEST_F(MahoControlActivityIndicatorViewTest,
       AccessibleNameSurvivesCompactMode) {
  view()->SetModel(MakeModel(State::kActing));
  view()->SetCompact(true);
  // Compact hides visible text but must not degrade the screen-reader name.
  EXPECT_FALSE(view()->status_label_for_testing()->GetVisible());
  EXPECT_EQ(view()->GetViewAccessibility().GetCachedName(),
            u"Maho CLI - Acting on this tab - Maho MCP Visual Feedback Probe");
}

TEST_F(MahoControlActivityIndicatorViewTest, IndicatorExposesStatusRole) {
  view()->SetModel(MakeModel(State::kActing));
  EXPECT_EQ(view()->GetViewAccessibility().GetCachedRole(),
            ax::mojom::Role::kStatus);
}

TEST_F(MahoControlActivityIndicatorViewTest,
       ButtonsCarryExplicitAccessibleNames) {
  view()->SetModel(MakeModel(State::kActing));
  EXPECT_EQ(view()
                ->details_button_for_testing()
                ->GetViewAccessibility()
                .GetCachedName(),
            u"Details");
  EXPECT_EQ(
      view()->stop_button_for_testing()->GetViewAccessibility().GetCachedName(),
      u"Stop");
}

TEST_F(MahoControlActivityIndicatorViewTest,
       DecorativeStateGlyphIsIgnoredByAssistiveTech) {
  view()->SetModel(MakeModel(State::kActing));
  // The glyph duplicates information already in the accessible name.
  EXPECT_TRUE(
      view()->state_icon_for_testing()->GetViewAccessibility().GetIsIgnored());
}

// --- Keyboard traversal ----------------------------------------------------

TEST_F(MahoControlActivityIndicatorViewTest,
       BothActionsAreKeyboardFocusableInVisualOrder) {
  view()->SetModel(MakeModel(State::kActing));

  EXPECT_TRUE(view()->details_button_for_testing()->IsFocusable());
  EXPECT_TRUE(view()->stop_button_for_testing()->IsFocusable());

  // Details precedes Stop so the destructive action is never the first stop.
  views::View* focus_root = view();
  const auto& children = focus_root->children();
  auto details_pos = std::find(children.begin(), children.end(),
                               view()->details_button_for_testing());
  auto stop_pos = std::find(children.begin(), children.end(),
                            view()->stop_button_for_testing());
  ASSERT_NE(details_pos, children.end());
  ASSERT_NE(stop_pos, children.end());
  EXPECT_LT(details_pos, stop_pos);
}

TEST_F(MahoControlActivityIndicatorViewTest,
       NonInteractiveTextIsNotAFocusStop) {
  view()->SetModel(MakeModel(State::kActing));
  EXPECT_FALSE(view()->status_label_for_testing()->IsFocusable());
  EXPECT_FALSE(view()->target_label_for_testing()->IsFocusable());
  EXPECT_FALSE(view()->state_icon_for_testing()->IsFocusable());
}

// --- Compact / narrow layout ----------------------------------------------

TEST_F(MahoControlActivityIndicatorViewTest,
       CompactModeKeepsGlyphAndStopButDropsText) {
  view()->SetModel(MakeModel(State::kActing));
  view()->SetCompact(true);

  EXPECT_TRUE(view()->state_icon_for_testing()->GetVisible());
  EXPECT_TRUE(view()->stop_button_for_testing()->GetVisible());
  EXPECT_FALSE(view()->status_label_for_testing()->GetVisible());
  EXPECT_FALSE(view()->target_label_for_testing()->GetVisible());
}

TEST_F(MahoControlActivityIndicatorViewTest,
       CompactChipFitsInsideANarrowWindow) {
  view()->SetModel(MakeModel(State::kActing));
  view()->SetCompact(true);

  // 320dp window minus sidebar insets is the adversarial floor.
  constexpr int kNarrowContentWidth = 320 - (sidebar_layout::kRailInsetLeftDp +
                                             sidebar_layout::kRailInsetRightDp);
  const gfx::Size preferred =
      view()->GetPreferredSize(views::SizeBounds(kNarrowContentWidth, {}));
  EXPECT_LE(preferred.width(), kNarrowContentWidth);

  LayoutAtWidth(kNarrowContentWidth);
  EXPECT_LE(view()->stop_button_for_testing()->bounds().right(),
            kNarrowContentWidth);
  EXPECT_GT(view()->stop_button_for_testing()->width(), 0);
}

TEST_F(MahoControlActivityIndicatorViewTest,
       ExpandedChipFitsTheDefaultRailWidth) {
  view()->SetModel(MakeModel(State::kActing));
  constexpr int kContentWidth =
      sidebar_layout::kDefaultRailWidthDp -
      (sidebar_layout::kRailInsetLeftDp + sidebar_layout::kRailInsetRightDp);
  const gfx::Size preferred =
      view()->GetPreferredSize(views::SizeBounds(kContentWidth, {}));
  EXPECT_LE(preferred.width(), kContentWidth);
}

// --- Theme mapping ---------------------------------------------------------

TEST_F(MahoControlActivityIndicatorViewTest,
       ForegroundsComeFromTheSidebarPaletteInBothFamilies) {
  view()->SetModel(MakeModel(State::kReading));

  const MahoSidebarPalette dark = MakePalette(/*dark=*/true);
  view()->OnSidebarPaletteChanged(dark);
  EXPECT_EQ(view()->status_label_for_testing()->GetEnabledColor(),
            dark.primary_text);
  EXPECT_EQ(view()->target_label_for_testing()->GetEnabledColor(),
            dark.secondary_text);

  const MahoSidebarPalette light = MakePalette(/*dark=*/false);
  view()->OnSidebarPaletteChanged(light);
  EXPECT_EQ(view()->status_label_for_testing()->GetEnabledColor(),
            light.primary_text);
  EXPECT_EQ(view()->target_label_for_testing()->GetEnabledColor(),
            light.secondary_text);
  // Light and dark must actually differ, i.e. the palette is really consumed.
  EXPECT_NE(dark.primary_text, light.primary_text);
}

TEST_F(MahoControlActivityIndicatorViewTest,
       QuietStatesUseTheSubduedGlyphRoleAndLoudStatesTheAccentRole) {
  const MahoSidebarPalette palette = MakePalette(/*dark=*/true);
  view()->OnSidebarPaletteChanged(palette);

  view()->SetModel(MakeModel(State::kReading));
  const ui::ColorVariant reading_color =
      view()->state_icon_for_testing()->GetImageModel().GetVectorIcon().color();

  view()->SetModel(MakeModel(State::kActing));
  const ui::ColorVariant acting_color =
      view()->state_icon_for_testing()->GetImageModel().GetVectorIcon().color();

  EXPECT_TRUE(reading_color == palette.neutral_glyph);
  EXPECT_TRUE(acting_color == palette.focus_ring);
  EXPECT_NE(palette.neutral_glyph, palette.focus_ring);
}

TEST_F(MahoControlActivityIndicatorViewTest,
       ForcedColorsPaletteStillDrivesForegrounds) {
  MahoSidebarPalette forced = MakePalette(/*dark=*/true);
  forced.forced_colors = true;
  forced.primary_text = SK_ColorWHITE;
  forced.secondary_text = SK_ColorWHITE;
  view()->OnSidebarPaletteChanged(forced);
  view()->SetModel(MakeModel(State::kActing));

  EXPECT_EQ(view()->status_label_for_testing()->GetEnabledColor(),
            SK_ColorWHITE);
  EXPECT_EQ(view()->target_label_for_testing()->GetEnabledColor(),
            SK_ColorWHITE);
}

// --- Reduced motion --------------------------------------------------------

TEST_F(MahoControlActivityIndicatorViewTest,
       ComponentIsStaticAndPaintsNoLayerAnimation) {
  view()->SetModel(MakeModel(State::kActing));

  // The static indicator must not schedule any animation, so its rendering is
  // identical whether or not the platform requests reduced motion.
  {
    gfx::ScopedAnimationDurationScaleMode zero(
        gfx::ScopedAnimationDurationScaleMode::ZERO_DURATION);
    view()->SetModel(MakeModel(State::kActing));
    EXPECT_EQ(view()->status_label_for_testing()->GetText(),
              u"AI - CLI - Acting on this tab");
    EXPECT_FALSE(view()->layer() &&
                 view()->layer()->GetAnimator()->is_animating());
  }
  {
    gfx::ScopedAnimationDurationScaleMode normal(
        gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
    view()->SetModel(MakeModel(State::kActing));
    EXPECT_EQ(view()->status_label_for_testing()->GetText(),
              u"AI - CLI - Acting on this tab");
    EXPECT_FALSE(view()->layer() &&
                 view()->layer()->GetAnimator()->is_animating());
  }
}

// --- No business-state ownership ------------------------------------------

TEST_F(MahoControlActivityIndicatorViewTest,
       ViewHoldsOnlyWhatTheModelPushedIn) {
  Model model = MakeModel(State::kReading);
  view()->SetModel(model);
  EXPECT_EQ(view()->model().state, State::kReading);

  // Re-pushing a different model fully replaces the render state; the view
  // never merges, infers, or retains prior session truth.
  Model next = MakeModel(State::kPaused, Plane::kExternalMcp);
  next.controller_name = u"External MCP";
  next.target_title = u"Second target";
  view()->SetModel(next);

  EXPECT_EQ(view()->model().state, State::kPaused);
  EXPECT_EQ(view()->model().plane, Plane::kExternalMcp);
  EXPECT_EQ(view()->target_label_for_testing()->GetText(), u"Second target");
  EXPECT_EQ(view()->status_label_for_testing()->GetText(),
            u"AI - MCP - Paused");
}

}  // namespace
}  // namespace maho
