// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/spaces_overlay/maho_spaces_overlay_controller.h"

#include <memory>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"
#include "ui/compositor/layer.h"

namespace maho {

// ---------------------------------------------------------------------------
// Test fixture
//
// Mirrors the pattern in maho_command_overlay_controller_unittest.cc.
// The fixture is declared outside the anonymous namespace so that the
// `friend class MahoSpacesOverlayControllerTest` declaration in the header
// resolves to this class (maho:: scope). Private members are accessed through
// white-box helpers on the fixture; the controller itself is never modified.
// ---------------------------------------------------------------------------

class MahoSpacesOverlayControllerTest : public views::ViewsTestBase {
 protected:
  MahoSpacesOverlayControllerTest()
      : non_zero_duration_(gfx::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION) {}
  ~MahoSpacesOverlayControllerTest() override = default;

  // Factory — passes null Browser (sufficient for unit tests that do not
  // exercise browser-dependent view content).
  std::unique_ptr<MahoSpacesOverlayController> MakeController() {
    auto ctrl = std::make_unique<MahoSpacesOverlayController>(/*browser=*/nullptr);
    ctrl->set_disable_animation_for_testing(true);
    return ctrl;
  }

  // --- Private-state accessors ---

  bool GetIsHiding(const MahoSpacesOverlayController& ctrl) const {
    return ctrl.is_hiding_;
  }

  bool GetIsShowing(const MahoSpacesOverlayController& ctrl) const {
    return ctrl.is_showing_;
  }

  views::Widget* GetWidget(const MahoSpacesOverlayController& ctrl) const {
    return ctrl.widget_.get();
  }

  views::Widget* GetParentWidget(
      const MahoSpacesOverlayController& ctrl) const {
    return ctrl.parent_widget_;
  }

  const views::View* GetFocusRestoreView(
      const MahoSpacesOverlayController& ctrl) const {
    return ctrl.focus_restore_tracker_.view();
  }

  void SetParentWidgetDirect(MahoSpacesOverlayController& ctrl,
                              views::Widget* parent) {
    ctrl.parent_widget_ = parent;
  }

  void SetIsHidingDirect(MahoSpacesOverlayController& ctrl, bool value) {
    ctrl.is_hiding_ = value;
  }

  void SetIsShowingDirect(MahoSpacesOverlayController& ctrl, bool value) {
    ctrl.is_showing_ = value;
  }

  void SetFocusRestoreView(MahoSpacesOverlayController& ctrl,
                           views::View* view) {
    ctrl.focus_restore_tracker_.SetView(view);
  }

  bool GetRestoreFocusOnHide(const MahoSpacesOverlayController& ctrl) const {
    return ctrl.restore_focus_on_hide_;
  }

  // Injects a visible overlay widget without going through Show() so we can
  // test lifecycle helpers independently of MahoSpacesOverlayView.
  void InjectVisibleWidget(MahoSpacesOverlayController& ctrl,
                           views::Widget* parent) {
    ctrl.parent_widget_ = parent;
    parent->AddObserver(&ctrl);
    auto owned = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    owned->Show();
    auto* root_view = owned->GetRootView();
    if (root_view) {
      root_view->SetPaintToLayer();
      root_view->layer()->SetFillsBoundsOpaquely(false);
    }
    owned->AddObserver(&ctrl);
    ctrl.widget_ = std::move(owned);
    ctrl.is_hiding_ = false;
    ctrl.is_showing_ = false;
  }

  // Releases the injected widget cleanly without triggering animation paths.
  void ReleaseInjectedWidget(MahoSpacesOverlayController& ctrl) {
    if (ctrl.widget_) {
      ctrl.widget_->RemoveObserver(&ctrl);
      ctrl.widget_.reset();
    }
    if (ctrl.parent_widget_) {
      ctrl.parent_widget_->RemoveObserver(&ctrl);
      ctrl.parent_widget_ = nullptr;
    }
    ctrl.is_hiding_ = false;
    ctrl.is_showing_ = false;
    ctrl.overlay_view_ = nullptr;
    ctrl.focus_restore_tracker_.SetView(nullptr);
  }

