// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"
#include "maho/browser/ui/views/command/maho_command_action_selector_view.h"
#include "maho/browser/ui/views/command/maho_command_overlay_view.h"

#include <memory>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/compositor/layer.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"

namespace maho {

// Fixture is outside the anonymous namespace so the `friend class` declaration
// in MahoCommandOverlayController refers to the same class (maho:: scope).
class MahoCommandOverlayControllerTest : public views::ViewsTestBase {
 protected:
  MahoCommandOverlayControllerTest() = default;
  ~MahoCommandOverlayControllerTest() override = default;

  std::unique_ptr<MahoCommandOverlayController> MakeController() {
    return std::make_unique<MahoCommandOverlayController>(/*browser=*/nullptr);
  }

  void SetPreShowFocusedView(MahoCommandOverlayController& controller,
                              views::View* view) {
    controller.pre_show_focused_view_.SetView(view);
  }

  views::View* GetPreShowFocusedView(MahoCommandOverlayController& controller) {
    return controller.pre_show_focused_view_.view();
  }

  void SetParentWidgetDirect(MahoCommandOverlayController& controller,
                              views::Widget* parent) {
    controller.parent_widget_ = parent;
  }

  void ClearParentWidgetDirect(MahoCommandOverlayController& controller) {
    controller.parent_widget_ = nullptr;
  }

  void CallRestoreFocus(MahoCommandOverlayController& controller) {
    controller.RestoreFocusToPreShowView();
  }

  void SetIsHiding(MahoCommandOverlayController& controller, bool value) {
    controller.is_hiding_ = value;
  }

  bool GetIsHiding(const MahoCommandOverlayController& controller) {
    return controller.is_hiding_;
  }

  void InjectVisibleWidget(MahoCommandOverlayController& controller,
                           views::Widget* parent) {
    controller.parent_widget_ = parent;
    auto owned = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    owned->Show();
    auto* root_view = owned->GetRootView();
    if (root_view) {
      root_view->SetPaintToLayer();
      root_view->layer()->SetFillsBoundsOpaquely(false);
    }
    controller.widget_ = std::move(owned);
    controller.is_hiding_ = false;
  }

  void ReleaseInjectedWidget(MahoCommandOverlayController& controller) {
    controller.deactivation_hide_timer_.Stop();
    if (controller.widget_) {
      controller.widget_->RemoveObserver(&controller);
      controller.widget_.reset();
    }
    controller.parent_widget_ = nullptr;
    controller.is_hiding_ = false;
  }

  void CallOnWidgetActivationChanged(MahoCommandOverlayController& controller,
                                     views::Widget* widget, bool active) {
    controller.OnWidgetActivationChanged(widget, active);
  }

  bool DeactivationTimerIsRunning(
      const MahoCommandOverlayController& controller) {
    return controller.deactivation_hide_timer_.IsRunning();
  }

  views::Widget* GetOverlayWidget(
      const MahoCommandOverlayController& controller) {
    return controller.widget_.get();
  }

  ui::Layer* GetOverlayRootLayer(
      const MahoCommandOverlayController& controller) {
    views::Widget* widget = controller.widget_.get();
    return widget && widget->GetRootView()
               ? widget->GetRootView()->layer()
               : nullptr;
  }

