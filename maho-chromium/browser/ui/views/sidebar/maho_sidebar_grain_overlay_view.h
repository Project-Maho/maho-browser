// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_GRAIN_OVERLAY_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_GRAIN_OVERLAY_VIEW_H_

#include "ui/base/metadata/metadata_header_macros.h"
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

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  float texture_ = 0.0f;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_GRAIN_OVERLAY_VIEW_H_
