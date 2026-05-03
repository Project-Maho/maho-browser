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

constexpr base::TimeDelta kFadeInDuration = base::Milliseconds(150);
constexpr base::TimeDelta kFadeOutDuration = base::Milliseconds(100);

}  // namespace

MahoCommandOverlayController::MahoCommandOverlayController(Browser* browser)
    : browser_(browser) {}

MahoCommandOverlayController::~MahoCommandOverlayController() {
  if (widget_) {
    widget_->RemoveObserver(this);
    widget_->Close();
    widget_ = nullptr;
  }
}

void MahoCommandOverlayController::Toggle(views::View* anchor) {
  if (IsVisible()) {
    Hide();
  } else {
    Show(anchor);
  }
}

void MahoCommandOverlayController::Hide() {
  if (!widget_)
    return;

  auto* root_view = widget_->GetRootView();
  if (!root_view || !root_view->layer()) {
    widget_->RemoveObserver(this);
    widget_->Close();
    widget_ = nullptr;
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
          },
          weak_factory_.GetWeakPtr()))
      .OnAborted(base::BindOnce(
          [](base::WeakPtr<MahoCommandOverlayController> self) {
            if (!self || !self->widget_)
              return;
            self->widget_->RemoveObserver(self.get());
            self->widget_->Close();
            self->widget_ = nullptr;
          },
          weak_factory_.GetWeakPtr()))
      .Once()
      .SetDuration(kFadeOutDuration)
      .SetOpacity(root_view->layer(), 0.0f, gfx::Tween::EASE_IN);
}

bool MahoCommandOverlayController::IsVisible() const {
  return widget_ && widget_->IsVisible();
}

void MahoCommandOverlayController::OnWidgetDestroying(
    views::Widget* widget) {
  if (widget_ == widget) {
    widget_->RemoveObserver(this);
    widget_ = nullptr;
  }
}

void MahoCommandOverlayController::Show(views::View* anchor) {
  if (!anchor || !anchor->GetWidget())
    return;

  auto overlay_view = std::make_unique<MahoCommandOverlayView>(
      browser_, base::BindOnce(&MahoCommandOverlayController::Hide,
                               base::Unretained(this)));

  auto* overlay_ptr = overlay_view.get();
  gfx::Size preferred = overlay_view->GetPreferredSize();

  views::Widget::InitParams params(
      views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET,
      views::Widget::InitParams::TYPE_POPUP);
  params.parent = anchor->GetWidget()->GetNativeView();
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;

  gfx::Rect anchor_bounds = anchor->GetWidget()->GetWindowBoundsInScreen();
  int x = anchor_bounds.x() +
           (anchor_bounds.width() - preferred.width()) / 2;
  int y = anchor_bounds.y() + anchor_bounds.height() / 5;
  params.bounds = gfx::Rect(x, y, preferred.width(), preferred.height());

  auto widget = std::make_unique<views::Widget>();
  widget->Init(std::move(params));
  widget->SetContentsView(std::move(overlay_view));
  widget->AddObserver(this);

  widget_ = widget.get();
  widget->Show();

  auto* root_view = widget_->GetRootView();
  if (root_view) {
    root_view->SetPaintToLayer();
    root_view->layer()->SetFillsBoundsOpaquely(false);
    root_view->layer()->SetOpacity(0.0f);

    views::AnimationBuilder()
        .Once()
        .SetDuration(kFadeInDuration)
        .SetOpacity(root_view->layer(), 1.0f, gfx::Tween::EASE_OUT);
  }

  overlay_ptr->textfield()->RequestFocus();

  widget.release();
}

}  // namespace maho