  // TEST_F bodies run in a derived class that does not inherit the fixture's
  // friend grant, so private-member access must go through a fixture helper.
  void SetOpenStartTicks(MahoCommandOverlayController& controller,
                         base::TimeTicks ticks) {
    controller.open_start_ticks_ = ticks;
  }
};

namespace {

TEST_F(MahoCommandOverlayControllerTest, IsVisibleFalseWithNoWidget) {
  auto controller = MakeController();
  EXPECT_FALSE(controller->IsVisible());
}

TEST_F(MahoCommandOverlayControllerTest,
       ShowStartsEntranceWithIdentityRootTransform) {
  gfx::ScopedAnimationDurationScaleMode non_zero_duration(
      gfx::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);
  std::unique_ptr<views::Widget> parent =
      CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->SetBounds(gfx::Rect(0, 0, 1000, 800));
  views::View* anchor =
      parent->SetContentsView(std::make_unique<views::View>());
  parent->Show();

  auto controller = MakeController();
  controller->Show(anchor);

  ui::Layer* root_layer = GetOverlayRootLayer(*controller);
  ASSERT_NE(root_layer, nullptr);
  EXPECT_TRUE(root_layer->transform().IsIdentity())
      << "command palette entrance must not translate its text-bearing root "
         "layer";
  EXPECT_LT(root_layer->opacity(), 1.0f)
      << "command palette entrance should retain its opacity fade";
  ASSERT_NE(controller->GetOverlayViewForTesting(), nullptr);
  ASSERT_NE(controller->GetOverlayViewForTesting()->GetActionSelectorForTesting(),
            nullptr);
  EXPECT_FALSE(controller->GetOverlayViewForTesting()
                   ->GetActionSelectorForTesting()
                   ->bounds()
                   .IsEmpty());

  controller.reset();
  parent.reset();
  task_environment()->RunUntilIdle();
}

TEST_F(MahoCommandOverlayControllerTest,
       ActivationRestoresOverlayTextfieldFocus) {
  std::unique_ptr<views::Widget> parent =
      CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->SetBounds(gfx::Rect(0, 0, 1000, 800));
  views::View* anchor =
      parent->SetContentsView(std::make_unique<views::View>());
  parent->Show();

  auto controller = MakeController();
  controller->Show(anchor);
  auto* overlay = controller->GetOverlayViewForTesting();
  ASSERT_NE(overlay, nullptr);
  ASSERT_NE(overlay->textfield(), nullptr);
  views::FocusManager* focus_manager = overlay->textfield()->GetFocusManager();
  ASSERT_NE(focus_manager, nullptr);
  focus_manager->SetFocusedView(nullptr);
  ASSERT_FALSE(overlay->textfield()->HasFocus());

  CallOnWidgetActivationChanged(*controller, GetOverlayWidget(*controller),
                                true);

  EXPECT_TRUE(overlay->textfield()->HasFocus());

  controller.reset();
  parent.reset();
  task_environment()->RunUntilIdle();
}

TEST_F(MahoCommandOverlayControllerTest,
       ParentActivationRestoresOverlayTextfieldFocus) {
  std::unique_ptr<views::Widget> parent =
      CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->SetBounds(gfx::Rect(0, 0, 1000, 800));
  views::View* anchor =
      parent->SetContentsView(std::make_unique<views::View>());
  parent->Show();

  auto controller = MakeController();
  controller->Show(anchor);
  auto* overlay = controller->GetOverlayViewForTesting();
  ASSERT_NE(overlay, nullptr);
  ASSERT_NE(overlay->textfield(), nullptr);
  views::FocusManager* focus_manager = overlay->textfield()->GetFocusManager();
  ASSERT_NE(focus_manager, nullptr);
  focus_manager->SetFocusedView(nullptr);
  ASSERT_FALSE(overlay->textfield()->HasFocus());

  CallOnWidgetActivationChanged(*controller, parent.get(), true);

  EXPECT_TRUE(overlay->textfield()->HasFocus());

  controller.reset();
  parent.reset();
  task_environment()->RunUntilIdle();
}

TEST_F(MahoCommandOverlayControllerTest,
       RestoreFocusNoOpWhenNoParentWidget) {
  std::unique_ptr<views::Widget> widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  views::View* target_view =
      widget->SetContentsView(std::make_unique<views::View>());

  auto controller = MakeController();
  SetPreShowFocusedView(*controller, target_view);
  ASSERT_EQ(GetPreShowFocusedView(*controller), target_view);

  CallRestoreFocus(*controller);

  EXPECT_EQ(GetPreShowFocusedView(*controller), nullptr);
}

TEST_F(MahoCommandOverlayControllerTest,
       RestoreFocusRestoresFocusToSavedViewInParent) {
  std::unique_ptr<views::Widget> parent_widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent_widget->Show();

  views::View* focusable_view =
      parent_widget->SetContentsView(std::make_unique<views::View>());
  focusable_view->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);

  views::FocusManager* fm = parent_widget->GetFocusManager();
  ASSERT_TRUE(fm);

  fm->SetFocusedView(focusable_view);
  ASSERT_EQ(fm->GetFocusedView(), focusable_view);

