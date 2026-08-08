// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_action_selector_view.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/ax_event_counter.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"

namespace maho {

class MahoCommandActionSelectorViewTest : public views::ViewsTestBase {
 public:
  MahoCommandActionSelectorViewTest() = default;
  ~MahoCommandActionSelectorViewTest() override = default;

  void TearDown() override {
    host_widget_.reset();
    views::ViewsTestBase::TearDown();
  }

 protected:
  MahoCommandActionSelectorView* CreateSelector(
      base::RepeatingCallback<void(PaletteAction)> on_change =
          base::DoNothing()) {
    host_widget_ =
        CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                         views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
    auto selector =
        std::make_unique<MahoCommandActionSelectorView>(std::move(on_change));
    auto* selector_ptr = selector.get();
    host_widget_->SetContentsView(std::move(selector));
    host_widget_->Show();
    host_widget_->Activate();
    return selector_ptr;
  }

  views::Button* search_button(MahoCommandActionSelectorView* selector) {
    return selector->search_button_;
  }

  views::Button* ask_maho_button(MahoCommandActionSelectorView* selector) {
    return selector->ask_maho_button_;
  }

  views::Label* hint_label(MahoCommandActionSelectorView* selector) {
    return selector->hint_label_;
  }

  void SelectFromInteraction(MahoCommandActionSelectorView* selector,
                             PaletteAction action) {
    selector->OnSegmentClicked(action);
  }

  bool HasFlowBackground(MahoCommandActionSelectorView* selector,
                         PaletteAction action) {
    return selector->IsFlowBackgroundInstalledForTesting(action);
  }

  bool IsFlowAnimating(MahoCommandActionSelectorView* selector,
                       PaletteAction action) {
    return selector->IsFlowAnimatingForTesting(action);
  }

  SkColor FlowAccent(MahoCommandActionSelectorView* selector,
                     PaletteAction action) {
    return selector->FlowAccentForTesting(action);
  }

  SkColor SegmentTextColor(MahoCommandActionSelectorView* selector,
                           PaletteAction action) {
    return selector->SegmentTextColorForTesting(action);
  }

  bool SegmentUsesSubpixelRendering(
      MahoCommandActionSelectorView* selector,
      PaletteAction action) {
    return selector->SegmentUsesSubpixelRenderingForTesting(action);
  }

  int RunningFlowCount() {
    return MahoCommandActionSelectorView::
        GetRunningFlowAnimationCountForTesting();
  }

  int SegmentCornerRadius() {
    return MahoCommandActionSelectorView::
        GetSegmentCornerRadiusForTesting();
  }

  void HideWidget() { host_widget_->Hide(); }
  void ShowAndActivateWidget() {
    host_widget_->Show();
    host_widget_->Activate();
  }
  void NotifyWidgetDeactivated(MahoCommandActionSelectorView* selector) {
    selector->OnWidgetActivationChanged(host_widget_.get(), false);
  }
  void DestroySelector() { host_widget_.reset(); }

  void ExpectVisibleNonOverlappingSegmentBounds(
      MahoCommandActionSelectorView* selector) {
    views::Button* search = search_button(selector);
    views::Button* ask_maho = ask_maho_button(selector);
    const gfx::Rect search_bounds = search->bounds();
    const gfx::Rect ask_maho_bounds = ask_maho->bounds();

    EXPECT_TRUE(search->GetVisible());
    EXPECT_TRUE(ask_maho->GetVisible());
    EXPECT_FALSE(search_bounds.IsEmpty());
    EXPECT_FALSE(ask_maho_bounds.IsEmpty());
    EXPECT_FALSE(search_bounds.Intersects(ask_maho_bounds));
  }

