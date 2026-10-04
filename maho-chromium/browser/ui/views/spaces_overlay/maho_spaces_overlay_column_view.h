// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_COLUMN_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_COLUMN_VIEW_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "maho/browser/ui/views/sidebar/maho_spaces_board_data.h"
#include "maho/browser/ui/views/spaces_overlay/maho_spaces_overlay_board_view.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class Browser;

namespace gfx {
struct VectorIcon;
}  // namespace gfx

namespace ui {
class SimpleMenuModel;
}  // namespace ui

namespace views {
class Label;
class MdTextButton;
class ScrollView;
class ImageButton;
class MenuRunner;
class ImageView;
}  // namespace views

class MahoSpaceContextMenu;

namespace maho {

class MahoSpacesOverlayTabRowView;

class MahoSpacesOverlayColumnView : public views::View {
  METADATA_HEADER(MahoSpacesOverlayColumnView, views::View)

 public:
  using SpaceSelectedCallback = base::RepeatingCallback<void(const std::string&)>;
  using TabSelectedCallback =
      base::RepeatingCallback<void(const std::string& space_id,
                                   const std::string& tab_id)>;

  MahoSpacesOverlayColumnView(
      Browser* browser,
      SpacesBoardRenderMode render_mode,
      const SpaceBoardColumn& column,
      bool is_active,
      SpaceSelectedCallback callback,
      TabSelectedCallback tab_callback = TabSelectedCallback());
  MahoSpacesOverlayColumnView(const MahoSpacesOverlayColumnView&) = delete;
  MahoSpacesOverlayColumnView& operator=(
      const MahoSpacesOverlayColumnView&) = delete;
  ~MahoSpacesOverlayColumnView() override;

  void SetPalette(const MahoSidebarPalette& palette);

  void OnThemeChanged() override;
  gfx::Size GetMinimumSize() const override;

  bool OnMousePressed(const ui::MouseEvent& event) override;
  ui::Cursor GetCursor(const ui::MouseEvent& event) override;

  std::u16string GetTitleTextForTesting() const;
  std::vector<std::u16string> GetSectionTitlesForTesting() const;

 private:
  void UpdateColors();
  void SetFunctionalGlyphImages(views::ImageButton* button,
                                const gfx::VectorIcon& icon);
  void HandleEditPressed();
  void HandleMenuPressed();

  raw_ptr<Browser> browser_;
  SpacesBoardRenderMode render_mode_;
  SpaceBoardColumn column_;
  const bool is_active_;
  SpaceSelectedCallback space_selected_callback_;
  TabSelectedCallback tab_selected_callback_;
  raw_ptr<views::View> badge_container_ = nullptr;
  raw_ptr<views::Label> badge_label_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> subtitle_label_ = nullptr;
  raw_ptr<views::ScrollView> tab_scroll_view_ = nullptr;
  raw_ptr<views::View> tab_list_ = nullptr;
  raw_ptr<views::Label> empty_state_label_ = nullptr;
  std::vector<raw_ptr<MahoSpacesOverlayTabRowView>> tab_row_views_;

  raw_ptr<views::ImageView> clear_arrow_ = nullptr;
  raw_ptr<views::Label> clear_label_ = nullptr;

  raw_ptr<views::Label> tag_chip_ = nullptr;
  raw_ptr<views::ImageButton> edit_button_ = nullptr;
  raw_ptr<views::ImageButton> menu_button_ = nullptr;
  raw_ptr<views::ImageView> drag_handle_ = nullptr;

  MahoSidebarPalette palette_;

  std::unique_ptr<MahoSpaceContextMenu> active_space_context_menu_;
  std::unique_ptr<ui::SimpleMenuModel> active_context_menu_model_;
  std::unique_ptr<views::MenuRunner> context_menu_runner_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_COLUMN_VIEW_H_
