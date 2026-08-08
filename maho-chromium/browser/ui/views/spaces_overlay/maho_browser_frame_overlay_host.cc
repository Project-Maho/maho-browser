// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/spaces_overlay/maho_browser_frame_overlay_host.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"
#include "maho/browser/ui/views/library_overlay/maho_library_overlay_controller.h"
#include "maho/browser/ui/views/peek/maho_peek_controller.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
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
  if (sidebar_container_) {
    if (auto* sidebar = static_cast<MahoSidebarView*>(
            sidebar_container_->sidebar_view())) {
      OnSidebarPaletteChanged(sidebar->sidebar_palette());
      sidebar_palette_subscription_ =
          sidebar->AddSidebarPaletteChangedCallback(base::BindRepeating(
              &MahoBrowserFrameOverlayHost::OnSidebarPaletteChanged,
              base::Unretained(this)));
    }
  }
}

MahoBrowserFrameOverlayHost::~MahoBrowserFrameOverlayHost() = default;

void MahoBrowserFrameOverlayHost::ToggleSpacesOverlay() {
  DismissAllOverlaysExcept(OverlayKind::kSpaces);
  if (!spaces_overlay_controller_) {
    spaces_overlay_controller_ =
        std::make_unique<MahoSpacesOverlayController>(browser_);
    spaces_overlay_controller_->set_sidebar_container(sidebar_container_);
    if (has_sidebar_palette_) {
      spaces_overlay_controller_->SetPalette(sidebar_palette_);
    }
  }
  if (sidebar_container_) {
    if (auto* sidebar = static_cast<MahoSidebarView*>(sidebar_container_->sidebar_view())) {
      spaces_overlay_controller_->set_dismiss_callback(
          base::BindRepeating(
              [](base::WeakPtr<MahoSidebarView> view) {
                if (view) {
                  view->library_rail_view()->ClearSelection();
                  // Restore the translucent rail once the opaque Spaces overlay
                  // closes (the rail was forced opaque to match it while open).
                  view->ApplySidebarBackground();
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

void MahoBrowserFrameOverlayHost::ToggleLibraryOverlay(
    MahoSidebarLibraryRailView::Category category) {
  DismissAllOverlaysExcept(OverlayKind::kLibrary);
  if (!library_overlay_controller_) {
    library_overlay_controller_ =
        std::make_unique<MahoLibraryOverlayController>(browser_);
    library_overlay_controller_->set_sidebar_container(sidebar_container_);
    if (has_sidebar_palette_) {
      library_overlay_controller_->SetPalette(sidebar_palette_);
    }
    library_overlay_controller_->set_delete_callback(base::BindRepeating(
        [](const std::string& tab_id) {
          DispatchShellEvent("delete_archived_tab", {{"tab_id", tab_id}});
        }));
  }
  if (sidebar_container_) {
    if (auto* sidebar = static_cast<MahoSidebarView*>(
            sidebar_container_->sidebar_view())) {
      library_overlay_controller_->set_dismiss_callback(base::BindRepeating(
          [](base::WeakPtr<MahoSidebarView> view) {
            if (view) {
              view->library_rail_view()->ClearSelection();
            }
          },
          sidebar->GetWeakPtr()));
      library_overlay_controller_->set_restore_callback(base::BindRepeating(
          &MahoSidebarView::RestoreArchivedTabAndActivate,
          sidebar->GetWeakPtr()));
    }
  }
  library_overlay_controller_->Toggle(GetWidget(), category);
}

void MahoBrowserFrameOverlayHost::DismissLibraryOverlay() {
  if (library_overlay_controller_) {
    library_overlay_controller_->Dismiss(
        MahoLibraryOverlayController::DismissReason::kExplicitCancel);
  }
}

bool MahoBrowserFrameOverlayHost::IsLibraryOverlayVisible() const {
  return library_overlay_controller_ &&
         library_overlay_controller_->IsVisible();
}

MahoPeekController* MahoBrowserFrameOverlayHost::peek_controller() {
  if (!peek_controller_ && GetWidget()) {
    peek_controller_ = std::make_unique<MahoPeekController>(
        browser_, GetWidget(),
        base::BindRepeating(
            &MahoBrowserFrameOverlayHost::DismissAllOverlaysExcept,
            base::Unretained(this), OverlayKind::kPeek));
  }
  return peek_controller_.get();
}

void MahoBrowserFrameOverlayHost::OnSidebarPaletteChanged(
    const MahoSidebarPalette& palette) {
  sidebar_palette_ = palette;
  has_sidebar_palette_ = true;
  if (spaces_overlay_controller_) {
    spaces_overlay_controller_->SetPalette(sidebar_palette_);
  }
  if (library_overlay_controller_) {
    library_overlay_controller_->SetPalette(sidebar_palette_);
  }
}

void MahoBrowserFrameOverlayHost::UpdateLibraryOverlayBounds() {
  if (library_overlay_controller_) {
    library_overlay_controller_->UpdateBounds();
  }
}

void MahoBrowserFrameOverlayHost::DismissAllOverlaysExcept(OverlayKind kind) {
  if (kind != OverlayKind::kSpaces) {
    DismissSpacesOverlay();
  }
  if (kind != OverlayKind::kLibrary) {
    DismissLibraryOverlay();
  }
  if (kind != OverlayKind::kPeek && peek_controller_) {
    peek_controller_->Hide();
  }
  if (kind != OverlayKind::kCommand) {
    BrowserView* browser_view =
        BrowserView::GetBrowserViewForBrowser(browser_.get());
    if (browser_view) {
      if (auto* command_controller =
              browser_view->GetMahoCommandOverlayControllerForTesting()) {
        command_controller->Hide();
      }
    }
  }
}

}  // namespace maho
