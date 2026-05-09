// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "maho/browser/ui/views/command/maho_command_overlay_view.h"
#include "ui/compositor/layer.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {

constexpr base::TimeDelta kFadeOutDuration = base::Milliseconds(180);
constexpr base::TimeDelta kResizeDuration = base::Milliseconds(200);
constexpr base::TimeDelta kFadeInDuration = base::Milliseconds(120);
constexpr base::TimeDelta kSlideInDuration = base::Milliseconds(350);
constexpr base::TimeDelta kDeactivationHideDelay = base::Milliseconds(200);
constexpr base::TimeDelta kFocusRetentionInterval = base::Milliseconds(120);
constexpr int kFocusRetentionPassCount = 30;

constexpr int kPopupWidthDp = 720;
constexpr int kPopupInitialHeightDp = 220;
constexpr int kPopupMinTopMarginDp = 62;
constexpr int kPopupMinBottomMarginDp = 32;
constexpr int kSlideOffsetDp = 10;

gfx::Rect GetWorkAreaForParentWidget(views::Widget* parent_widget,
                                     const gfx::Rect& parent_bounds) {
  display::Screen* screen = display::Screen::Get();
  if (!screen) {
    return parent_bounds;
  }

  const display::Display display = screen->GetDisplayMatching(parent_bounds);
  const gfx::Rect work_area = display.work_area();
  return work_area.IsEmpty() ? parent_bounds : work_area;
}

int ClampPopupY(int target_y, int height, const gfx::Rect& work_area) {
  const int min_y = work_area.y() + kPopupMinTopMarginDp;
  const int max_y = work_area.bottom() - kPopupMinBottomMarginDp - height;
  if (max_y >= min_y) {
    return std::clamp(target_y, min_y, max_y);
  }

  return std::clamp(target_y, work_area.y(), work_area.bottom() - height);
}

gfx::Rect ComputeCenteredPopupBounds(const gfx::Rect& parent_bounds,
                                     const gfx::Rect& work_area,
                                     int height) {
  const int x = parent_bounds.x() +
                (parent_bounds.width() - kPopupWidthDp) / 2;
  const int target_y = parent_bounds.y() +
                       (parent_bounds.height() - height) / 2;
  return gfx::Rect(x, ClampPopupY(target_y, height, work_area), kPopupWidthDp,
                   height);
}

}  // namespace

MahoCommandOverlayController::MahoCommandOverlayController(Browser* browser)
    : browser_(browser) {
  resize_animation_.SetSlideDuration(kResizeDuration);
  resize_animation_.SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN);
}

MahoCommandOverlayController::~MahoCommandOverlayController() {
  if (parent_widget_) {
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
  }
  if (widget_) {
    widget_->RemoveObserver(this);
    widget_.reset();
  }
  anchor_ = nullptr;
}

void MahoCommandOverlayController::Toggle(views::View* anchor,
                                         CommandOverlayMode mode) {
  if (IsVisible()) {
    Hide();
  } else {
    Show(anchor, mode);
  }
}

void MahoCommandOverlayController::Hide() {
  if (!widget_ || is_hiding_)
    return;

  deactivation_hide_timer_.Stop();
  is_hiding_ = true;

  if (parent_widget_) {
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
  }
  anchor_ = nullptr;

  auto* root_view = widget_->GetRootView();
  if (!root_view || !root_view->layer()) {
    widget_->RemoveObserver(this);
    widget_.reset();
    is_hiding_ = false;
    return;
  }

  views::AnimationBuilder()
      .OnEnded(base::BindOnce(
          &MahoCommandOverlayController::FinishHideAnimation,
          weak_factory_.GetWeakPtr()))
      .OnAborted(base::BindOnce(
          &MahoCommandOverlayController::FinishHideAnimation,
          weak_factory_.GetWeakPtr()))
      .Once()
      .SetDuration(kFadeOutDuration)
      .SetOpacity(root_view->layer(), 0.0f, gfx::Tween::EASE_IN)
      .SetTransform(root_view->layer(),
                    gfx::Transform::MakeTranslation(0, -6),
                    gfx::Tween::EASE_IN);
}

bool MahoCommandOverlayController::IsVisible() const {
  return widget_ && widget_->IsVisible() && !is_hiding_;
}

