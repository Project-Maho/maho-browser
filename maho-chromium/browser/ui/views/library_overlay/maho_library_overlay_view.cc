// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/library_overlay/maho_library_overlay_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_archive_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_downloads_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_themed_background.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/background.h"
#include "ui/views/layout/fill_layout.h"

namespace maho {

BEGIN_METADATA(MahoLibraryOverlayView)
END_METADATA

MahoLibraryOverlayView::MahoLibraryOverlayView(Browser* browser,
                                               Category category,
                                               DismissCallback dismiss_callback,
                                               RestoreCallback restore_callback,
                                               DeleteCallback delete_callback)
    : browser_(browser),
      category_(category),
      dismiss_callback_(std::move(dismiss_callback)),
      restore_callback_(std::move(restore_callback)),
      delete_callback_(std::move(delete_callback)) {
  AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));

  card_container_ = AddChildView(std::make_unique<views::View>());
  card_container_->SetLayoutManager(std::make_unique<views::FillLayout>());

  BuildContent();
}

MahoLibraryOverlayView::~MahoLibraryOverlayView() = default;

void MahoLibraryOverlayView::SetCategory(Category category) {
  if (category_ == category) {
    return;
  }
  category_ = category;
  BuildContent();
}

void MahoLibraryOverlayView::SetPalette(
    const MahoSidebarPalette& palette) {
  palette_ = palette;
  if (archive_view_) {
    archive_view_->SetSidebarPalette(palette_);
  }
  if (downloads_view_) {
    downloads_view_->SetSidebarPalette(palette_);
  }
  SchedulePaint();
}

void MahoLibraryOverlayView::BuildContent() {
  archive_view_ = nullptr;
  downloads_view_ = nullptr;
  card_container_->RemoveAllChildViews();

  switch (category_) {
    case Category::kArchivedTabs: {
      archive_view_ = card_container_->AddChildView(
          std::make_unique<MahoSidebarArchiveView>(browser_));
      archive_view_->SetRestoreCallback(restore_callback_);
      archive_view_->SetDeleteCallback(base::BindRepeating(
          &MahoLibraryOverlayView::HandleArchiveDelete,
           weak_ptr_factory_.GetWeakPtr()));
      archive_view_->SetSidebarPalette(palette_);
      archive_view_->ResetState();
      archive_view_->ReloadArchivedTabs();
      archive_view_->FocusSearchField();
      break;
    }
    case Category::kDownloads: {
      downloads_view_ = card_container_->AddChildView(
          std::make_unique<MahoSidebarDownloadsView>(browser_));
      downloads_view_->SetSidebarPalette(palette_);
      downloads_view_->ReloadDownloads();
      break;
    }
    case Category::kMedia:
    case Category::kSpaces:
      break;
  }
}

void MahoLibraryOverlayView::HandleArchiveDelete(const std::string& tab_id) {
  if (delete_callback_) {
    delete_callback_.Run(tab_id);
  }
  if (archive_view_) {
    archive_view_->ReloadArchivedTabs();
  }
}

void MahoLibraryOverlayView::DismissOverlay() {
  if (dismiss_callback_) {
    std::move(dismiss_callback_).Run();
  }
}

void MahoLibraryOverlayView::RequestFocus() {
  if (archive_view_) {
    archive_view_->FocusSearchField();
    return;
  }
  if (downloads_view_) {
    downloads_view_->RequestFocus();
    return;
  }
  views::View::RequestFocus();
}

void MahoLibraryOverlayView::OnThemeChanged() {
  views::View::OnThemeChanged();
  // This overlay is its own top-level widget, so Chromium delivers its theme
  // change independently of the docked rail's. Pull the rail's authoritative
  // snapshot before repainting instead of relying on the frame overlay host's
  // push landing first, so an OS light/dark flip cannot paint this widget once
  // from the palette the rail has already replaced.
  if (palette_refresh_callback_) {
    palette_refresh_callback_.Run();
  }
  // The themed gradient is painted in OnPaintBackground(); repaint so it picks
  // up the new space-theme colors.
  SchedulePaint();
}

void MahoLibraryOverlayView::OnPaintBackground(gfx::Canvas* canvas) {
  // Paint the same space-theme-tinted gradient as the docked sidebar so the
  // fixed-width overlay content column matches the rail across themes (square
  // corners; the overlay abuts the rail's right edge). The snapshot carries the
  // rail's own paint mode and the shared gradient span, so the two surfaces can
  // never disagree about which stops to use or how densely to lay them out.
  const SkVector radii[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
  PaintMahoSidebarThemedBackground(
      canvas, palette_, GetLocalBounds(), palette_.opaque, radii,
      sidebar_layout::kThemedBackgroundGradientSpanDp);
}

bool MahoLibraryOverlayView::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  if (accelerator.key_code() == ui::VKEY_ESCAPE) {
    DismissOverlay();
    return true;
  }
  return views::View::AcceleratorPressed(accelerator);
}

void MahoLibraryOverlayView::Layout(PassKey pass_key) {
  LayoutSuperclass<views::View>(this);
  if (!card_container_) {
    return;
  }
  const gfx::Rect local = GetLocalBounds();
  const int card_width =
      std::min(local.width(), sidebar_layout::kLibraryOverlayContentWidthDp);
  card_container_->SetBoundsRect(
      gfx::Rect(local.x(), local.y(), card_width, local.height()));
}

}  // namespace maho
