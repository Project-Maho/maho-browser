// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_BROWSER_FRAME_OVERLAY_HOST_H_
#define MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_BROWSER_FRAME_OVERLAY_HOST_H_

#include <memory>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class Browser;

namespace maho {

class MahoSidebarContainerView;
class MahoSpacesOverlayController;
class MahoLibraryOverlayController;
class MahoPeekController;

class MahoBrowserFrameOverlayHost : public views::View {
  METADATA_HEADER(MahoBrowserFrameOverlayHost, views::View)

 public:
  enum class OverlayKind {
    kSpaces,
    kLibrary,
    kCommand,
    kPeek,
  };

  MahoBrowserFrameOverlayHost(Browser* browser,
                              MahoSidebarContainerView* sidebar_container);
  MahoBrowserFrameOverlayHost(const MahoBrowserFrameOverlayHost&) = delete;
  MahoBrowserFrameOverlayHost& operator=(
      const MahoBrowserFrameOverlayHost&) = delete;
  ~MahoBrowserFrameOverlayHost() override;

  void ToggleSpacesOverlay();
  void DismissSpacesOverlay();
  bool IsSpacesOverlayVisible() const;

  void ToggleLibraryOverlay(MahoSidebarLibraryRailView::Category category);
  void DismissLibraryOverlay();
  bool IsLibraryOverlayVisible() const;
  void UpdateLibraryOverlayBounds();

  void DismissAllOverlaysExcept(OverlayKind kind);

  MahoSpacesOverlayController* spaces_overlay_controller() {
    return spaces_overlay_controller_.get();
  }

  MahoLibraryOverlayController* library_overlay_controller() {
    return library_overlay_controller_.get();
  }

  MahoPeekController* peek_controller();

 private:
  void OnSidebarPaletteChanged(const MahoSidebarPalette& palette);

  raw_ptr<Browser> browser_;
  raw_ptr<MahoSidebarContainerView> sidebar_container_;
  std::unique_ptr<MahoSpacesOverlayController> spaces_overlay_controller_;
  std::unique_ptr<MahoLibraryOverlayController> library_overlay_controller_;
  std::unique_ptr<MahoPeekController> peek_controller_;
  MahoSidebarPalette sidebar_palette_;
  bool has_sidebar_palette_ = false;
  base::CallbackListSubscription sidebar_palette_subscription_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_BROWSER_FRAME_OVERLAY_HOST_H_