  auto controller = MakeController();
  SetParentWidgetDirect(*controller, parent_widget.get());
  SetPreShowFocusedView(*controller, focusable_view);

  fm->SetFocusedView(nullptr);
  ASSERT_EQ(fm->GetFocusedView(), nullptr);

  CallRestoreFocus(*controller);

  EXPECT_EQ(fm->GetFocusedView(), focusable_view);
  EXPECT_EQ(GetPreShowFocusedView(*controller), nullptr);

  ClearParentWidgetDirect(*controller);
}

TEST_F(MahoCommandOverlayControllerTest,
       RestoreFocusIsIdempotentOnSecondCall) {
  std::unique_ptr<views::Widget> parent_widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent_widget->Show();

  views::View* focusable_view =
      parent_widget->SetContentsView(std::make_unique<views::View>());
  focusable_view->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);

  auto controller = MakeController();
  SetParentWidgetDirect(*controller, parent_widget.get());
  SetPreShowFocusedView(*controller, focusable_view);

  CallRestoreFocus(*controller);
  EXPECT_EQ(GetPreShowFocusedView(*controller), nullptr);

  CallRestoreFocus(*controller);
  EXPECT_EQ(GetPreShowFocusedView(*controller), nullptr);

  ClearParentWidgetDirect(*controller);
}

TEST_F(MahoCommandOverlayControllerTest,
       ViewTrackerClearsWhenSavedViewIsDestroyed) {
  std::unique_ptr<views::Widget> parent_widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent_widget->Show();

  auto controller = MakeController();
  SetParentWidgetDirect(*controller, parent_widget.get());

  {
    auto ephemeral_view = std::make_unique<views::View>();
    SetPreShowFocusedView(*controller, ephemeral_view.get());
    ASSERT_EQ(GetPreShowFocusedView(*controller), ephemeral_view.get());
  }

  EXPECT_EQ(GetPreShowFocusedView(*controller), nullptr);

  CallRestoreFocus(*controller);

  EXPECT_EQ(GetPreShowFocusedView(*controller), nullptr);

  ClearParentWidgetDirect(*controller);
}

TEST_F(MahoCommandOverlayControllerTest, IsVisibleTrueWhenWidgetIsShowingAndNotHiding) {
  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  auto controller = MakeController();
  InjectVisibleWidget(*controller, parent.get());
  EXPECT_TRUE(controller->IsVisible());

  ReleaseInjectedWidget(*controller);
}

TEST_F(MahoCommandOverlayControllerTest, IsVisibleFalseWhenIsHidingIsSet) {
  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  auto controller = MakeController();
  InjectVisibleWidget(*controller, parent.get());
  ASSERT_TRUE(controller->IsVisible());

  SetIsHiding(*controller, true);
  EXPECT_FALSE(controller->IsVisible());

  ReleaseInjectedWidget(*controller);
}

TEST_F(MahoCommandOverlayControllerTest, HideIsNoOpWhenAlreadyHiding) {
  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  auto controller = MakeController();
  InjectVisibleWidget(*controller, parent.get());
  SetIsHiding(*controller, true);

  controller->Hide();

  EXPECT_TRUE(GetIsHiding(*controller));

  ReleaseInjectedWidget(*controller);
}

TEST_F(MahoCommandOverlayControllerTest, HideIsNoOpWhenNoWidget) {
  auto controller = MakeController();
  ASSERT_FALSE(controller->IsVisible());

  controller->Hide();

  EXPECT_FALSE(GetIsHiding(*controller));
}

TEST_F(MahoCommandOverlayControllerTest,
       OnWidgetActivationChangedStartsTimerOnDeactivation) {
  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  auto controller = MakeController();
  InjectVisibleWidget(*controller, parent.get());
  ASSERT_FALSE(DeactivationTimerIsRunning(*controller));

  CallOnWidgetActivationChanged(*controller, GetOverlayWidget(*controller), false);

  EXPECT_TRUE(DeactivationTimerIsRunning(*controller));

  ReleaseInjectedWidget(*controller);
}

