// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_spaces_overlay_controller.h"

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "maho_spaces_overlay_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {

constexpr base::TimeDelta kFadeInDuration = base::Milliseconds(180);
constexpr base::TimeDelta kFadeOutDuration = base::Milliseconds(140);
constexpr int kShowSlideOffsetDp = 12;

// Grace added to the fade-out duration before the hide watchdog force-closes
// the widget. Long enough that the animation callback normally wins the race,
// short enough that a dropped callback cannot strand the overlay.
constexpr base::TimeDelta kHideWatchdogGrace = base::Milliseconds(250);

}  // namespace

MahoSpacesOverlayController::MahoSpacesOverlayController(Browser* browser)
    : browser_(browser) {
  if (auto* bridge = MahoSpaceProfileBridge::GetInstance(); bridge) {
    bridge->AddObserver(this);
  }
}

MahoSpacesOverlayController::~MahoSpacesOverlayController() {
  if (auto* bridge = MahoSpaceProfileBridge::GetInstance(); bridge) {
    bridge->RemoveObserver(this);
  }
  CloseWidget(false);
}

void MahoSpacesOverlayController::Toggle(views::Widget* parent_widget) {
  if (IsVisible()) {
    Dismiss(DismissReason::kExplicitCancel);
    return;
  }
  Show(parent_widget);
}

void MahoSpacesOverlayController::Show(views::Widget* parent_widget) {
  DCHECK(parent_widget);
  if (!parent_widget) {
    return;
  }

  if (widget_) {
    CloseWidget(false);
  }
  if (parent_widget_) {
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
  }

  is_hiding_ = false;
  restore_focus_on_hide_ = false;
  focus_restore_tracker_.SetView(nullptr);
  parent_widget_ = parent_widget;

  if (auto* focus_manager = parent_widget_->GetFocusManager()) {
    focus_restore_tracker_.SetView(focus_manager->GetFocusedView());
  }

  parent_widget_->AddObserver(this);

  auto overlay_view = std::make_unique<MahoSpacesOverlayView>(
      browser_,
      base::BindOnce(
          [](base::WeakPtr<MahoSpacesOverlayController> self) {
            if (self) {
              self->Dismiss(DismissReason::kExplicitCancel);
            }
          },
          weak_factory_.GetWeakPtr()),
      base::BindOnce(
          [](base::WeakPtr<MahoSpacesOverlayController> self) {
            if (self) {
              self->Dismiss(DismissReason::kAction);
            }
          },
          weak_factory_.GetWeakPtr()));
  overlay_view->SetPalette(palette_);
  overlay_view->set_palette_refresh_callback(base::BindRepeating(
      &MahoSpacesOverlayController::RefreshPaletteFromSidebar,
      base::Unretained(this)));

  widget_ = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params.parent = parent_widget_->GetNativeView();
  params.opacity = views::Widget::InitParams::WindowOpacity::kOpaque;
  params.activatable = views::Widget::InitParams::Activatable::kYes;
  params.shadow_type = views::Widget::InitParams::ShadowType::kNone;
  widget_->Init(std::move(params));

  overlay_view_ = widget_->SetContentsView(std::move(overlay_view));
  widget_->AddObserver(this);

  if (auto* root_view = widget_->GetRootView()) {
    root_view->SetPaintToLayer();
    root_view->layer()->SetFillsBoundsOpaquely(true);
  }

  UpdateBounds();
  is_showing_ = true;
  widget_->Show();
  widget_->Activate();

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<MahoSpacesOverlayController> self) {
            if (self) {
              self->is_showing_ = false;
            }
          },
          weak_factory_.GetWeakPtr()));

  if (overlay_view_ && overlay_view_->GetAnimatedView()) {
    if (auto* anim_layer = overlay_view_->GetAnimatedView()->layer()) {
      views::AnimationBuilder()
          .Once()
          .SetDuration(kFadeInDuration)
          .SetOpacity(anim_layer, 1.0f, gfx::Tween::FAST_OUT_SLOW_IN)
          .SetTransform(anim_layer, gfx::Transform(),
                        gfx::Tween::FAST_OUT_SLOW_IN);
    }
  }

  if (overlay_view_) {
    overlay_view_->RequestFocus();
  }
}

