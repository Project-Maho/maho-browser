// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/library_overlay/maho_library_overlay_controller.h"

#include <algorithm>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/compositor/layer.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace maho {

MahoLibraryOverlayController::MahoLibraryOverlayController(Browser* browser)
    : browser_(browser) {}

MahoLibraryOverlayController::~MahoLibraryOverlayController() {
  CloseWidget(false, /*suppress_dismiss_callback=*/true);
}

void MahoLibraryOverlayController::Toggle(views::Widget* parent_widget,
                                          Category category) {
  if (IsVisible()) {
    if (category_ == category) {
      Dismiss(DismissReason::kExplicitCancel);
      return;
    }
    SetCategory(category);
    return;
  }
  Show(parent_widget, category);
}

void MahoLibraryOverlayController::Show(views::Widget* parent_widget,
                                        Category category) {
  DCHECK(parent_widget);
  if (!parent_widget) {
    return;
  }

  if (widget_) {
    CloseWidget(false, /*suppress_dismiss_callback=*/true);
  }
  if (parent_widget_) {
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
  }

  category_ = category;
  is_hiding_ = false;
  restore_focus_on_hide_ = false;
  focus_restore_tracker_.SetView(nullptr);
  parent_widget_ = parent_widget;

  if (auto* focus_manager = parent_widget_->GetFocusManager()) {
    focus_restore_tracker_.SetView(focus_manager->GetFocusedView());
  }

  parent_widget_->AddObserver(this);

  auto overlay_view = std::make_unique<MahoLibraryOverlayView>(
      browser_, category_,
      base::BindOnce(
          [](base::WeakPtr<MahoLibraryOverlayController> self) {
            if (self) {
              self->Dismiss(DismissReason::kExplicitCancel);
            }
          },
          weak_factory_.GetWeakPtr()),
      restore_callback_, delete_callback_);
  overlay_view->SetPalette(palette_);
  overlay_view->set_palette_refresh_callback(base::BindRepeating(
      &MahoLibraryOverlayController::RefreshPaletteFromSidebar,
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
          [](base::WeakPtr<MahoLibraryOverlayController> self) {
            if (self) {
              self->is_showing_ = false;
            }
          },
          weak_factory_.GetWeakPtr()));

  if (overlay_view_) {
    overlay_view_->RequestFocus();
  }
}

void MahoLibraryOverlayController::SetCategory(Category category) {
  category_ = category;
  if (overlay_view_) {
    overlay_view_->SetCategory(category);
  }
}

void MahoLibraryOverlayController::SetPalette(
    const MahoSidebarPalette& palette) {
  palette_ = palette;
  if (overlay_view_) {
    overlay_view_->SetPalette(palette_);
  }
}

void MahoLibraryOverlayController::RefreshPaletteFromSidebar() {
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

void MahoLibraryOverlayController::Dismiss(DismissReason reason) {
  if (is_hiding_ || !widget_) {
    return;
  }
  restore_focus_on_hide_ = reason == DismissReason::kExplicitCancel;
  Hide();
}

bool MahoLibraryOverlayController::IsVisible() const {
  return widget_ && widget_->IsVisible() && !is_hiding_;
}

int MahoLibraryOverlayController::GetOverlayWidthForTesting() const {
  return widget_ ? widget_->GetWindowBoundsInScreen().width() : 0;
}

void MahoLibraryOverlayController::OnWidgetDestroying(views::Widget* widget) {
  if (widget == parent_widget_) {
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
    focus_restore_tracker_.SetView(nullptr);
  }

  if (widget == widget_.get()) {
    widget_->RemoveObserver(this);
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
    }
    // Under CLIENT_OWNS_WIDGET the controller must retain ownership of widget_
    // so that CloseWidget()/the destructor can call widget_.reset() to cleanly
    // destroy the widget after native teardown completes.
  }
}

void MahoLibraryOverlayController::OnWidgetBoundsChanged(
    views::Widget* widget,
    const gfx::Rect& new_bounds) {
  if (widget == parent_widget_ && widget_) {
    UpdateBounds();
  }
}

void MahoLibraryOverlayController::Hide() {
  if (!widget_ || is_hiding_) {
    return;
  }
  is_hiding_ = true;
  CloseWidget(restore_focus_on_hide_);
}

void MahoLibraryOverlayController::CloseWidget(bool restore_focus,
                                               bool suppress_dismiss_callback) {
  weak_factory_.InvalidateWeakPtrs();

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

  if (dismiss_callback_ && !suppress_dismiss_callback) {
    dismiss_callback_.Run();
  }

  if (restore_focus && target) {
    views::Widget* host = target->GetWidget();
    if (host && !host->IsClosed()) {
      target->RequestFocus();
    }
  }
}

gfx::Rect MahoLibraryOverlayController::ComputeOverlayBounds() const {
  if (!parent_widget_) {
    return gfx::Rect();
  }
  gfx::Rect bounds = parent_widget_->GetClientAreaBoundsInScreen();
  if (sidebar_container_) {
    bounds.Inset(sidebar_container_->GetSidebarSlotInsets());
  }
  const int width =
      std::min(bounds.width(), sidebar_layout::kLibraryOverlayContentWidthDp);
  bounds.set_width(std::max(width, 0));
  return bounds;
}

void MahoLibraryOverlayController::UpdateBounds() {
  if (widget_) {
    widget_->SetBounds(ComputeOverlayBounds());
  }
}

}  // namespace maho
