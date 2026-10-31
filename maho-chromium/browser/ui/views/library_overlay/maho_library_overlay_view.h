// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_LIBRARY_OVERLAY_MAHO_LIBRARY_OVERLAY_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_LIBRARY_OVERLAY_MAHO_LIBRARY_OVERLAY_VIEW_H_

#include <string>
#include <utility>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class Browser;

namespace maho {

class MahoSidebarArchiveView;
class MahoSidebarDownloadsView;

class MahoLibraryOverlayView : public views::View {
  METADATA_HEADER(MahoLibraryOverlayView, views::View)

 public:
  using Category = MahoSidebarLibraryRailView::Category;
  using DismissCallback = base::OnceClosure;
  using RestoreCallback =
      base::RepeatingCallback<void(const std::string& tab_id,
                                   const std::string& space_id)>;
  using DeleteCallback =
      base::RepeatingCallback<void(const std::string& tab_id)>;

  MahoLibraryOverlayView(Browser* browser,
                         Category category,
                         DismissCallback dismiss_callback,
                         RestoreCallback restore_callback,
                         DeleteCallback delete_callback);
  MahoLibraryOverlayView(const MahoLibraryOverlayView&) = delete;
  MahoLibraryOverlayView& operator=(const MahoLibraryOverlayView&) = delete;
  ~MahoLibraryOverlayView() override;

  Category category() const { return category_; }
  void SetCategory(Category category);
  void SetPalette(const MahoSidebarPalette& palette);
  // Invoked from OnThemeChanged() to pull the docked rail's authoritative
  // snapshot. This overlay lives in its own top-level widget, so Chromium
  // delivers its theme change independently of the rail's; without the pull the
  // widget can repaint from a snapshot the rail has already replaced.
  void set_palette_refresh_callback(base::RepeatingClosure callback) {
    palette_refresh_callback_ = std::move(callback);
  }
  bool palette_opaque_for_testing() const { return palette_.opaque; }

  void DismissOverlay();

  MahoSidebarArchiveView* archive_view_for_testing() { return archive_view_; }
  MahoSidebarDownloadsView* downloads_view_for_testing() {
    return downloads_view_;
  }

  // views::View:
  void RequestFocus() override;
  void OnThemeChanged() override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  void Layout(PassKey) override;

 private:
  void BuildContent();
  void HandleArchiveDelete(const std::string& tab_id);

  raw_ptr<Browser> browser_;
  Category category_;
  DismissCallback dismiss_callback_;
  RestoreCallback restore_callback_;
  DeleteCallback delete_callback_;

  raw_ptr<views::View> card_container_ = nullptr;
  raw_ptr<MahoSidebarArchiveView> archive_view_ = nullptr;
  raw_ptr<MahoSidebarDownloadsView> downloads_view_ = nullptr;
  MahoSidebarPalette palette_;
  base::RepeatingClosure palette_refresh_callback_;

  base::WeakPtrFactory<MahoLibraryOverlayView> weak_ptr_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_LIBRARY_OVERLAY_MAHO_LIBRARY_OVERLAY_VIEW_H_
