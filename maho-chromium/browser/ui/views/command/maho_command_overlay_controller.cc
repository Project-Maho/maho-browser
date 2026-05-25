// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/metrics/histogram_macros.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
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
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"

namespace maho {

// Intentional product divergences from Zen Browser design (preserved by design):
// 1. Standalone overlay architecture: Maho uses a views::Widget-based popup
//    instead of being a urlbar provider / omnibox replacement.
// 2. Explicit open modes and mode-based disposition: kCurrentTab, kNewTab,
//    and kSearch stay distinct.
// 3. AI mode (ai + Tab) and @prefix site-search.
// 4. Typed-text URL/domain/search fallback.
// 5. Calculator / unit-conversion clipboard copy behavior.
// 6. Pre-show focus restore and 200ms deactivation hide.

namespace {

constexpr base::TimeDelta kFadeOutDuration = base::Milliseconds(180);
constexpr base::TimeDelta kResizeDuration = base::Milliseconds(200);
constexpr base::TimeDelta kFadeInDuration = base::Milliseconds(120);
constexpr base::TimeDelta kSlideInDuration = base::Milliseconds(350);
constexpr base::TimeDelta kDeactivationHideDelay = base::Milliseconds(200);

constexpr int kPopupWidthDp = 744;
constexpr int kPopupInitialHeightDp = 232;
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

  RestoreFocusToPreShowView();

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

void MahoCommandOverlayController::RestoreFocusToPreShowView() {
  views::View* saved = pre_show_focused_view_.view();
  pre_show_focused_view_.SetView(nullptr);
  if (!saved || !parent_widget_)
    return;
  views::FocusManager* fm = parent_widget_->GetFocusManager();
  if (fm && fm->ContainsView(saved))
    fm->SetFocusedView(saved);
}

void MahoCommandOverlayController::OnWidgetDestroying(
    views::Widget* widget) {
  if (widget == parent_widget_) {
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
    anchor_ = nullptr;
    pre_show_focused_view_.SetView(nullptr);
  }
  if (widget == widget_.get()) {
    weak_factory_.InvalidateWeakPtrs();
    deactivation_hide_timer_.Stop();
    is_hiding_ = false;
    overlay_view_ = nullptr;
    anchor_ = nullptr;
    RestoreFocusToPreShowView();
    if (parent_widget_) {
      parent_widget_->RemoveObserver(this);
      parent_widget_ = nullptr;
    }
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
    if (parent_widget_ && parent_widget_->IsActive()) {
      deactivation_hide_timer_.Stop();
    }
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

  open_start_ticks_ = base::TimeTicks::Now();

  if (widget_) {
    widget_->RemoveObserver(this);
    widget_.reset();
  }
  if (parent_widget_) {
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
  }
  is_hiding_ = false;

  anchor_ = anchor;
  parent_widget_ = anchor->GetWidget();
  parent_widget_->AddObserver(this);

  views::FocusManager* parent_focus_manager = parent_widget_->GetFocusManager();
  pre_show_focused_view_.SetView(
      parent_focus_manager ? parent_focus_manager->GetFocusedView() : nullptr);

  auto dismiss_cb = base::BindOnce(
      [](base::WeakPtr<MahoCommandOverlayController> self) {
        if (self)
          self->Hide();
      },
      weak_factory_.GetWeakPtr());
  auto resize_cb = base::BindRepeating(
      [](base::WeakPtr<MahoCommandOverlayController> self, int height) {
        if (self)
          self->AnimateBoundsToHeight(height);
      },
      weak_factory_.GetWeakPtr());

  std::unique_ptr<MahoCommandOverlayView> overlay_view;
  if (browser_) {
    overlay_view = std::make_unique<MahoCommandOverlayView>(
        browser_, mode, initial_text, select_initial_text,
        std::move(dismiss_cb), std::move(resize_cb));
  } else {
    overlay_view = std::make_unique<MahoCommandOverlayView>(
        MahoCommandOverlayView::ForTestingTag{}, mode, initial_text,
        select_initial_text, std::move(dismiss_cb), std::move(resize_cb));
  }

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
  {
    const base::TimeDelta open_latency =
        base::TimeTicks::Now() - open_start_ticks_;
    TRACE_EVENT_INSTANT("ui", "MahoCommandPalette.OpenToFirstPaint",
                        "latency_ms", open_latency.InMilliseconds());
    UMA_HISTOGRAM_TIMES("Maho.CommandPalette.OpenLatency", open_latency);
  }

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

  // RequestFocus() is posted rather than called synchronously so that the
  // platform's native window activation (triggered by Activate() above) has
  // a chance to be processed before the view-system focus is set.  Without
  // this, the FocusManager may set focused_view_ before the native window
  // owns OS focus, which causes the first key event dispatched via
  // SendKeyPressSync to be routed to the wrong window.
  overlay_ptr->RequestFocus();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<MahoCommandOverlayController> self) {
            if (!self || !self->overlay_view_ || !self->widget_ ||
                !self->widget_->IsVisible()) {
              return;
            }
            self->overlay_view_->RequestFocus();
          },
          weak_factory_.GetWeakPtr()));
}


}  // namespace maho
