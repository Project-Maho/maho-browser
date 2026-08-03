// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_GRAIN_OVERLAY_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_GRAIN_OVERLAY_VIEW_H_

#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/view.h"

namespace gfx {
class Canvas;
}  // namespace gfx

namespace maho {

class MahoSidebarGrainOverlayView : public views::View {
  METADATA_HEADER(MahoSidebarGrainOverlayView, views::View)

 public:
  MahoSidebarGrainOverlayView();
  MahoSidebarGrainOverlayView(const MahoSidebarGrainOverlayView&) = delete;
  MahoSidebarGrainOverlayView& operator=(const MahoSidebarGrainOverlayView&) = delete;
  ~MahoSidebarGrainOverlayView() override;

  void SetTexture(float texture);
  void SetTextureWithoutRepaint(float texture);
  void SetTileOrigin(const gfx::Point& tile_origin);
  float texture_for_testing() const { return texture_; }
  const gfx::Point& tile_origin_for_testing() const { return tile_origin_; }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  bool UpdateTexture(float texture);
  float texture_ = 0.0f;
  gfx::Point tile_origin_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_GRAIN_OVERLAY_VIEW_H_