void MahoSpacesOverlayController::SetPalette(
    const MahoSidebarPalette& palette) {
  palette_ = palette;
  if (overlay_view_) {
    overlay_view_->SetPalette(palette_);
  }
}

void MahoSpacesOverlayController::RefreshPaletteFromSidebar() {
  if (!sidebar_container_) {
    return;
  }
  auto* sidebar = views::AsViewClass<MahoSidebarView>(
      sidebar_container_->sidebar_view());
  if (!sidebar) {
    return;
  }
  // The rail is the palette authority; re-apply its live snapshot so this
  // widget's own theme-change notification can never leave the overlay painted
  // from a palette the rail has already replaced.
  SetPalette(sidebar->sidebar_palette());
}

void MahoSpacesOverlayController::Dismiss(DismissReason reason) {
  if (is_hiding_ || !widget_) {
    return;
  }
  restore_focus_on_hide_ = reason == DismissReason::kExplicitCancel;
  Hide();
}

bool MahoSpacesOverlayController::IsVisible() const {
  return widget_ && widget_->IsVisible() && !is_hiding_;
}

void MahoSpacesOverlayController::OnWidgetDestroying(views::Widget* widget) {
  if (widget == parent_widget_) {
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
    focus_restore_tracker_.SetView(nullptr);
  }

  if (widget == widget_.get()) {
    widget_->RemoveObserver(this);
    hide_watchdog_.Stop();
    weak_factory_.InvalidateWeakPtrs();
    is_hiding_ = false;
    is_showing_ = false;
    restore_focus_on_hide_ = false;
    overlay_view_ = nullptr;
    focus_restore_tracker_.SetView(nullptr);
    if (parent_widget_) {
      parent_widget_->RemoveObserver(this);
      parent_widget_ = nullptr;
    }
    if (dismiss_callback_) {
      dismiss_callback_.Run();
      dismiss_callback_.Reset();
    }
    // Under CLIENT_OWNS_WIDGET the controller must retain ownership of
    // widget_ so that CloseWidget() / the destructor can call widget_.reset()
    // to cleanly destroy the widget after native teardown completes.
    // Releasing here would orphan the view tree and leak the overlay.
  }
}

void MahoSpacesOverlayController::OnWidgetBoundsChanged(
    views::Widget* widget,
    const gfx::Rect& new_bounds) {
  if (widget == parent_widget_ && widget_) {
    UpdateBounds();
  }
}

void MahoSpacesOverlayController::OnWidgetActivationChanged(
    views::Widget* widget,
    bool active) {
  if (widget == parent_widget_ && !active) {
    // Do not dismiss if the activation shift is going to our own overlay
    // widget. This handles the transient deactivation of the parent browser
    // window that occurs immediately after widget_->Activate() is called
    // during Show(), which would otherwise cause the overlay to close itself.
    if (is_showing_ || (widget_ && widget_->IsActive())) {
      return;
    }
    Dismiss(DismissReason::kDeactivation);
  }
}

