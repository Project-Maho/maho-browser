// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/notifications/maho_toast_view.h"

#include <algorithm>

#include "cc/paint/paint_flags.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace {

constexpr int kToastCornerRadius = 12;
constexpr int kIconChipSize = 28;
constexpr int kIconGlyphSize = 16;
constexpr int kMinToastWidth = 160;
constexpr int kMaxToastWidth = 360;

class ToastIconChip : public views::ImageView {
  METADATA_HEADER(ToastIconChip, views::ImageView)
 public:
  ToastIconChip() {
    SetPreferredSize(gfx::Size(kIconChipSize, kIconChipSize));
    SetHorizontalAlignment(views::ImageView::Alignment::kCenter);
    SetVerticalAlignment(views::ImageView::Alignment::kCenter);
  }
  ToastIconChip(const ToastIconChip&) = delete;
  ToastIconChip& operator=(const ToastIconChip&) = delete;
  ~ToastIconChip() override = default;

  void OnPaintBackground(gfx::Canvas* canvas) override {
    const ui::ColorProvider* cp = GetColorProvider();
    if (!cp) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(SkColorSetA(cp->GetColor(kMahoColorToastForeground), 0x1F));
    const gfx::RectF bounds(GetLocalBounds());
    canvas->DrawCircle(bounds.CenterPoint(), kIconChipSize / 2.0f, flags);
  }
};

BEGIN_METADATA(ToastIconChip)
END_METADATA

}  // namespace

MahoToastView::MahoToastView() {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(10, 12, 10, 16), 10));
  layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);

  icon_chip_ = AddChildView(std::make_unique<ToastIconChip>());

  auto* text_container = AddChildView(std::make_unique<views::View>());
  auto* text_layout =
      text_container->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  text_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStart);
  layout->SetFlexForView(text_container, 1);

  title_label_ = text_container->AddChildView(std::make_unique<views::Label>());
  title_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_label_->SetSubpixelRenderingEnabled(false);
  title_label_->SetAutoColorReadabilityEnabled(false);
  title_label_->SetFontList(title_label_->font_list().Derive(
      1, gfx::Font::NORMAL, gfx::Font::Weight::MEDIUM));

  body_label_ = text_container->AddChildView(std::make_unique<views::Label>());
  body_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  body_label_->SetSubpixelRenderingEnabled(false);
  body_label_->SetAutoColorReadabilityEnabled(false);
  body_label_->SetFontList(body_label_->font_list().Derive(
      0, gfx::Font::NORMAL, gfx::Font::Weight::NORMAL));
  body_label_->SetMultiLine(true);
  body_label_->SetMaxLines(2);
  body_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  body_label_->SetVisible(false);
}

MahoToastView::~MahoToastView() = default;

void MahoToastView::SetTitleText(const std::u16string& title) {
  title_label_->SetText(title);
  title_label_->SetVisible(!title.empty());
}

void MahoToastView::SetBodyText(const std::u16string& body) {
  body_label_->SetText(body);
  body_label_->SetVisible(!body.empty());
}

void MahoToastView::SetOnClicked(base::RepeatingClosure callback) {
  on_clicked_ = std::move(callback);
}

void MahoToastView::OnPaintBackground(gfx::Canvas* canvas) {
  const ui::ColorProvider* cp = GetColorProvider();
  const gfx::RectF bounds(GetLocalBounds());

  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setStyle(cc::PaintFlags::kFill_Style);
  fill.setColor(cp->GetColor(kMahoColorToastBackground));
  canvas->DrawRoundRect(bounds, kToastCornerRadius, fill);

  cc::PaintFlags border;
  border.setAntiAlias(true);
  border.setStyle(cc::PaintFlags::kStroke_Style);
  border.setStrokeWidth(1);
  border.setColor(cp->GetColor(kMahoColorToastBorder));
  gfx::RectF border_bounds = bounds;
  border_bounds.Inset(0.5f);
  canvas->DrawRoundRect(border_bounds, kToastCornerRadius, border);
}

gfx::Size MahoToastView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  gfx::Size size = views::View::CalculatePreferredSize(available_size);
  size.set_width(std::clamp(size.width(), kMinToastWidth, kMaxToastWidth));
  return size;
}

void MahoToastView::OnThemeChanged() {
  views::View::OnThemeChanged();
  const ui::ColorProvider* cp = GetColorProvider();
  const SkColor foreground = cp->GetColor(kMahoColorToastForeground);
  const SkColor background = cp->GetColor(kMahoColorToastBackground);

  if (icon_chip_) {
    icon_chip_->SetImage(ui::ImageModel::FromVectorIcon(
        maho_lucide_icons::kCopyIcon, foreground, kIconGlyphSize));
    icon_chip_->SchedulePaint();
  }
  title_label_->SetEnabledColor(foreground);
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
