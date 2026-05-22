// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_spaces_overlay_board_view.h"

#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho_spaces_overlay_column_view.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"

namespace maho {

namespace {

constexpr int kBoardSpacingDp = 8;
constexpr int kBoardRightPaddingDp = 18;

}  // namespace

BEGIN_METADATA(MahoSpacesOverlayBoardView)
END_METADATA

MahoSpacesOverlayBoardView::MahoSpacesOverlayBoardView(
    const SpaceBoardModel& model,
    SpaceSelectedCallback callback)
    : model_(model), space_selected_callback_(std::move(callback)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(0, 0, 0, kBoardRightPaddingDp), kBoardSpacingDp));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  GetViewAccessibility().SetRole(ax::mojom::Role::kList);
  GetViewAccessibility().SetName(u"Spaces board");

  if (model_.columns.empty()) {
    empty_state_label_ = AddChildView(std::make_unique<views::Label>(
        u"No spaces available.", views::style::CONTEXT_LABEL,
        views::style::STYLE_BODY_3_MEDIUM));
    empty_state_label_->SetAutoColorReadabilityEnabled(false);
    empty_state_label_->SetSubpixelRenderingEnabled(false);
    empty_state_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    empty_state_label_->SetMultiLine(true);
    empty_state_label_->SetEnabledColor(kMahoColorSecondaryText);
    return;
  }

  for (const SpaceBoardColumn& column : model_.columns) {
    const bool is_active = column.is_active ||
                           (!model_.active_space_id.empty() &&
                            column.space_id == model_.active_space_id);
    AddChildView(std::make_unique<MahoSpacesOverlayColumnView>(
        column, is_active, space_selected_callback_));
  }
}

MahoSpacesOverlayBoardView::~MahoSpacesOverlayBoardView() = default;

void MahoSpacesOverlayBoardView::OnThemeChanged() {
  views::View::OnThemeChanged();
  if (empty_state_label_) {
    empty_state_label_->SetEnabledColor(kMahoColorSecondaryText);
  }
}

}  // namespace maho
