// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_TAB_ROW_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_TAB_ROW_VIEW_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "maho/browser/ui/views/sidebar/maho_spaces_board_data.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class Label;
}  // namespace views

namespace maho {

class MahoSpacesOverlayTabRowView : public views::View {
  METADATA_HEADER(MahoSpacesOverlayTabRowView, views::View)

 public:
  explicit MahoSpacesOverlayTabRowView(const SpaceBoardTabItem& tab);
  MahoSpacesOverlayTabRowView(const MahoSpacesOverlayTabRowView&) = delete;
  MahoSpacesOverlayTabRowView& operator=(
      const MahoSpacesOverlayTabRowView&) = delete;
  ~MahoSpacesOverlayTabRowView() override;

  void OnThemeChanged() override;

 private:
  void UpdateColors();
  views::View* AddStatusChip(const std::u16string& text);

  SpaceBoardTabItem tab_;
  raw_ptr<views::View> glyph_container_ = nullptr;
  raw_ptr<views::Label> glyph_label_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> subtitle_label_ = nullptr;
  raw_ptr<views::View> chip_row_ = nullptr;
  std::vector<raw_ptr<views::Label>> chip_labels_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_TAB_ROW_VIEW_H_
