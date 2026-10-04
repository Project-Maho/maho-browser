// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_BOARD_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_BOARD_VIEW_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "maho/browser/ui/views/sidebar/maho_spaces_board_data.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class Browser;

namespace views {
class Label;
}  // namespace views

namespace maho {

class MahoSpacesOverlayColumnView;

enum class SpacesBoardRenderMode {
  kEmbedded,
  kFullViewport,
};

class MahoSpacesOverlayBoardView : public views::View {
  METADATA_HEADER(MahoSpacesOverlayBoardView, views::View)

 public:
  using SpaceSelectedCallback = base::RepeatingCallback<void(const std::string&)>;
  using TabSelectedCallback =
      base::RepeatingCallback<void(const std::string& space_id,
                                   const std::string& tab_id)>;

  MahoSpacesOverlayBoardView(
      Browser* browser,
      SpacesBoardRenderMode render_mode,
      const SpaceBoardModel& model,
      SpaceSelectedCallback callback,
      TabSelectedCallback tab_callback = TabSelectedCallback());
  MahoSpacesOverlayBoardView(const MahoSpacesOverlayBoardView&) = delete;
  MahoSpacesOverlayBoardView& operator=(
      const MahoSpacesOverlayBoardView&) = delete;
  ~MahoSpacesOverlayBoardView() override;

  void OnThemeChanged() override;

  // Pushes the sidebar palette (the single color authority for sidebar
  // surfaces) to this view and its column children. Construction sites that
  // never push a palette (sidebar-embedded spaces panes) are covered by an
  // on-demand resolve at the first OnThemeChanged().
  void SetPalette(const MahoSidebarPalette& palette);

 private:
  void ApplyPaletteColors();
  void ResolvePaletteFromEnvironment();
  raw_ptr<Browser> browser_;
  SpacesBoardRenderMode render_mode_;
  SpaceBoardModel model_;
  SpaceSelectedCallback space_selected_callback_;
  TabSelectedCallback tab_selected_callback_;
  raw_ptr<views::Label> empty_state_label_ = nullptr;
  std::vector<raw_ptr<MahoSpacesOverlayColumnView>> column_views_;
  MahoSidebarPalette palette_;
  bool has_palette_ = false;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_BOARD_VIEW_H_
