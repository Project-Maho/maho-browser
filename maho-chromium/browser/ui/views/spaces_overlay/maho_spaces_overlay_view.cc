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
constexpr int kSurfaceCornerRadiusDp = 22;
constexpr int kSurfaceSpacingDp = 10;
constexpr int kHeaderSpacingDp = 8;
constexpr int kTitleBlockSpacingDp = 2;
constexpr int kCloseButtonSizeDp = 28;

std::u16string BoardSummaryText(const SpaceBoardModel& model) {
  int total_tabs = 0;
  std::u16string active_name;
  for (const SpaceBoardColumn& column : model.columns) {
    total_tabs += std::max(column.tab_count,
                           static_cast<int>(column.tabs.size()));
    if ((column.is_active || column.space_id == model.active_space_id) &&
        active_name.empty()) {
      active_name = column.name;
    }
  }

  std::u16string summary =
      base::UTF8ToUTF16(base::NumberToString(model.columns.size())) +
      (model.columns.size() == 1 ? u" space" : u" spaces");
  summary += u" · ";
  summary += base::UTF8ToUTF16(base::NumberToString(total_tabs));
  summary += total_tabs == 1 ? u" tab" : u" tabs";

  if (!active_name.empty()) {
    summary += u" · Active: ";
    summary += active_name;
  }

  return summary;
}

}  // namespace

BEGIN_METADATA(MahoSpacesOverlayView)
END_METADATA

MahoSpacesOverlayView::MahoSpacesOverlayView(Browser* browser,
                                             CancelCallback cancel_callback,
                                             ActionCallback action_callback)
    : MahoSpacesOverlayView(ForTestingTag{},
                            BuildSpacesBoardModel(),
                            std::move(cancel_callback),
                            std::move(action_callback)) {
  browser_ = browser;
}

MahoSpacesOverlayView::MahoSpacesOverlayView(ForTestingTag,
                                             SpaceBoardModel model,
                                             CancelCallback cancel_callback,
                                             ActionCallback action_callback)
    : browser_(nullptr),
      cancel_callback_(std::move(cancel_callback)),
      action_callback_(std::move(action_callback)),
      model_(std::move(model)) {
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

  auto* utility_row = surface_->AddChildView(std::make_unique<views::View>());
  auto* header_layout = utility_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kHeaderSpacingDp));
  header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto* title_block = utility_row->AddChildView(std::make_unique<views::View>());
  auto* title_layout = title_block->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), kTitleBlockSpacingDp));
  title_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  header_layout->SetFlexForView(title_block, 1);

  auto* title_label = title_block->AddChildView(std::make_unique<views::Label>(
      u"Spaces", views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_3_MEDIUM));
  title_label->SetAutoColorReadabilityEnabled(false);
  title_label->SetSubpixelRenderingEnabled(false);
  title_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_label->SetEnabledColor(kMahoColorPrimaryText);

  summary_label_ = title_block->AddChildView(std::make_unique<views::Label>(
      BoardSummaryText(model_), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_5));
  summary_label_->SetAutoColorReadabilityEnabled(false);
  summary_label_->SetSubpixelRenderingEnabled(false);
  summary_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);

  close_button_ = utility_row->AddChildView(std::make_unique<views::ImageButton>(
      base::BindRepeating(&MahoSpacesOverlayView::HandleClosePressed,
                          base::Unretained(this))));
  close_button_->SetPreferredSize(
      gfx::Size(kCloseButtonSizeDp, kCloseButtonSizeDp));
  close_button_->SetTooltipText(u"Close spaces overlay");
  close_button_->SetAccessibleName(u"Close spaces overlay");
  close_button_->SetRequestFocusOnPress(false);

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
          model_,
          base::BindRepeating(&MahoSpacesOverlayView::HandleSpaceSelected,
                              base::Unretained(this))));
}

MahoSpacesOverlayView::~MahoSpacesOverlayView() = default;

std::u16string MahoSpacesOverlayView::GetSummaryTextForTesting() const {
  return summary_label_ ? std::u16string(summary_label_->GetText()) : std::u16string();
}

void MahoSpacesOverlayView::RequestFocus() {
  if (close_button_) {
    close_button_->RequestFocus();
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

  const SkColor scrim = SkColorSetA(
      color_provider->GetColor(kMahoColorWindowBackground), 0xE8);
  const SkColor surface_fill = SkColorSetA(
      color_provider->GetColor(kMahoColorContentBackground), 0xF0);
  const SkColor surface_border = SkColorSetA(
      color_provider->GetColor(kMahoColorContentBorder), 0xA6);
  const SkColor close_fill = SkColorSetA(
      color_provider->GetColor(kMahoColorSidebarHover), 0x7A);
  const SkColor close_border = SkColorSetA(
      color_provider->GetColor(kMahoColorCardBorder), 0x66);

  SetBackground(views::CreateSolidBackground(scrim));
  surface_->SetBackground(
      views::CreateRoundedRectBackground(surface_fill, kSurfaceCornerRadiusDp));
  surface_->SetBorder(views::CreateRoundedRectBorder(1, kSurfaceCornerRadiusDp,
                                                     surface_border));

  close_button_->SetBackground(
      views::CreateRoundedRectBackground(close_fill, kCloseButtonSizeDp / 2));
  close_button_->SetBorder(views::CreateRoundedRectBorder(
      1, kCloseButtonSizeDp / 2, close_border));
  const ui::ImageModel close_icon = ui::ImageModel::FromVectorIcon(
      vector_icons::kCloseChromeRefreshIcon, kMahoColorPrimaryText, 14);
  close_button_->SetImageModel(views::Button::STATE_NORMAL, close_icon);
  close_button_->SetImageModel(views::Button::STATE_HOVERED, close_icon);
  close_button_->SetImageModel(views::Button::STATE_PRESSED, close_icon);

  summary_label_->SetEnabledColor(kMahoColorTertiaryText);
  UpdateSummaryText();
}

bool MahoSpacesOverlayView::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  if (accelerator.key_code() == ui::VKEY_ESCAPE) {
    DismissCancel();
    return true;
  }
  return views::View::AcceleratorPressed(accelerator);
}

void MahoSpacesOverlayView::UpdateSummaryText() {
  if (summary_label_) {
    summary_label_->SetText(BoardSummaryText(model_));
  }
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

void MahoSpacesOverlayView::HandleClosePressed() {
  DismissCancel();
}

void MahoSpacesOverlayView::HandleSpaceSelected(const std::string& space_id) {
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  if (bridge && bridge->SwitchToSpace(browser_, space_id)) {
    DismissAction();
  }
}

}  // namespace maho