 private:
  std::unique_ptr<views::Widget> host_widget_;
};

TEST_F(MahoCommandActionSelectorViewTest,
       SelectedSegmentOwnsOnlyFlowBackgroundAndExpectedAccent) {
  auto* selector = CreateSelector();
  const auto* colors = selector->GetColorProvider();
  ASSERT_NE(nullptr, colors);

  EXPECT_TRUE(HasFlowBackground(selector, PaletteAction::kSearch));
  EXPECT_TRUE(IsFlowAnimating(selector, PaletteAction::kSearch));
  EXPECT_FALSE(HasFlowBackground(selector, PaletteAction::kAskMaho));
  EXPECT_EQ(colors->GetColor(kMahoColorAccentBlue),
            FlowAccent(selector, PaletteAction::kSearch));

  selector->SetSelected(PaletteAction::kAskMaho);

  EXPECT_FALSE(HasFlowBackground(selector, PaletteAction::kSearch));
  EXPECT_TRUE(HasFlowBackground(selector, PaletteAction::kAskMaho));
  EXPECT_TRUE(IsFlowAnimating(selector, PaletteAction::kAskMaho));
  EXPECT_EQ(colors->GetColor(kMahoColorAccentBlueBright),
            FlowAccent(selector, PaletteAction::kAskMaho));
}

TEST_F(MahoCommandActionSelectorViewTest,
       FlowAnimationStopsOnDeselectDisableAndDestruction) {
  ASSERT_EQ(0, RunningFlowCount());
  auto* selector = CreateSelector();
  EXPECT_EQ(1, RunningFlowCount());

  selector->SetSelected(PaletteAction::kAskMaho);
  EXPECT_FALSE(IsFlowAnimating(selector, PaletteAction::kSearch));
  EXPECT_TRUE(IsFlowAnimating(selector, PaletteAction::kAskMaho));
  EXPECT_EQ(1, RunningFlowCount());

  selector->SetEnabled(false);
  EXPECT_FALSE(IsFlowAnimating(selector, PaletteAction::kAskMaho));
  EXPECT_EQ(0, RunningFlowCount());

  selector->SetEnabled(true);
  EXPECT_TRUE(IsFlowAnimating(selector, PaletteAction::kAskMaho));
  EXPECT_EQ(1, RunningFlowCount());

  DestroySelector();
  EXPECT_EQ(0, RunningFlowCount());
}

TEST_F(MahoCommandActionSelectorViewTest,
       FlowTracksVisibilityAndIgnoresWidgetActivation) {
  ASSERT_EQ(0, RunningFlowCount());
  auto* selector = CreateSelector();
  ASSERT_TRUE(IsFlowAnimating(selector, PaletteAction::kSearch));

  selector->SetVisible(false);
  EXPECT_FALSE(IsFlowAnimating(selector, PaletteAction::kSearch));
  EXPECT_EQ(0, RunningFlowCount());

  selector->SetVisible(true);
  EXPECT_TRUE(IsFlowAnimating(selector, PaletteAction::kSearch));
  EXPECT_EQ(1, RunningFlowCount());

  HideWidget();
  EXPECT_FALSE(IsFlowAnimating(selector, PaletteAction::kSearch));
  EXPECT_EQ(0, RunningFlowCount());

  ShowAndActivateWidget();
  EXPECT_TRUE(IsFlowAnimating(selector, PaletteAction::kSearch));
  EXPECT_EQ(1, RunningFlowCount());

  NotifyWidgetDeactivated(selector);
  EXPECT_TRUE(IsFlowAnimating(selector, PaletteAction::kSearch));
  EXPECT_EQ(1, RunningFlowCount());
}

TEST_F(MahoCommandActionSelectorViewTest,
       FlowKeepsSegmentGeometryTextColorsAndOuterChrome) {
  auto* selector = CreateSelector();
  auto* search = search_button(selector);
  auto* ask = ask_maho_button(selector);
  const auto* colors = selector->GetColorProvider();
  ASSERT_NE(nullptr, colors);
  auto* outer_background = selector->GetBackground();
  auto* outer_border = selector->GetBorder();

  EXPECT_EQ(gfx::Size(64, 26), search->GetPreferredSize());
  EXPECT_EQ(gfx::Size(64, 26), ask->GetPreferredSize());
  ASSERT_NE(nullptr, search->GetBackground());
  const std::optional<gfx::RoundedCornersF> radii =
      search->GetBackground()->GetRoundedCornerRadii();
  ASSERT_TRUE(radii.has_value());
  EXPECT_EQ(gfx::RoundedCornersF(
                static_cast<float>(SegmentCornerRadius())),
            *radii);
  EXPECT_EQ(colors->GetColor(kMahoColorPrimaryText),
            SegmentTextColor(selector, PaletteAction::kSearch));
  EXPECT_EQ(colors->GetColor(ui::kColorLabelForegroundSecondary),
            SegmentTextColor(selector, PaletteAction::kAskMaho));

  selector->SetSelected(PaletteAction::kAskMaho);

  EXPECT_EQ(outer_background, selector->GetBackground());
  EXPECT_EQ(outer_border, selector->GetBorder());
  EXPECT_EQ(colors->GetColor(ui::kColorLabelForegroundSecondary),
            SegmentTextColor(selector, PaletteAction::kSearch));
  EXPECT_EQ(colors->GetColor(kMahoColorPrimaryText),
            SegmentTextColor(selector, PaletteAction::kAskMaho));
  EXPECT_EQ(gfx::Size(64, 26), search->GetPreferredSize());
  EXPECT_EQ(gfx::Size(64, 26), ask->GetPreferredSize());
}

TEST_F(MahoCommandActionSelectorViewTest, InitialSelectionIsSearch) {
  // Given a newly created action selector.
  auto* selector = CreateSelector();

  // When its initial selection is queried.
  const PaletteAction selected = selector->selected();

  // Then Search is selected by default.
  EXPECT_EQ(PaletteAction::kSearch, selected);
}

TEST_F(MahoCommandActionSelectorViewTest, ChangingSelectionRunsCallbackOnce) {
  // Given a selector that records selection changes.
  int change_count = 0;
  PaletteAction last_action = PaletteAction::kSearch;
  auto cb = base::BindRepeating(
      [](int* count, PaletteAction* out, PaletteAction action) {
        ++(*count);
        *out = action;
      },
      &change_count, &last_action);
  auto* selector = CreateSelector(cb);

  // When the selection changes to Ask Maho.
  selector->SetSelected(PaletteAction::kAskMaho);

  // Then the callback reports the new selection exactly once.
  EXPECT_EQ(1, change_count);
  EXPECT_EQ(PaletteAction::kAskMaho, last_action);
}

TEST_F(MahoCommandActionSelectorViewTest, SelectingCurrentActionIsNoOp) {
  // Given a selector already set to Ask Maho.
  int change_count = 0;
  auto* selector = CreateSelector(base::BindRepeating(
      [](int* count, PaletteAction) { ++(*count); }, &change_count));
  selector->SetSelected(PaletteAction::kAskMaho);
  ASSERT_EQ(1, change_count);

  // When Ask Maho is selected again.
  selector->SetSelected(PaletteAction::kAskMaho);

  // Then no second change is reported.
  EXPECT_EQ(1, change_count);
}

TEST_F(MahoCommandActionSelectorViewTest,
       AccessibilityRolesExposeRadioGroupAndRadioButtons) {
  // Given a newly created action selector.
  auto* selector = CreateSelector();

  // When accessibility data is read from the group and both segments.
  ui::AXNodeData group_data;
  ui::AXNodeData search_data;
  ui::AXNodeData ask_maho_data;
  selector->GetViewAccessibility().GetAccessibleNodeData(&group_data);
  search_button(selector)->GetViewAccessibility().GetAccessibleNodeData(
      &search_data);
  ask_maho_button(selector)->GetViewAccessibility().GetAccessibleNodeData(
      &ask_maho_data);

  // Then the selector is a radio group containing radio buttons.
  EXPECT_EQ(ax::mojom::Role::kRadioGroup, group_data.role);
  EXPECT_EQ(ax::mojom::Role::kRadioButton, search_data.role);
  EXPECT_EQ(ax::mojom::Role::kRadioButton, ask_maho_data.role);
}

TEST_F(MahoCommandActionSelectorViewTest,
       InitialAccessibleSelectionMarksOnlySearchSelected) {
  // Given a newly created action selector.
  auto* selector = CreateSelector();

  // When accessibility selection state is read from both segments.
  ui::AXNodeData search_data;
  ui::AXNodeData ask_maho_data;
  search_button(selector)->GetViewAccessibility().GetAccessibleNodeData(
      &search_data);
  ask_maho_button(selector)->GetViewAccessibility().GetAccessibleNodeData(
      &ask_maho_data);

  // Then only the Search radio button is selected.
  EXPECT_TRUE(
      search_data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
  EXPECT_FALSE(
      ask_maho_data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
}

TEST_F(MahoCommandActionSelectorViewTest,
       ChangingSelectionUpdatesAccessibleSelectedState) {
  // Given a newly created action selector.
  auto* selector = CreateSelector();

  // When the selection changes to Ask Maho.
  selector->SetSelected(PaletteAction::kAskMaho);

  // Then accessibility marks only Ask Maho as selected.
  ui::AXNodeData search_data;
  ui::AXNodeData ask_maho_data;
  search_button(selector)->GetViewAccessibility().GetAccessibleNodeData(
      &search_data);
  ask_maho_button(selector)->GetViewAccessibility().GetAccessibleNodeData(
      &ask_maho_data);
  EXPECT_FALSE(
      search_data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
  EXPECT_TRUE(
      ask_maho_data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
}

TEST_F(MahoCommandActionSelectorViewTest,
       SelectionChangeFiresSelectedChildrenChangedAccessibilityEvent) {
  // Given an action selector observed by the real accessibility event counter.
  auto* selector = CreateSelector();
  views::test::AXEventCounter counter(views::AXUpdateNotifier::Get());

  // When the selection changes to Ask Maho.
  selector->SetSelected(PaletteAction::kAskMaho);

  // Then the radio group announces that its selected child changed.
  EXPECT_EQ(1, counter.GetCount(ax::mojom::Event::kSelectedChildrenChanged,
                                selector));
}

TEST_F(MahoCommandActionSelectorViewTest, HoveringSegmentUpdatesButtonState) {
  // Given a selector whose Search segment is idle.
  auto* selector = CreateSelector();
  views::Button* search = search_button(selector);
  ASSERT_EQ(views::Button::STATE_NORMAL, search->GetState());
  const ui::MouseEvent enter_event(ui::EventType::kMouseEntered, gfx::Point(),
                                   gfx::Point(), ui::EventTimeForNow(),
                                   ui::EF_NONE, ui::EF_NONE);
  const ui::MouseEvent exit_event(ui::EventType::kMouseExited, gfx::Point(),
                                  gfx::Point(), ui::EventTimeForNow(),
                                  ui::EF_NONE, ui::EF_NONE);

  // When real mouse enter and exit events are delivered to the segment.
  search->OnMouseEntered(enter_event);
  EXPECT_EQ(views::Button::STATE_HOVERED, search->GetState());
  search->OnMouseExited(exit_event);

  // Then the segment returns to its idle state.
  EXPECT_EQ(views::Button::STATE_NORMAL, search->GetState());
}

TEST_F(MahoCommandActionSelectorViewTest, SegmentsExposeKeyboardFocusBehavior) {
  // Given a newly created action selector.
  auto* selector = CreateSelector();

  // When each segment's focus behavior is queried.
  const auto search_focus_behavior =
      search_button(selector)->GetFocusBehavior();
  const auto ask_maho_focus_behavior =
      ask_maho_button(selector)->GetFocusBehavior();

  // Then both segments participate in keyboard focus traversal.
  EXPECT_EQ(views::View::FocusBehavior::ALWAYS, search_focus_behavior);
  EXPECT_EQ(views::View::FocusBehavior::ALWAYS, ask_maho_focus_behavior);
}

TEST_F(MahoCommandActionSelectorViewTest, KeyboardFocusCanMoveBetweenSegments) {
  // Given a visible selector with two keyboard-focusable segments.
  auto* selector = CreateSelector();
  views::Button* search = search_button(selector);
  views::Button* ask_maho = ask_maho_button(selector);

  // When keyboard focus is requested first on Search and then on Ask Maho.
  search->RequestFocus();
  ASSERT_TRUE(search->HasFocus());
  ask_maho->RequestFocus();

  // Then focus moves to Ask Maho and leaves Search.
  EXPECT_FALSE(search->HasFocus());
  EXPECT_TRUE(ask_maho->HasFocus());
}

TEST_F(MahoCommandActionSelectorViewTest,
       ProgrammaticSelectionStillWorksWhenDisabled) {
  // Given a disabled action selector.
  int change_count = 0;
  auto* selector = CreateSelector(base::BindRepeating(
      [](int* count, PaletteAction) { ++(*count); }, &change_count));
  selector->SetEnabled(false);

  // When its selection is changed programmatically.
  selector->SetSelected(PaletteAction::kAskMaho);

  // Then the selection and callback still update.
  EXPECT_EQ(PaletteAction::kAskMaho, selector->selected());
  EXPECT_EQ(1, change_count);
}

TEST_F(MahoCommandActionSelectorViewTest, DisabledStateBlocksInteraction) {
  // Given a disabled action selector with Search selected.
  int change_count = 0;
  auto* selector = CreateSelector(base::BindRepeating(
      [](int* count, PaletteAction) { ++(*count); }, &change_count));
  selector->SetEnabled(false);
  ASSERT_FALSE(selector->enabled());

  // When interaction attempts to select Ask Maho.
  SelectFromInteraction(selector, PaletteAction::kAskMaho);

  // Then the selection and callback remain unchanged.
  EXPECT_EQ(PaletteAction::kSearch, selector->selected());
  EXPECT_EQ(0, change_count);
}

TEST_F(MahoCommandActionSelectorViewTest,
       SegmentsHaveStableNonOverlappingBoundsAfterLayout) {
  // Given a selector with enough width for both action segments and the hint.
  auto* selector = CreateSelector();
  selector->SetSize(gfx::Size(400, 40));

  // When layout runs.
  selector->DeprecatedLayoutImmediately();

  // Then both action segments are visible, laid out, and non-overlapping.
  ExpectVisibleNonOverlappingSegmentBounds(selector);
  const gfx::Rect search_bounds = search_button(selector)->bounds();
  const gfx::Rect ask_maho_bounds = ask_maho_button(selector)->bounds();
  selector->DeprecatedLayoutImmediately();

  EXPECT_EQ(search_bounds, search_button(selector)->bounds());
  EXPECT_EQ(ask_maho_bounds, ask_maho_button(selector)->bounds());

  selector->SetSelected(PaletteAction::kAskMaho);
  selector->DeprecatedLayoutImmediately();

  EXPECT_EQ(search_bounds, search_button(selector)->bounds());
  EXPECT_EQ(ask_maho_bounds, ask_maho_button(selector)->bounds());
}

TEST_F(MahoCommandActionSelectorViewTest,
       SelectorPaintsToNonOpaqueLayer) {
  auto* selector = CreateSelector();

  ASSERT_NE(nullptr, selector->layer());
  EXPECT_FALSE(selector->layer()->fills_bounds_opaquely());
}

TEST_F(MahoCommandActionSelectorViewTest, InlineHintStaysHiddenAcrossWidths) {
  auto* selector = CreateSelector();
  selector->SetSize(gfx::Size(300, 40));
  selector->DeprecatedLayoutImmediately();
  EXPECT_FALSE(hint_label(selector)->GetVisible());
  ExpectVisibleNonOverlappingSegmentBounds(selector);

  selector->SetSize(gfx::Size(299, 40));
  selector->DeprecatedLayoutImmediately();

  EXPECT_FALSE(hint_label(selector)->GetVisible());
  ExpectVisibleNonOverlappingSegmentBounds(selector);
  const gfx::Rect search_bounds = search_button(selector)->bounds();
  const gfx::Rect ask_maho_bounds = ask_maho_button(selector)->bounds();
  selector->DeprecatedLayoutImmediately();

  EXPECT_EQ(search_bounds, search_button(selector)->bounds());
  EXPECT_EQ(ask_maho_bounds, ask_maho_button(selector)->bounds());
}

TEST_F(MahoCommandActionSelectorViewTest,
       TranslucentLabelsDisableSubpixelRendering) {
  auto* selector = CreateSelector();
  EXPECT_FALSE(
      SegmentUsesSubpixelRendering(selector, PaletteAction::kSearch));
  EXPECT_FALSE(
      SegmentUsesSubpixelRendering(selector, PaletteAction::kAskMaho));
  EXPECT_FALSE(hint_label(selector)->GetSubpixelRenderingEnabled());
}

}  // namespace maho
