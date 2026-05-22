// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_COLUMN_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_COLUMN_VIEW_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "maho/browser/ui/views/sidebar/maho_spaces_board_data.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class Label;
class MdTextButton;
class ScrollView;
}  // namespace views

namespace maho {

class MahoSpacesOverlayColumnView : public views::View {
  METADATA_HEADER(MahoSpacesOverlayColumnView, views::View)

 public:
  using SpaceSelectedCallback = base::RepeatingCallback<void(const std::string&)>;

  MahoSpacesOverlayColumnView(const SpaceBoardColumn& column,
                              bool is_active,
                              SpaceSelectedCallback callback);
  MahoSpacesOverlayColumnView(const MahoSpacesOverlayColumnView&) = delete;
  MahoSpacesOverlayColumnView& operator=(
      const MahoSpacesOverlayColumnView&) = delete;
  ~MahoSpacesOverlayColumnView() override;

  void OnThemeChanged() override;

  bool IsCurrentChipVisibleForTesting() const;
  std::u16string GetTitleTextForTesting() const;
  std::u16string GetActionButtonTextForTesting() const;
  std::vector<std::u16string> GetSectionTitlesForTesting() const;

 private:
  void UpdateColors();
  void HandleOpenPressed();

  SpaceBoardColumn column_;
  const bool is_active_;
  SpaceSelectedCallback space_selected_callback_;
  raw_ptr<views::View> badge_container_ = nullptr;
  raw_ptr<views::Label> badge_label_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> subtitle_label_ = nullptr;
  raw_ptr<views::Label> current_chip_label_ = nullptr;
  raw_ptr<views::MdTextButton> action_button_ = nullptr;
  raw_ptr<views::ScrollView> tab_scroll_view_ = nullptr;
  raw_ptr<views::View> tab_list_ = nullptr;
  raw_ptr<views::Label> empty_state_label_ = nullptr;
  std::vector<raw_ptr<views::Label>> section_labels_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_COLUMN_VIEW_H_
