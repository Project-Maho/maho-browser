// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_BROWSER_FRAME_OVERLAY_HOST_H_
#define MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_BROWSER_FRAME_OVERLAY_HOST_H_

#include <memory>
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class Browser;

namespace maho {

class MahoSidebarContainerView;
class MahoSpacesOverlayController;

class MahoBrowserFrameOverlayHost : public views::View {
  METADATA_HEADER(MahoBrowserFrameOverlayHost, views::View)

 public:
  MahoBrowserFrameOverlayHost(Browser* browser,
                              MahoSidebarContainerView* sidebar_container);
  MahoBrowserFrameOverlayHost(const MahoBrowserFrameOverlayHost&) = delete;
  MahoBrowserFrameOverlayHost& operator=(
      const MahoBrowserFrameOverlayHost&) = delete;
  ~MahoBrowserFrameOverlayHost() override;

  void ToggleSpacesOverlay();
  void DismissSpacesOverlay();
  bool IsSpacesOverlayVisible() const;

  MahoSpacesOverlayController* spaces_overlay_controller() {
    return spaces_overlay_controller_.get();
  }

 private:
  raw_ptr<Browser> browser_;
  raw_ptr<MahoSidebarContainerView> sidebar_container_;
  std::unique_ptr<MahoSpacesOverlayController> spaces_overlay_controller_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_BROWSER_FRAME_OVERLAY_HOST_H_
