// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_spaces_overlay_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "components/vector_icons/vector_icons.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho_spaces_overlay_board_view.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_constants.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"

namespace maho {

namespace {

constexpr int kOverlayInsetDp = 14;
constexpr int kSurfaceInsetDp = 14;
[[maybe_unused]] constexpr int kSurfaceCornerRadiusDp = 22;
constexpr int kSurfaceSpacingDp = 10;

}  // namespace

BEGIN_METADATA(MahoSpacesOverlayView)
END_METADATA

MahoSpacesOverlayView::MahoSpacesOverlayView(Browser* browser,
                                             CancelCallback cancel_callback,
                                             ActionCallback action_callback)
    : browser_(browser) {
  Init(BuildSpacesBoardModel(),
       std::move(cancel_callback),
       std::move(action_callback));
}

MahoSpacesOverlayView::MahoSpacesOverlayView(ForTestingTag,
                                             SpaceBoardModel model,
                                             CancelCallback cancel_callback,
                                             ActionCallback action_callback)
    : browser_(nullptr) {
  Init(std::move(model),
       std::move(cancel_callback),
       std::move(action_callback));
}

void MahoSpacesOverlayView::Init(SpaceBoardModel model,
                                 CancelCallback cancel_callback,
                                 ActionCallback action_callback) {
  cancel_callback_ = std::move(cancel_callback);
  action_callback_ = std::move(action_callback);
  model_ = std::move(model);

  AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(kOverlayInsetDp, kOverlayInsetDp), 0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  surface_ = AddChildView(std::make_unique<views::View>());
  auto* surface_layout = surface_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(kSurfaceInsetDp, kSurfaceInsetDp), kSurfaceSpacingDp));
  surface_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  layout->SetFlexForView(surface_, 1);

  board_scroll_view_ = surface_->AddChildView(std::make_unique<views::ScrollView>());
  board_scroll_view_->SetBackgroundColor(std::nullopt);
  board_scroll_view_->SetDrawOverflowIndicator(false);
  board_scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  board_scroll_view_->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  surface_layout->SetFlexForView(board_scroll_view_, 1);

  board_view_ = board_scroll_view_->SetContents(
      std::make_unique<MahoSpacesOverlayBoardView>(
          browser_,
          SpacesBoardRenderMode::kFullViewport,
          model_,
          base::BindRepeating(&MahoSpacesOverlayView::HandleSpaceSelected,
                              base::Unretained(this))));

  // Hairline separator
  separator_ = surface_->AddChildView(std::make_unique<views::View>());
  separator_->SetPreferredSize(gfx::Size(0, 1));
  separator_->SetVisible(false);
}

MahoSpacesOverlayView::~MahoSpacesOverlayView() = default;

void MahoSpacesOverlayView::RequestFocus() {
  if (board_view_ && !board_view_->children().empty()) {
    board_view_->children().front()->RequestFocus();
    return;
  }
  views::View::RequestFocus();
}

void MahoSpacesOverlayView::OnThemeChanged() {
  views::View::OnThemeChanged();
  const ui::ColorProvider* color_provider = GetColorProvider();
  if (!color_provider) {
    return;
  }

  const SkColor scrim = color_provider->GetColor(kMahoColorSidebarBackground);

  SetBackground(views::CreateSolidBackground(scrim));
}

bool MahoSpacesOverlayView::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  if (accelerator.key_code() == ui::VKEY_ESCAPE) {
    DismissCancel();
    return true;
  }
  return views::View::AcceleratorPressed(accelerator);
}

void MahoSpacesOverlayView::DismissCancel() {
  if (cancel_callback_) {
    std::move(cancel_callback_).Run();
  }
}

void MahoSpacesOverlayView::DismissAction() {
  if (action_callback_) {
    std::move(action_callback_).Run();
  }
}

void MahoSpacesOverlayView::HandleSpaceSelected(const std::string& space_id) {
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  if (bridge && bridge->SwitchToSpace(browser_, space_id)) {
    DismissAction();
  }
}

}  // namespace maho
