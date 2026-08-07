// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_translate_popover_view.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/ui_base_types.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {
constexpr int kPopoverCornerRadiusDp = 14;
constexpr int kPopoverPaddingDp = 14;
constexpr int kPopoverRowSpacingDp = 12;
constexpr int kBadgeSizeDp = 34;
}  // namespace

MahoTranslatePopoverView::MahoTranslatePopoverView(
    const std::u16string& target_language_name,
    base::RepeatingClosure translate_callback,
    OptionsCallback options_callback)
    : translate_callback_(std::move(translate_callback)),
      options_callback_(std::move(options_callback)) {
  SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysBaseContainerElevated, kPopoverCornerRadiusDp));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets(kPopoverPaddingDp), kPopoverRowSpacingDp));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto* prompt_row = AddChildView(std::make_unique<views::View>());
  auto* prompt_layout = prompt_row->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          kPopoverRowSpacingDp));
  prompt_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto badge = std::make_unique<views::Label>(u"Aa");
  badge->SetAutoColorReadabilityEnabled(false);
  badge->SetEnabledColor(ui::kColorSysOnSurface);
  badge->SetPreferredSize(gfx::Size(kBadgeSizeDp, kBadgeSizeDp));
  badge->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysStateHoverOnSubtle, kBadgeSizeDp / 2));
  prompt_row->AddChildView(std::move(badge));

  auto* prompt = prompt_row->AddChildView(std::make_unique<views::Label>(
      std::u16string(u"Translate this page to ") + target_language_name +
      u"?"));
  prompt->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  prompt->SetMultiLine(true);
  prompt->SetAutoColorReadabilityEnabled(false);
  prompt->SetEnabledColor(ui::kColorSysOnSurface);
  prompt->SetTextStyle(views::style::STYLE_BODY_3_MEDIUM);
  prompt_layout->SetFlexForView(prompt, 1);

  auto* button_row = AddChildView(std::make_unique<views::View>());
  auto* button_layout = button_row->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          kPopoverRowSpacingDp));
  button_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kEnd);

  auto options = std::make_unique<views::MdTextButton>(
      base::BindRepeating(&MahoTranslatePopoverView::OnOptionsPressed,
                          weak_factory_.GetWeakPtr()),
      u"Options");
  options->SetStyle(ui::ButtonStyle::kTonal);
  options_button_ = button_row->AddChildView(std::move(options));

  auto translate = std::make_unique<views::MdTextButton>(
      base::BindRepeating(&MahoTranslatePopoverView::OnTranslatePressed,
                          weak_factory_.GetWeakPtr()),
      u"Translate");
  translate->SetStyle(ui::ButtonStyle::kProminent);
  button_row->AddChildView(std::move(translate));
}

MahoTranslatePopoverView::~MahoTranslatePopoverView() = default;

// static
views::Widget* MahoTranslatePopoverView::Show(
    views::View* anchor_view,
    const std::u16string& target_language_name,
    base::RepeatingClosure translate_callback,
    OptionsCallback options_callback) {
  if (!anchor_view) {
    return nullptr;
  }

  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor_view, views::BubbleBorder::TOP_LEFT);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->set_margins(gfx::Insets());
  delegate->SetContentsView(std::make_unique<MahoTranslatePopoverView>(
      target_language_name, std::move(translate_callback),
      std::move(options_callback)));

  views::Widget* widget =
      views::BubbleDialogDelegate::CreateBubble(std::move(delegate));
  if (widget) {
    widget->Show();
  }
  return widget;
}

void MahoTranslatePopoverView::OnTranslatePressed() {
  if (translate_callback_) {
    translate_callback_.Run();
  }
  if (GetWidget()) {
    GetWidget()->CloseWithReason(views::Widget::ClosedReason::kAcceptButtonClicked);
  }
}

void MahoTranslatePopoverView::OnOptionsPressed() {
  if (options_callback_) {
    options_callback_.Run(
        options_button_,
        base::BindRepeating(&MahoTranslatePopoverView::ClosePopover,
                            weak_factory_.GetWeakPtr()));
  }
}

void MahoTranslatePopoverView::ClosePopover() {
  if (GetWidget()) {
    GetWidget()->CloseWithReason(
        views::Widget::ClosedReason::kAcceptButtonClicked);
  }
}

BEGIN_METADATA(MahoTranslatePopoverView)
END_METADATA

}  // namespace maho
