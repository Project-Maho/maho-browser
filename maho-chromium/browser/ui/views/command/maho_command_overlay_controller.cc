// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "maho/browser/ui/views/command/maho_command_overlay_view.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {

constexpr base::TimeDelta kFadeInDuration = base::Milliseconds(120);
constexpr base::TimeDelta kFadeOutDuration = base::Milliseconds(80);

constexpr int kPopupWidthDp = 640;
constexpr int kPopupTopOffsetDp = 80;

}  // namespace

MahoCommandOverlayController::MahoCommandOverlayController(Browser* browser)
    : browser_(browser) {}

MahoCommandOverlayController::~MahoCommandOverlayController() {
  if (parent_widget_) {
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
  }
  if (widget_) {
    widget_->RemoveObserver(this);
    widget_->Close();
    widget_ = nullptr;
  }
  anchor_ = nullptr;
}

void MahoCommandOverlayController::Toggle(views::View* anchor) {
  if (IsVisible()) {
    Hide();
  } else {
    Show(anchor);
  }
}

void MahoCommandOverlayController::Hide() {
  if (!widget_ || is_hiding_)
    return;

  is_hiding_ = true;

  if (parent_widget_) {
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
  }
  anchor_ = nullptr;

  auto* root_view = widget_->GetRootView();
  if (!root_view || !root_view->layer()) {
    widget_->RemoveObserver(this);
    widget_->Close();
    widget_ = nullptr;
    is_hiding_ = false;
    return;
  }

  views::AnimationBuilder()
      .OnEnded(base::BindOnce(
          [](base::WeakPtr<MahoCommandOverlayController> self) {
            if (!self || !self->widget_)
              return;
            self->widget_->RemoveObserver(self.get());
            self->widget_->Close();
            self->widget_ = nullptr;
            self->is_hiding_ = false;
          },
          weak_factory_.GetWeakPtr()))
      .OnAborted(base::BindOnce(
          [](base::WeakPtr<MahoCommandOverlayController> self) {
            if (!self || !self->widget_)
              return;
            self->widget_->RemoveObserver(self.get());
            self->widget_->Close();
            self->widget_ = nullptr;
            self->is_hiding_ = false;
          },
          weak_factory_.GetWeakPtr()))
      .Once()
      .SetDuration(kFadeOutDuration)
      .SetOpacity(root_view->layer(), 0.0f, gfx::Tween::EASE_IN);
}

bool MahoCommandOverlayController::IsVisible() const {
  return widget_ && widget_->IsVisible() && !is_hiding_;
}

void MahoCommandOverlayController::OnWidgetDestroying(
    views::Widget* widget) {
  if (widget == parent_widget_) {
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
    anchor_ = nullptr;
  }
  if (widget == widget_) {
    widget_->RemoveObserver(this);
    widget_ = nullptr;
  }
}

void MahoCommandOverlayController::OnWidgetBoundsChanged(
    views::Widget* widget,
    const gfx::Rect& new_bounds) {
  if (widget == parent_widget_ && widget_) {
    Reposition();
  }
}

void MahoCommandOverlayController::OnWidgetActivationChanged(
    views::Widget* widget,
    bool active) {
  if (widget == widget_ && !active) {
    Hide();
  }
}

gfx::Rect MahoCommandOverlayController::ComputePopupBounds() const {
  if (!parent_widget_)
    return gfx::Rect();

  gfx::Rect parent_bounds = parent_widget_->GetWindowBoundsInScreen();

  int x = parent_bounds.x() +
           (parent_bounds.width() - kPopupWidthDp) / 2;
  int y = parent_bounds.y() + kPopupTopOffsetDp;
  int height = 1;

  return gfx::Rect(x, y, kPopupWidthDp, height);
}

void MahoCommandOverlayController::Reposition() {
  if (!widget_ || !parent_widget_)
    return;

  gfx::Rect bounds = ComputePopupBounds();
  gfx::Rect current = widget_->GetWindowBoundsInScreen();
  bounds.set_height(current.height());
  widget_->SetBounds(bounds);
}

void MahoCommandOverlayController::Show(views::View* anchor) {
  if (!anchor || !anchor->GetWidget())
    return;

  // If a previous widget is still closing (mid-animation), force-close it.
  if (widget_) {
    widget_->RemoveObserver(this);
    widget_->Close();
    widget_ = nullptr;
  }
  is_hiding_ = false;

  anchor_ = anchor;
  parent_widget_ = anchor->GetWidget();
  parent_widget_->AddObserver(this);

  auto overlay_view = std::make_unique<MahoCommandOverlayView>(
      browser_, base::BindOnce(&MahoCommandOverlayController::Hide,
                               base::Unretained(this)));

  auto* overlay_ptr = overlay_view.get();

  views::Widget::InitParams params(
      views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET,
      views::Widget::InitParams::TYPE_POPUP);
  params.parent = parent_widget_->GetNativeView();
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.bounds = ComputePopupBounds();
  params.accept_events = true;
  params.activatable = views::Widget::InitParams::Activatable::kYes;
  params.shadow_type = views::Widget::InitParams::ShadowType::kDrop;

  auto widget = std::make_unique<views::Widget>();
  widget->Init(std::move(params));
  widget->SetContentsView(std::move(overlay_view));
  widget->AddObserver(this);

  widget_ = widget.get();

  auto* root_view = widget_->GetRootView();
  if (root_view) {
    root_view->SetPaintToLayer();
    root_view->layer()->SetFillsBoundsOpaquely(false);
    root_view->layer()->SetOpacity(0.0f);
  }

  widget->Show();
  widget->Activate();

  gfx::Size preferred = overlay_ptr->GetPreferredSize();
  gfx::Rect bounds = widget_->GetWindowBoundsInScreen();
  bounds.set_height(preferred.height());
  widget_->SetBounds(bounds);

  if (root_view && root_view->layer()) {
    views::AnimationBuilder()
        .Once()
        .SetDuration(kFadeInDuration)
        .SetOpacity(root_view->layer(), 1.0f, gfx::Tween::EASE_OUT);
  }

  overlay_ptr->textfield()->RequestFocus();

  widget.release();
}

}  // namespace maho