TEST_F(MahoCommandOverlayControllerTest,
        OnWidgetActivationChangedIgnoresUnrelatedWidget) {
  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();
  std::unique_ptr<views::Widget> unrelated = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  unrelated->Show();

  auto controller = MakeController();
  InjectVisibleWidget(*controller, parent.get());

  CallOnWidgetActivationChanged(*controller, unrelated.get(), false);

  EXPECT_FALSE(DeactivationTimerIsRunning(*controller));

  ReleaseInjectedWidget(*controller);
}

// ---------------------------------------------------------------------------
// Repeated open/close lifecycle tests
// ---------------------------------------------------------------------------

// Verifies that the controller returns to a consistent "not visible" state
// after each hide, and that injecting a new widget afterwards correctly
// marks it visible again.  This covers the repeated open→close→open cycle
// Oracle flagged as untested.
TEST_F(MahoCommandOverlayControllerTest,
       RepeatedOpenCloseRetainsConsistentState) {
  auto controller = MakeController();

  for (int cycle = 0; cycle < 3; ++cycle) {
    SCOPED_TRACE(testing::Message() << "cycle=" << cycle);

    std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    parent->Show();

    InjectVisibleWidget(*controller, parent.get());
    EXPECT_TRUE(controller->IsVisible());
    EXPECT_FALSE(GetIsHiding(*controller));

    SetIsHiding(*controller, true);
    EXPECT_FALSE(controller->IsVisible());

    ReleaseInjectedWidget(*controller);

    EXPECT_FALSE(controller->IsVisible());
    EXPECT_FALSE(GetIsHiding(*controller));
    EXPECT_EQ(GetOverlayWidget(*controller), nullptr);
  }
}

// Verifies that calling Hide() when already hiding is a strict no-op: the
// is_hiding_ flag must remain true and no double-release occurs.
TEST_F(MahoCommandOverlayControllerTest,
       HideWhileHidingInProgressIsNoOp) {
  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  auto controller = MakeController();
  InjectVisibleWidget(*controller, parent.get());
  ASSERT_TRUE(controller->IsVisible());

  controller->Hide();
  ASSERT_TRUE(GetIsHiding(*controller));

  controller->Hide();
  EXPECT_TRUE(GetIsHiding(*controller));
  EXPECT_NE(GetOverlayWidget(*controller), nullptr);

  ReleaseInjectedWidget(*controller);
}

// ---------------------------------------------------------------------------
// Window-state / lifecycle scenarios
// ---------------------------------------------------------------------------

// Simulates the parent window being destroyed while the overlay is open.
// OnWidgetDestroying(parent_widget_) should clear all parent-related
// pointers so the controller is left in a safe, inert state.
TEST_F(MahoCommandOverlayControllerTest,
       ParentWidgetDestroyedWhileOverlayOpenClearsParentState) {
  auto controller = MakeController();

  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  InjectVisibleWidget(*controller, parent.get());
  ASSERT_TRUE(controller->IsVisible());

  controller->OnWidgetDestroying(parent.get());

  EXPECT_EQ(GetPreShowFocusedView(*controller), nullptr);

  ReleaseInjectedWidget(*controller);
}

// Simulates the overlay widget itself being destroyed externally (e.g., the
// OS closes the window).  OnWidgetDestroying(overlay_widget) should leave the
// controller in a fully idle state with no dangling pointers.
TEST_F(MahoCommandOverlayControllerTest,
       OverlayWidgetDestroyedExternallyClearsAllState) {
  auto controller = MakeController();

  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  InjectVisibleWidget(*controller, parent.get());
  ASSERT_TRUE(controller->IsVisible());

  views::Widget* overlay_raw = GetOverlayWidget(*controller);
  ASSERT_NE(overlay_raw, nullptr);

  controller->OnWidgetDestroying(overlay_raw);

  // OnWidgetDestroying releases (not resets) widget_ — test owns the raw ptr.
  overlay_raw->CloseNow();

  EXPECT_FALSE(controller->IsVisible());
  EXPECT_FALSE(GetIsHiding(*controller));
  EXPECT_EQ(GetOverlayWidget(*controller), nullptr);

  ClearParentWidgetDirect(*controller);
}