void MahoSpacesOverlayController::Hide() {
  if (!widget_ || is_hiding_) {
    return;
  }

  is_hiding_ = true;

  // The overlay widget spans the whole web-contents area and keeps hit-testing
  // for as long as it exists. The fade-out below only animates a child layer's
  // opacity, so an overlay that has visually disappeared still swallows mouse
  // input. Drop input at the start of the hide rather than relying on teardown
  // to arrive: a delayed or dropped close can then never leave an invisible
  // widget eating clicks and scrolls over the page. Keyboard input is
  // unaffected by widget hit-testing, which is why that failure mode presents
  // as "mouse dead, keyboard fine".
  if (views::View* root_view = widget_->GetRootView()) {
    root_view->SetCanProcessEventsWithinSubtree(false);
  }

  if (parent_widget_) {
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
  }

  if (disable_animation_for_testing_) {
    return;
  }

  views::View* animated_view = overlay_view_ ? overlay_view_->GetAnimatedView() : nullptr;
  if (!animated_view || !animated_view->layer()) {
    CloseWidget(restore_focus_on_hide_);
    return;
  }

  // Destruction must not hang off the animation callbacks alone. A preempted
  // animation, or one whose layer leaves the tree mid-flight, delivers neither
  // OnEnded nor OnAborted and would strand the widget forever. The watchdog
  // closes it regardless; whichever path runs first wins, since CloseWidget()
  // stops the timer and FinishHide() is a no-op once widget_ is gone.
  hide_watchdog_.Start(FROM_HERE, kFadeOutDuration + kHideWatchdogGrace,
                       base::BindOnce(&MahoSpacesOverlayController::FinishHide,
                                      base::Unretained(this)));

  views::AnimationBuilder()
      .OnEnded(base::BindOnce(&MahoSpacesOverlayController::FinishHide,
                              weak_factory_.GetWeakPtr()))
      .OnAborted(base::BindOnce(&MahoSpacesOverlayController::FinishHide,
                                weak_factory_.GetWeakPtr()))
      .Once()
      .SetDuration(kFadeOutDuration)
      .SetOpacity(animated_view->layer(), 0.0f, gfx::Tween::EASE_IN)
      .SetTransform(animated_view->layer(),
                    gfx::Transform::MakeTranslation(0, kShowSlideOffsetDp / 2),
                    gfx::Tween::EASE_IN);
}

void MahoSpacesOverlayController::CloseWidget(bool restore_focus,
                                              bool suppress_dismiss_callback) {
  hide_watchdog_.Stop();
  weak_factory_.InvalidateWeakPtrs();
  if (dismiss_callback_ && !suppress_dismiss_callback) {
    dismiss_callback_.Run();
    dismiss_callback_.Reset();
  }

  if (parent_widget_) {
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
  }

  if (!widget_) {
    is_hiding_ = false;
    restore_focus_on_hide_ = false;
    return;
  }

  widget_->RemoveObserver(this);
  overlay_view_ = nullptr;
  is_hiding_ = false;
  is_showing_ = false;
  restore_focus_on_hide_ = false;

  views::View* target = focus_restore_tracker_.view();
  focus_restore_tracker_.SetView(nullptr);

  widget_.reset();

  if (restore_focus && target) {
    views::Widget* host = target->GetWidget();
    if (host && !host->IsClosed()) {
      target->RequestFocus();
    }
  }
}

void MahoSpacesOverlayController::FinishHide() {
  CloseWidget(restore_focus_on_hide_);
}

gfx::Rect MahoSpacesOverlayController::ComputeOverlayBounds() const {
  if (!parent_widget_) return gfx::Rect();
  gfx::Rect bounds = parent_widget_->GetClientAreaBoundsInScreen();
  if (sidebar_container_) {
    bounds.Inset(sidebar_container_->GetSidebarSlotInsets());
  }
  return bounds;
}

void MahoSpacesOverlayController::UpdateBounds() {
  if (widget_) {
    widget_->SetBounds(ComputeOverlayBounds());
  }
}

void MahoSpacesOverlayController::OnSpaceProfileBridgeChanged(
    bool is_structural) {
  OnSpaceProfileBridgeChanged();
}

void MahoSpacesOverlayController::OnSpaceProfileBridgeChanged() {
  if (widget_ && widget_->IsVisible() && !is_hiding_) {
    views::Widget* parent = parent_widget_;
    if (parent) {
      // Internal rebuild: skip dismiss callback so rail selection stays.
      // Move current widget to a temp variable so we can keep it alive
      // while the new widget is initialized and shown.
      std::unique_ptr<views::Widget> old_widget = std::move(widget_);
      if (old_widget) {
        old_widget->RemoveObserver(this);
      }
      overlay_view_ = nullptr;
      is_hiding_ = false;
      is_showing_ = false;
      restore_focus_on_hide_ = false;

      Show(parent);

      if (old_widget) {
        old_widget.reset();
      }
    }
  }
}

}  // namespace maho
