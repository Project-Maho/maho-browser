// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_spaces_view.h"

#include <memory>
#include <string>

#include "base/functional/bind.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"

#include "maho/browser/ui/views/sidebar/maho_spaces_board_data.h"
#include "maho/browser/ui/views/spaces_overlay/maho_spaces_overlay_board_view.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"

namespace maho {

BEGIN_METADATA(MahoSidebarSpacesView)
END_METADATA

MahoSidebarSpacesView::MahoSidebarSpacesView(Browser* browser)
    : browser_(browser) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  GetViewAccessibility().SetName(u"Spaces panel");

  scroll_view_ = AddChildView(std::make_unique<views::ScrollView>());
  scroll_view_->SetBackgroundColor(std::nullopt);
  scroll_view_->SetDrawOverflowIndicator(false);
  scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  scroll_view_->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  layout->SetFlexForView(scroll_view_, 1, true);

  scroll_contents_ = scroll_view_->SetContents(std::make_unique<views::View>());
  scroll_contents_->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::VH(sidebar_layout::kSpacesScrollPaddingDp,
                       sidebar_layout::kSpacesScrollPaddingDp)));
  auto* contents_layout = scroll_contents_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal));
  contents_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
}

MahoSidebarSpacesView::~MahoSidebarSpacesView() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void MahoSidebarSpacesView::ReloadSpaces() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!scroll_contents_) {
    return;
  }

  board_view_ = nullptr;
  scroll_contents_->RemoveAllChildViews();
  board_view_ = scroll_contents_->AddChildView(
      std::make_unique<MahoSpacesOverlayBoardView>(
          browser_,
          SpacesBoardRenderMode::kEmbedded,
          BuildSpacesBoardModel(),
          base::BindRepeating(&MahoSidebarSpacesView::HandleSpaceSelected,
                              base::Unretained(this))));
}

void MahoSidebarSpacesView::HandleSpaceSelected(const std::string& space_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!browser_) {
    return;
  }

  if (MahoSidebarView::ActivateSpaceAndTab(browser_, space_id)) {
    ReloadSpaces();
  }
}


}  // namespace maho