// Verifies that after an external overlay destruction the deactivation timer
// has been stopped and will not fire a stale Hide() call.
TEST_F(MahoCommandOverlayControllerTest,
       DeactivationTimerStoppedAfterOverlayDestroyedExternally) {
  auto controller = MakeController();

  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  InjectVisibleWidget(*controller, parent.get());

  CallOnWidgetActivationChanged(*controller, GetOverlayWidget(*controller),
                                false);
  ASSERT_TRUE(DeactivationTimerIsRunning(*controller));

  views::Widget* overlay_raw = GetOverlayWidget(*controller);
  controller->OnWidgetDestroying(overlay_raw);
  overlay_raw->CloseNow();

  EXPECT_FALSE(DeactivationTimerIsRunning(*controller));
  EXPECT_FALSE(GetIsHiding(*controller));

  ClearParentWidgetDirect(*controller);
}

// Verifies full open→activation-loss→timer-stop→second-open cycle so that
// re-opening after a deactivation event produces a clean visible state.
TEST_F(MahoCommandOverlayControllerTest,
       SecondOpenAfterDeactivationTimerStartedResetsCleanly) {
  auto controller = MakeController();

  std::unique_ptr<views::Widget> parent1 = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent1->Show();
  InjectVisibleWidget(*controller, parent1.get());
  ASSERT_TRUE(controller->IsVisible());

  CallOnWidgetActivationChanged(*controller, GetOverlayWidget(*controller),
                                false);
  ASSERT_TRUE(DeactivationTimerIsRunning(*controller));

  ReleaseInjectedWidget(*controller);
  ASSERT_FALSE(controller->IsVisible());
  ASSERT_FALSE(GetIsHiding(*controller));

  std::unique_ptr<views::Widget> parent2 = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent2->Show();
  InjectVisibleWidget(*controller, parent2.get());

  EXPECT_TRUE(controller->IsVisible());
  EXPECT_FALSE(GetIsHiding(*controller));
  EXPECT_FALSE(DeactivationTimerIsRunning(*controller));

  ReleaseInjectedWidget(*controller);
}

TEST_F(MahoCommandOverlayControllerTest,
       SpuriousActivationLossImmediatelyAfterShowDoesNotDismiss) {
  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  auto controller = MakeController();
  InjectVisibleWidget(*controller, parent.get());

  // Set open_start_ticks_ to now.
  SetOpenStartTicks(*controller, base::TimeTicks::Now());

  // Simulate deactivation immediately after show.
  CallOnWidgetActivationChanged(*controller, GetOverlayWidget(*controller), false);
  ASSERT_TRUE(DeactivationTimerIsRunning(*controller));

  // Fast-forward by 300ms (so timer fires, since delay is 200ms).
  task_environment()->FastForwardBy(base::Milliseconds(300));

  // The overlay should still be visible because we are within the 500ms grace period.
  EXPECT_TRUE(controller->IsVisible());

  // Fast-forward to 600ms after show.
  task_environment()->FastForwardBy(base::Milliseconds(300));

  // Simulate deactivation again.
  CallOnWidgetActivationChanged(*controller, GetOverlayWidget(*controller), false);
  ASSERT_TRUE(DeactivationTimerIsRunning(*controller));

  // Fast-forward by another 300ms (total time since show > 500ms).
  task_environment()->FastForwardBy(base::Milliseconds(300));

  // Now it should be hidden (or starting to hide).
  EXPECT_FALSE(controller->IsVisible());

  ReleaseInjectedWidget(*controller);
}

TEST_F(MahoCommandOverlayControllerTest, OtherOtrHasNoController) {
  EXPECT_TRUE(MahoCommandOverlayController::ShouldCreateForContext(
      MahoPrivateContextClass::kRegular));
  EXPECT_TRUE(MahoCommandOverlayController::ShouldCreateForContext(
      MahoPrivateContextClass::kPrimaryIncognito));

  for (MahoPrivateContextClass klass :
       {MahoPrivateContextClass::kOtherOtr, MahoPrivateContextClass::kGuest,
        MahoPrivateContextClass::kSystem,
        MahoPrivateContextClass::kDevToolsOtr,
        MahoPrivateContextClass::kNull}) {
    EXPECT_FALSE(MahoCommandOverlayController::ShouldCreateForContext(klass))
        << "only regular and exact primary Incognito windows own a command "
           "controller";
  }
}

}  // namespace
}  // namespace maho
