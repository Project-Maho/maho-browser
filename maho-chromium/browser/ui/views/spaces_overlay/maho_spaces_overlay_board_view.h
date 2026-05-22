// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_BOARD_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_BOARD_VIEW_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "maho/browser/ui/views/sidebar/maho_spaces_board_data.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class Label;
}  // namespace views

namespace maho {

class MahoSpacesOverlayBoardView : public views::View {
  METADATA_HEADER(MahoSpacesOverlayBoardView, views::View)

 public:
  using SpaceSelectedCallback = base::RepeatingCallback<void(const std::string&)>;

  MahoSpacesOverlayBoardView(const SpaceBoardModel& model,
                             SpaceSelectedCallback callback);
  MahoSpacesOverlayBoardView(const MahoSpacesOverlayBoardView&) = delete;
  MahoSpacesOverlayBoardView& operator=(
      const MahoSpacesOverlayBoardView&) = delete;
  ~MahoSpacesOverlayBoardView() override;

  void OnThemeChanged() override;

 private:
  SpaceBoardModel model_;
  SpaceSelectedCallback space_selected_callback_;
  raw_ptr<views::Label> empty_state_label_ = nullptr;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SPACES_OVERLAY_MAHO_SPACES_OVERLAY_BOARD_VIEW_H_