  void CallOnWidgetActivationChanged(MahoSpacesOverlayController& ctrl,
                                     views::Widget* widget,
                                     bool active) {
    ctrl.OnWidgetActivationChanged(widget, active);
  }

  void CallOnWidgetDestroying(MahoSpacesOverlayController& ctrl,
                              views::Widget* widget) {
    ctrl.OnWidgetDestroying(widget);
  }

  bool IsHideWatchdogRunning(const MahoSpacesOverlayController& ctrl) const {
    return ctrl.hide_watchdog_.IsRunning();
  }

  // Invokes the same completion path that both the fade-out animation
  // callbacks and the hide watchdog use.
  void CallFinishHide(MahoSpacesOverlayController& ctrl) {
    ctrl.FinishHide();
  }

  // Enables the real (animated) hide path on a controller built by
  // MakeController(), which otherwise short-circuits animations.
  void EnableAnimatedHide(MahoSpacesOverlayController& ctrl) {
    ctrl.set_disable_animation_for_testing(false);
  }

 private:
  gfx::ScopedAnimationDurationScaleMode non_zero_duration_;
};

namespace {

// ---------------------------------------------------------------------------
// IsVisible — baseline
// ---------------------------------------------------------------------------

TEST_F(MahoSpacesOverlayControllerTest, IsVisibleFalseWhenNoWidget) {
  auto ctrl = MakeController();
  EXPECT_FALSE(ctrl->IsVisible());
}

TEST_F(MahoSpacesOverlayControllerTest,
       IsVisibleTrueWhenWidgetShowingAndNotHiding) {
  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  auto ctrl = MakeController();
  InjectVisibleWidget(*ctrl, parent.get());
  EXPECT_TRUE(ctrl->IsVisible());

  ReleaseInjectedWidget(*ctrl);
}

TEST_F(MahoSpacesOverlayControllerTest,
       IsVisibleFalseWhenIsHidingIsSet) {
  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  auto ctrl = MakeController();
  InjectVisibleWidget(*ctrl, parent.get());
  ASSERT_TRUE(ctrl->IsVisible());

  SetIsHidingDirect(*ctrl, true);
  EXPECT_FALSE(ctrl->IsVisible());

  ReleaseInjectedWidget(*ctrl);
}

// ---------------------------------------------------------------------------
// Toggle — delegates correctly to Show/Dismiss
// ---------------------------------------------------------------------------

// Toggle when visible must set is_hiding_ (Dismiss path) without crashing.
// We cannot fully exercise the animation path in unit tests, so we verify the
// is_hiding_ flag flips.
TEST_F(MahoSpacesOverlayControllerTest,
       ToggleWhileVisibleSetsIsHiding) {
  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  auto ctrl = MakeController();
  InjectVisibleWidget(*ctrl, parent.get());
  ASSERT_TRUE(ctrl->IsVisible());

  // Dismiss(kExplicitCancel) → Hide() → is_hiding_ = true.
  ctrl->Dismiss(MahoSpacesOverlayController::DismissReason::kExplicitCancel);

  EXPECT_TRUE(GetIsHiding(*ctrl));

  ReleaseInjectedWidget(*ctrl);
}

// ---------------------------------------------------------------------------
// Dismiss — idempotency
// ---------------------------------------------------------------------------

TEST_F(MahoSpacesOverlayControllerTest,
       DismissWhileAlreadyHidingIsNoOp) {
  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  auto ctrl = MakeController();
  InjectVisibleWidget(*ctrl, parent.get());

  // First dismiss starts hiding.
  ctrl->Dismiss(MahoSpacesOverlayController::DismissReason::kExplicitCancel);
  ASSERT_TRUE(GetIsHiding(*ctrl));

  // Second dismiss must be a strict no-op (widget stays allocated, no crash).
  ctrl->Dismiss(MahoSpacesOverlayController::DismissReason::kAction);
  EXPECT_TRUE(GetIsHiding(*ctrl));
  EXPECT_NE(GetWidget(*ctrl), nullptr);

  ReleaseInjectedWidget(*ctrl);
}

TEST_F(MahoSpacesOverlayControllerTest,
       DismissWhenNoWidgetIsNoOp) {
  auto ctrl = MakeController();
  ASSERT_FALSE(ctrl->IsVisible());

  // Must not crash.
  ctrl->Dismiss(MahoSpacesOverlayController::DismissReason::kExplicitCancel);

  EXPECT_FALSE(GetIsHiding(*ctrl));
  EXPECT_EQ(GetWidget(*ctrl), nullptr);
}

// ---------------------------------------------------------------------------
// OnWidgetDestroying — parent widget
// ---------------------------------------------------------------------------

TEST_F(MahoSpacesOverlayControllerTest,
       ParentWidgetDestroyedClearsParentPointer) {
  auto ctrl = MakeController();

  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  InjectVisibleWidget(*ctrl, parent.get());
  ASSERT_TRUE(ctrl->IsVisible());
  ASSERT_EQ(GetParentWidget(*ctrl), parent.get());

  CallOnWidgetDestroying(*ctrl, parent.get());

  EXPECT_EQ(GetParentWidget(*ctrl), nullptr);
  EXPECT_EQ(GetFocusRestoreView(*ctrl), nullptr);

  ReleaseInjectedWidget(*ctrl);
}

// ---------------------------------------------------------------------------
// OnWidgetDestroying — overlay widget
// ---------------------------------------------------------------------------

TEST_F(MahoSpacesOverlayControllerTest,
       OverlayWidgetDestroyedExternallyClearsAllState) {
  auto ctrl = MakeController();

  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  InjectVisibleWidget(*ctrl, parent.get());
  ASSERT_TRUE(ctrl->IsVisible());

  views::Widget* overlay_raw = GetWidget(*ctrl);
  ASSERT_NE(overlay_raw, nullptr);

  CallOnWidgetDestroying(*ctrl, overlay_raw);
  overlay_raw->CloseNow();

  EXPECT_FALSE(GetIsHiding(*ctrl));
  EXPECT_FALSE(GetIsShowing(*ctrl));
  EXPECT_EQ(GetParentWidget(*ctrl), nullptr);
  EXPECT_EQ(GetFocusRestoreView(*ctrl), nullptr);

  // widget_ still holds ownership (CLIENT_OWNS_WIDGET requirement); but
  // visibility must read false because widget_ is no longer showing or it
  // was invalidated.  IsVisible() checks widget_->IsVisible() which will be
  // false once the native window has gone through the destroying sequence.
  EXPECT_FALSE(ctrl->IsVisible());

  ReleaseInjectedWidget(*ctrl);
}

// ---------------------------------------------------------------------------
// OnWidgetActivationChanged — deactivation logic
// ---------------------------------------------------------------------------

// Parent-widget deactivation while NOT in the is_showing_ window must trigger
// Dismiss, provided the overlay widget is not itself active.
//
// The observable outcome is that the overlay is gone, not that is_hiding_ is
// momentarily true: with no MahoSpacesOverlayView attached there is no layer
// to animate, so Hide() closes the widget synchronously and clears is_hiding_
// again before this test can observe it. Asserting on is_hiding_ therefore
// only ever passed when Dismiss was NOT reached (as on a window manager that
// keeps the overlay active), which is the opposite of this contract.
//
// Widget activation is owned by the window manager and Deactivate() merely
// posts a request, so the precondition is checked rather than assumed.
TEST_F(MahoSpacesOverlayControllerTest,
       ParentDeactivationWhileOverlayNotActiveTriggersDismiss) {
  auto ctrl = MakeController();

  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  // Use the real hide path so the dismissal runs to completion instead of
  // parking behind the animation short-circuit.
  EnableAnimatedHide(*ctrl);
  InjectVisibleWidget(*ctrl, parent.get());
  ASSERT_TRUE(ctrl->IsVisible());
  ASSERT_FALSE(GetIsShowing(*ctrl));

  parent->Activate();
  GetWidget(*ctrl)->Deactivate();

  if (GetWidget(*ctrl)->IsActive()) {
    GTEST_SKIP() << "Window manager kept the overlay widget active; the "
                    "not-active precondition of this contract does not hold.";
  }

  CallOnWidgetActivationChanged(*ctrl, parent.get(), false);

  EXPECT_EQ(GetWidget(*ctrl), nullptr)
      << "Parent deactivation outside the activation-handoff window must "
         "dismiss the overlay";
  EXPECT_FALSE(ctrl->IsVisible());

  ReleaseInjectedWidget(*ctrl);
}

// Activation change on an *unrelated* widget must be ignored.
TEST_F(MahoSpacesOverlayControllerTest,
       UnrelatedWidgetActivationChangeIsIgnored) {
  auto ctrl = MakeController();

  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();
  std::unique_ptr<views::Widget> unrelated = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  unrelated->Show();

  InjectVisibleWidget(*ctrl, parent.get());
  ASSERT_TRUE(ctrl->IsVisible());

  CallOnWidgetActivationChanged(*ctrl, unrelated.get(), false);

  EXPECT_FALSE(GetIsHiding(*ctrl));

  ReleaseInjectedWidget(*ctrl);
}

// Overlay-widget activation change (not parent) must be ignored by the guard.
TEST_F(MahoSpacesOverlayControllerTest,
       OverlayWidgetActivationChangeIsIgnored) {
  auto ctrl = MakeController();

  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  InjectVisibleWidget(*ctrl, parent.get());
  ASSERT_TRUE(ctrl->IsVisible());

  views::Widget* overlay = GetWidget(*ctrl);
  ASSERT_NE(overlay, nullptr);

  // Activation change on the overlay widget itself — OnWidgetActivationChanged
  // only reacts to `widget == parent_widget_`.
  CallOnWidgetActivationChanged(*ctrl, overlay, false);

  EXPECT_FALSE(GetIsHiding(*ctrl));

  ReleaseInjectedWidget(*ctrl);
}

// ---------------------------------------------------------------------------
// is_showing_ guard — suppress parent-deactivation during activation handoff
// ---------------------------------------------------------------------------

TEST_F(MahoSpacesOverlayControllerTest,
       ParentDeactivationDuringIsShowingWindowIsIgnored) {
  auto ctrl = MakeController();

  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  InjectVisibleWidget(*ctrl, parent.get());

  // Manually set is_showing_ to simulate the window just after widget_->Show()
  // fires but before the posted task clears it.
  SetIsShowingDirect(*ctrl, true);

  CallOnWidgetActivationChanged(*ctrl, parent.get(), false);

  EXPECT_FALSE(GetIsHiding(*ctrl))
      << "Deactivation during is_showing_ window must not trigger Dismiss";

  SetIsShowingDirect(*ctrl, false);
  ReleaseInjectedWidget(*ctrl);
}

// ---------------------------------------------------------------------------
// Focus-restore tracker
// ---------------------------------------------------------------------------

TEST_F(MahoSpacesOverlayControllerTest,
       FocusRestoreTrackerCapturedByShow_ClearsOnDestroy) {
  auto ctrl = MakeController();

  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  // Create a focusable view and record it in the tracker.
  auto* focusable = parent->SetContentsView(std::make_unique<views::View>());
  focusable->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);