void MahoCommandOverlayController::FinishHideAnimation() {
  if (!widget_)
    return;

  widget_->RemoveObserver(this);
  widget_->Hide();
  std::unique_ptr<views::Widget> widget = std::move(widget_);
  is_hiding_ = false;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce([](std::unique_ptr<views::Widget>) {}, std::move(widget)));
}

void MahoCommandOverlayController::ScheduleFocusRetentionPasses() {
  if (!widget_ || !overlay_view_ || is_hiding_ ||
      remaining_focus_retention_passes_ <= 0) {
    return;
  }

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<MahoCommandOverlayController> self) {
            if (!self || !self->widget_ || !self->overlay_view_ ||
                self->is_hiding_) {
              return;
            }

            --self->remaining_focus_retention_passes_;
            self->widget_->Activate();
            self->overlay_view_->RequestFocus();
            self->ScheduleFocusRetentionPasses();
          },
          weak_factory_.GetWeakPtr()),
      kFocusRetentionInterval);
}

void MahoCommandOverlayController::OnWidgetDestroying(
    views::Widget* widget) {
  if (widget == parent_widget_) {
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
    anchor_ = nullptr;
  }
  if (widget == widget_.get()) {
    weak_factory_.InvalidateWeakPtrs();
    deactivation_hide_timer_.Stop();
    is_hiding_ = false;
    overlay_view_ = nullptr;
    anchor_ = nullptr;
    // The widget is already in its destruction path, so the controller must
    // relinquish ownership without attempting to delete it again here.
    widget_.release();
  }
}

void MahoCommandOverlayController::OnWidgetBoundsChanged(
    views::Widget* widget,
    const gfx::Rect& new_bounds) {
  if (widget == parent_widget_ && widget_.get()) {
    Reposition();
  }
}

void MahoCommandOverlayController::OnWidgetActivationChanged(
    views::Widget* widget,
    bool active) {
  if (widget != widget_.get()) {
    return;
  }

  if (active) {
    deactivation_hide_timer_.Stop();
    return;
  }

  deactivation_hide_timer_.Start(
      FROM_HERE, kDeactivationHideDelay,
      base::BindOnce(
          [](base::WeakPtr<MahoCommandOverlayController> self) {
            if (!self || !self->widget_ || self->is_hiding_) {
              return;
            }
            if (self->widget_->IsActive()) {
              return;
            }
            self->Hide();
          },
          weak_factory_.GetWeakPtr()));
}

void MahoCommandOverlayController::AnimationProgressed(
    const gfx::Animation* animation) {
  if (!widget_ || animation != &resize_animation_)
    return;

  widget_->SetBounds(gfx::Tween::RectValueBetween(
      resize_animation_.GetCurrentValue(), animation_start_bounds_,
      animation_target_bounds_));
}

void MahoCommandOverlayController::AnimationEnded(
    const gfx::Animation* animation) {
  if (!widget_ || animation != &resize_animation_)
    return;

  widget_->SetBounds(animation_target_bounds_);
}

gfx::Rect MahoCommandOverlayController::ComputePopupBounds() const {
  if (!parent_widget_)
    return gfx::Rect();

  const gfx::Rect parent_bounds = parent_widget_->GetWindowBoundsInScreen();
  const gfx::Rect work_area =
      GetWorkAreaForParentWidget(parent_widget_, parent_bounds);
  return ComputeCenteredPopupBounds(parent_bounds, work_area,
                                    kPopupInitialHeightDp);
}

gfx::Rect MahoCommandOverlayController::ComputeBoundsForHeight(int height) const {
  if (!parent_widget_)
    return gfx::Rect();

  const gfx::Rect parent_bounds = parent_widget_->GetWindowBoundsInScreen();
  const gfx::Rect work_area =
      GetWorkAreaForParentWidget(parent_widget_, parent_bounds);
  const int x = parent_bounds.x() +
                (parent_bounds.width() - kPopupWidthDp) / 2;
  const int target_y = widget_
                           ? widget_->GetWindowBoundsInScreen().y()
                           : parent_bounds.y() +
                                 (parent_bounds.height() - height) / 2;
  return gfx::Rect(x, ClampPopupY(target_y, height, work_area), kPopupWidthDp,
                   height);
}

