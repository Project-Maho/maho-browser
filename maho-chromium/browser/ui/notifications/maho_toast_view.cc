// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/notifications/maho_toast_view.h"

#include <algorithm>

#include "maho/browser/ui/theme/maho_color_id.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

MahoToastView::MahoToastView() {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(12), 4));
  layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);

  auto title = std::make_unique<views::Label>();
  title_label_ = AddChildView(std::move(title));
  title_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_label_->SetSubpixelRenderingEnabled(false);
  title_label_->SetAutoColorReadabilityEnabled(false);
  title_label_->SetFontList(title_label_->font_list().Derive(
      2, gfx::Font::NORMAL, gfx::Font::Weight::BOLD));

  auto body = std::make_unique<views::Label>();
  body_label_ = AddChildView(std::move(body));
  body_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  body_label_->SetSubpixelRenderingEnabled(false);
  body_label_->SetAutoColorReadabilityEnabled(false);
  body_label_->SetFontList(body_label_->font_list().Derive(
      0, gfx::Font::NORMAL, gfx::Font::Weight::NORMAL));
  body_label_->SetMultiLine(true);
  body_label_->SetMaxLines(2);
  body_label_->SetElideBehavior(gfx::ELIDE_TAIL);
}

MahoToastView::~MahoToastView() = default;

void MahoToastView::SetTitleText(const std::u16string& title) {
  title_label_->SetText(title);
}

void MahoToastView::SetBodyText(const std::u16string& body) {
  body_label_->SetText(body);
}

void MahoToastView::SetOnClicked(base::RepeatingClosure callback) {
  on_clicked_ = std::move(callback);
}

void MahoToastView::OnPaintBackground(gfx::Canvas* canvas) {
  const auto* cp = GetColorProvider();
  cc::PaintFlags flags;
  flags.setAntiAlias(true);

  // Background fill
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(cp->GetColor(kMahoColorToastBackground));
  canvas->DrawRoundRect(GetLocalBounds(), 10, flags);

  // Border stroke
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(1.0f);
  flags.setColor(cp->GetColor(kMahoColorToastBorder));
  canvas->DrawRoundRect(GetLocalBounds(), 10, flags);
}

gfx::Size MahoToastView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  gfx::Size size = views::View::CalculatePreferredSize(available_size);
  size.set_width(std::clamp(size.width(), 240, 360));
  return size;
}

void MahoToastView::OnThemeChanged() {
  views::View::OnThemeChanged();
  const auto* cp = GetColorProvider();
  const SkColor background = cp->GetColor(kMahoColorToastBackground);
  title_label_->SetEnabledColor(cp->GetColor(kMahoColorToastForeground));
  title_label_->SetBackgroundColor(background);
  body_label_->SetEnabledColor(cp->GetColor(kMahoColorToastBodyForeground));
  body_label_->SetBackgroundColor(background);
  SchedulePaint();
}

bool MahoToastView::OnMousePressed(const ui::MouseEvent& event) {
  if (on_clicked_) {
    on_clicked_.Run();
    return true;
  }
  return false;
}

BEGIN_METADATA(MahoToastView)
END_METADATA