  // Manually seed the tracker (mirrors what Show() does before creating the
  // overlay widget).
  SetFocusRestoreView(*ctrl, focusable);
  ASSERT_EQ(GetFocusRestoreView(*ctrl), focusable);

  // Destroying the parent widget must clear the tracker (widget observer path).
  // We simulate this with the direct OnWidgetDestroying helper.
  SetParentWidgetDirect(*ctrl, parent.get());
  CallOnWidgetDestroying(*ctrl, parent.get());

  EXPECT_EQ(GetFocusRestoreView(*ctrl), nullptr);
}

TEST_F(MahoSpacesOverlayControllerTest,
       ViewTrackerClearsWhenSavedViewIsDestroyed) {
  auto ctrl = MakeController();

  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  {
    // Ephemeral view — will be destroyed at the end of this block.
    auto ephemeral = std::make_unique<views::View>();
    SetFocusRestoreView(*ctrl, ephemeral.get());
    ASSERT_EQ(GetFocusRestoreView(*ctrl), ephemeral.get());
  }

  // ViewTracker nullifies itself when the observed view is destroyed.
  EXPECT_EQ(GetFocusRestoreView(*ctrl), nullptr);
}

// ---------------------------------------------------------------------------
// Repeated open/close lifecycle
// ---------------------------------------------------------------------------

TEST_F(MahoSpacesOverlayControllerTest,
       RepeatedInjectReleaseRetainsConsistentState) {
  auto ctrl = MakeController();

  for (int cycle = 0; cycle < 3; ++cycle) {
    SCOPED_TRACE(testing::Message() << "cycle=" << cycle);

    std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    parent->Show();

    InjectVisibleWidget(*ctrl, parent.get());
    EXPECT_TRUE(ctrl->IsVisible());
    EXPECT_FALSE(GetIsHiding(*ctrl));

    SetIsHidingDirect(*ctrl, true);
    EXPECT_FALSE(ctrl->IsVisible());

    ReleaseInjectedWidget(*ctrl);

    EXPECT_FALSE(ctrl->IsVisible());
    EXPECT_FALSE(GetIsHiding(*ctrl));
    EXPECT_EQ(GetWidget(*ctrl), nullptr);
    EXPECT_EQ(GetParentWidget(*ctrl), nullptr);
  }
}

// ---------------------------------------------------------------------------
// DismissReason::kAction vs kExplicitCancel — restore_focus_on_hide_ flag
// ---------------------------------------------------------------------------

TEST_F(MahoSpacesOverlayControllerTest,
       ExplicitCancelSetsRestoreFocusFlag) {
  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  auto ctrl = MakeController();
  InjectVisibleWidget(*ctrl, parent.get());

  ctrl->Dismiss(MahoSpacesOverlayController::DismissReason::kExplicitCancel);

  EXPECT_TRUE(GetRestoreFocusOnHide(*ctrl));

  ReleaseInjectedWidget(*ctrl);
}

TEST_F(MahoSpacesOverlayControllerTest,
       ActionDismissDoesNotSetRestoreFocusFlag) {
  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  auto ctrl = MakeController();
  InjectVisibleWidget(*ctrl, parent.get());

  ctrl->Dismiss(MahoSpacesOverlayController::DismissReason::kAction);

  EXPECT_FALSE(GetRestoreFocusOnHide(*ctrl));

  ReleaseInjectedWidget(*ctrl);
}

TEST_F(MahoSpacesOverlayControllerTest,
       DeactivationDismissDoesNotSetRestoreFocusFlag) {
  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  auto ctrl = MakeController();
  InjectVisibleWidget(*ctrl, parent.get());

  ctrl->Dismiss(MahoSpacesOverlayController::DismissReason::kDeactivation);

  EXPECT_FALSE(GetRestoreFocusOnHide(*ctrl));

  ReleaseInjectedWidget(*ctrl);
}

// ---------------------------------------------------------------------------
// Destructor — safe when widget is still open
// ---------------------------------------------------------------------------

TEST_F(MahoSpacesOverlayControllerTest,
       DestructorWithOpenWidgetDoesNotCrash) {
  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  auto ctrl = MakeController();
  InjectVisibleWidget(*ctrl, parent.get());
  ASSERT_TRUE(ctrl->IsVisible());

  // Manually disconnect the observer relationships before destroying the
  // controller to avoid UAF on the parent widget.
  GetWidget(*ctrl)->RemoveObserver(ctrl.get());
  GetParentWidget(*ctrl)->RemoveObserver(ctrl.get());
  SetParentWidgetDirect(*ctrl, nullptr);

  // Destructor calls CloseWidget(false) — must not crash.
  ctrl.reset();
}

// ---------------------------------------------------------------------------
// Hide — the overlay must never keep eating mouse input
//
// The overlay widget covers the whole web-contents area. Hiding only animates
// a child layer's opacity, so until the widget is destroyed it still
// hit-tests. If teardown is delayed or lost, the window looks normal but every
// click and scroll over the page is swallowed while the sidebar and keyboard
// keep working. These tests pin both halves of the structural fix: input is
// dropped immediately, and destruction cannot depend on an animation callback
// that may never arrive.
// ---------------------------------------------------------------------------

TEST_F(MahoSpacesOverlayControllerTest,
       HideStopsProcessingInputBeforeWidgetIsDestroyed) {
  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  auto ctrl = MakeController();
  InjectVisibleWidget(*ctrl, parent.get());

  views::View* root_view = GetWidget(*ctrl)->GetRootView();
  ASSERT_NE(root_view, nullptr);
  ASSERT_TRUE(root_view->GetCanProcessEventsWithinSubtree());

  ctrl->Dismiss(MahoSpacesOverlayController::DismissReason::kExplicitCancel);

  // The widget still exists here (teardown is asynchronous), so the only thing
  // preventing it from swallowing mouse events is this flag.
  ASSERT_NE(GetWidget(*ctrl), nullptr);
  EXPECT_FALSE(root_view->GetCanProcessEventsWithinSubtree());

  ReleaseInjectedWidget(*ctrl);
}

TEST_F(MahoSpacesOverlayControllerTest,
       HideWithoutAnimatableSurfaceDestroysWidgetSynchronously) {
  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  auto ctrl = MakeController();
  EnableAnimatedHide(*ctrl);
  InjectVisibleWidget(*ctrl, parent.get());

  // No MahoSpacesOverlayView is attached, so there is no layer to animate and
  // no animation callback will ever fire. The widget must still go away rather
  // than linger over the web contents.
  ctrl->Dismiss(MahoSpacesOverlayController::DismissReason::kExplicitCancel);

  EXPECT_EQ(GetWidget(*ctrl), nullptr)
      << "Hide() must not leave a widget alive when no animation can report "
         "completion";
  EXPECT_FALSE(IsHideWatchdogRunning(*ctrl));
  EXPECT_FALSE(GetIsHiding(*ctrl));
}

TEST_F(MahoSpacesOverlayControllerTest,
       FinishHideDestroysWidgetStrandedByHide) {
  std::unique_ptr<views::Widget> parent = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  parent->Show();

  auto ctrl = MakeController();
  InjectVisibleWidget(*ctrl, parent.get());

  // MakeController() disables animations, so Hide() returns with is_hiding_
  // set and the widget still alive - the same stranded state a dropped
  // OnEnded/OnAborted leaves behind in production.
  ctrl->Dismiss(MahoSpacesOverlayController::DismissReason::kExplicitCancel);
  ASSERT_TRUE(GetIsHiding(*ctrl));
  ASSERT_NE(GetWidget(*ctrl), nullptr);

  // FinishHide() is the completion path the watchdog invokes on timeout.
  CallFinishHide(*ctrl);

  EXPECT_EQ(GetWidget(*ctrl), nullptr);
  EXPECT_FALSE(GetIsHiding(*ctrl));
}

}  // namespace
}  // namespace maho
