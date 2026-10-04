// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_SPACES_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_SPACES_VIEW_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/sequence_checker.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class Browser;

namespace views {
class ScrollView;
class View;
}  // namespace views

namespace maho {

class MahoSpacesOverlayBoardView;

class MahoSidebarSpacesView : public views::View {
  METADATA_HEADER(MahoSidebarSpacesView, views::View)

 public:
  explicit MahoSidebarSpacesView(Browser* browser);
  MahoSidebarSpacesView(const MahoSidebarSpacesView&) = delete;
  MahoSidebarSpacesView& operator=(const MahoSidebarSpacesView&) = delete;
  ~MahoSidebarSpacesView() override;

  void ReloadSpaces();

  views::ScrollView* scroll_view_for_testing() { return scroll_view_; }
  MahoSpacesOverlayBoardView* board_view_for_testing() { return board_view_; }

 private:
  void HandleSpaceSelected(const std::string& space_id);

  raw_ptr<Browser> browser_ = nullptr;
  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<views::View> scroll_contents_ = nullptr;
  raw_ptr<MahoSpacesOverlayBoardView> board_view_ = nullptr;
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_SPACES_VIEW_H_
