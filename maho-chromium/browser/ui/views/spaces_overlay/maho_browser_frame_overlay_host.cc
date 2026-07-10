// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/spaces_overlay/maho_browser_frame_overlay_host.h"

#include "chrome/browser/ui/browser.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/ui/views/spaces_overlay/maho_spaces_overlay_controller.h"
#include "ui/base/metadata/metadata_impl_macros.h"

namespace maho {

BEGIN_METADATA(MahoBrowserFrameOverlayHost)
END_METADATA

MahoBrowserFrameOverlayHost::MahoBrowserFrameOverlayHost(
    Browser* browser,
    MahoSidebarContainerView* sidebar_container)
    : browser_(browser), sidebar_container_(sidebar_container) {
  SetPreferredSize(gfx::Size());
}

MahoBrowserFrameOverlayHost::~MahoBrowserFrameOverlayHost() = default;

void MahoBrowserFrameOverlayHost::ToggleSpacesOverlay() {
  if (!spaces_overlay_controller_) {
    spaces_overlay_controller_ =
        std::make_unique<MahoSpacesOverlayController>(browser_);
    spaces_overlay_controller_->set_sidebar_container(sidebar_container_);
  }
  if (sidebar_container_) {
    if (auto* sidebar = static_cast<MahoSidebarView*>(sidebar_container_->sidebar_view())) {
      spaces_overlay_controller_->set_dismiss_callback(
          base::BindRepeating(
              [](base::WeakPtr<MahoSidebarView> view) {
                if (view) {
                  view->library_rail_view()->ClearSelection();
                }
              },
              sidebar->GetWeakPtr()));
    }
  }
  spaces_overlay_controller_->Toggle(GetWidget());
}

void MahoBrowserFrameOverlayHost::DismissSpacesOverlay() {
  if (spaces_overlay_controller_) {
    spaces_overlay_controller_->Dismiss(MahoSpacesOverlayController::DismissReason::kExplicitCancel);
  }
}

bool MahoBrowserFrameOverlayHost::IsSpacesOverlayVisible() const {
  return spaces_overlay_controller_ && spaces_overlay_controller_->IsVisible();
}

}  // namespace maho
