// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/maho_content_gradient_view.h"

#include "maho/browser/ui/views/sidebar/maho_sidebar_grain_overlay_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_themed_background.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/layout/fill_layout.h"

namespace maho {

MahoContentGradientView::MahoContentGradientView() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SetCanProcessEventsWithinSubtree(false);
  SetFocusBehavior(FocusBehavior::NEVER);
  // The sibling MultiContentsView/ContentsWebView is layer-backed (compositor
  // surface for web content). Layer-backed views composite above
  // non-layer-backed siblings regardless of View z-order, so this gradient
  // must also paint to a layer to sit on top of the empty content area's gray
  // web-contents layer. Ordered last among contents_container children by
  // BrowserView, so its layer is topmost.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);

  grain_overlay_ =
      AddChildView(std::make_unique<MahoSidebarGrainOverlayView>());
  SetLayoutManager(std::make_unique<views::FillLayout>());
}

MahoContentGradientView::~MahoContentGradientView() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void MahoContentGradientView::SetGrain(float grain) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (grain_overlay_) {
    grain_overlay_->SetTexture(grain);
  }
}

void MahoContentGradientView::SetPalette(
    const MahoSidebarPalette& palette) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (palette.surface_stops == palette_.surface_stops &&
      palette.grain == palette_.grain) {
    return;
  }
  palette_ = palette;
  SetGrain(palette.grain);
  SchedulePaint();
}

void MahoContentGradientView::Layout(PassKey pass_key) {
  LayoutSuperclass<views::View>(this);
  if (!grain_overlay_) {
    return;
  }
  grain_overlay_->SetBoundsRect(GetLocalBounds());
  grain_overlay_->SetTileOrigin(gfx::Point());
}

void MahoContentGradientView::OnPaint(gfx::Canvas* canvas) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const gfx::Rect bounds = GetContentsBounds();
  if (bounds.IsEmpty()) {
    return;
  }

  const SkVector square_radii[4] = {};
  PaintMahoSidebarThemedBackground(canvas, palette_, bounds,
                                   /*opaque=*/false, square_radii);
}

void MahoContentGradientView::OnThemeChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  views::View::OnThemeChanged();
  SchedulePaint();
}

BEGIN_METADATA(MahoContentGradientView)
END_METADATA

}  // namespace maho