void MahoCommandOverlayController::Reposition() {
  if (!widget_ || !parent_widget_)
    return;

  const gfx::Rect parent_bounds = parent_widget_->GetWindowBoundsInScreen();
  const gfx::Rect work_area =
      GetWorkAreaForParentWidget(parent_widget_, parent_bounds);
  gfx::Rect bounds = ComputeCenteredPopupBounds(
      parent_bounds, work_area, widget_->GetWindowBoundsInScreen().height());
  widget_->SetBounds(bounds);
}

void MahoCommandOverlayController::AnimateBoundsToHeight(int height) {
  if (!widget_)
    return;

  gfx::Rect target_bounds = ComputeBoundsForHeight(height);
  gfx::Rect current_bounds = widget_->GetWindowBoundsInScreen();
  if (current_bounds == target_bounds)
    return;

  animation_start_bounds_ = current_bounds;
  animation_target_bounds_ = target_bounds;
  resize_animation_.Reset(0.0);
  resize_animation_.Show();
}

void MahoCommandOverlayController::Show(views::View* anchor,
                                        CommandOverlayMode mode,
                                        const std::string& initial_text,
                                        bool select_initial_text) {
  DCHECK(anchor);
  DCHECK(anchor->GetWidget());
  if (!anchor || !anchor->GetWidget())
    return;

  if (widget_) {
    widget_->RemoveObserver(this);
    widget_.reset();
  }
  if (parent_widget_) {
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
  }
  is_hiding_ = false;
  remaining_focus_retention_passes_ = 0;

  anchor_ = anchor;
  parent_widget_ = anchor->GetWidget();
  parent_widget_->AddObserver(this);

  auto overlay_view = std::make_unique<MahoCommandOverlayView>(
      browser_, mode, initial_text, select_initial_text,
      base::BindOnce(
          [](base::WeakPtr<MahoCommandOverlayController> self) {
            if (self)
              self->Hide();
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(
          [](base::WeakPtr<MahoCommandOverlayController> self, int height) {
            if (self)
              self->AnimateBoundsToHeight(height);
          },
          weak_factory_.GetWeakPtr()));

  auto* overlay_ptr = overlay_view.get();

  views::Widget::InitParams params(
      views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params.parent = parent_widget_->GetNativeView();
  params.context = parent_widget_->GetNativeWindow();
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.bounds = ComputePopupBounds();
  params.accept_events = true;
  params.activatable = views::Widget::InitParams::Activatable::kYes;
  params.shadow_type = views::Widget::InitParams::ShadowType::kDrop;

  auto widget = std::make_unique<views::Widget>();
  auto* widget_ptr = widget.get();
  widget_ptr->Init(std::move(params));
  widget_ptr->SetContentsView(std::move(overlay_view));
  widget_ptr->AddObserver(this);

  auto* root_view = widget_ptr->GetRootView();
  if (root_view) {
    root_view->SetPaintToLayer();
    root_view->layer()->SetFillsBoundsOpaquely(false);
  }

  widget_ptr->Show();
  widget_ptr->Activate();

  if (root_view && root_view->layer()) {
    root_view->layer()->SetOpacity(0.0f);
    root_view->layer()->SetTransform(
        gfx::Transform::MakeTranslation(0, -kSlideOffsetDp));
  }

  gfx::Size preferred = overlay_ptr->GetPreferredSize();
  gfx::Rect bounds = ComputeBoundsForHeight(preferred.height());
  widget_ptr->SetBounds(bounds);
  animation_start_bounds_ = bounds;
  animation_target_bounds_ = bounds;

  widget_ = std::move(widget);
  overlay_view_ = overlay_ptr;
  remaining_focus_retention_passes_ = kFocusRetentionPassCount;
  ScheduleFocusRetentionPasses();

  if (root_view && root_view->layer()) {
    views::AnimationBuilder()
        .Once()
        .SetDuration(kSlideInDuration)
        .SetTransform(root_view->layer(), gfx::Transform(),
                      gfx::Tween::ACCEL_20_DECEL_100)
        .At(base::TimeDelta())
        .SetDuration(kFadeInDuration)
        .SetOpacity(root_view->layer(), 1.0f, gfx::Tween::EASE_OUT);
  }

  overlay_ptr->RequestFocus();
}


}  // namespace maho
