// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_spaces_overlay_tab_row_view.h"

#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "ui/views/controls/image_view.h"
#include "url/gurl.h"

#include <string>

#include "base/strings/string_util.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"

namespace maho {

namespace {

constexpr int kRowPaddingVerticalDp = 3;
constexpr int kGlyphSizeDp = 16;
constexpr int kGlyphCornerRadiusDp = 5;
constexpr int kTextSpacingDp = 6;
constexpr int kHeaderSpacingDp = 6;
constexpr int kChipSpacingDp = 4;

std::u16string TabGlyphText(const SpaceBoardTabItem& tab) {
  std::u16string title = SpaceBoardTabCompactTitle(tab);
  for (char16_t ch : title) {
    if (base::IsAsciiAlpha(ch)) {
      return std::u16string(1, base::ToUpperASCII(ch));
    }
    if (base::IsAsciiDigit(ch)) {
      return std::u16string(1, ch);
    }
  }
  return u"•";
}

}  // namespace

BEGIN_METADATA(MahoSpacesOverlayTabRowView)
END_METADATA

MahoSpacesOverlayTabRowView::MahoSpacesOverlayTabRowView(
    Profile* profile,
    const SpaceBoardTabItem& tab)
    : tab_(tab) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(kRowPaddingVerticalDp, 0), kHeaderSpacingDp));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  favicon_view_ = AddChildView(std::make_unique<views::ImageView>());
  favicon_view_->SetPreferredSize(gfx::Size(kGlyphSizeDp, kGlyphSizeDp));
  favicon_view_->SetVisible(false);

  glyph_container_ = AddChildView(std::make_unique<views::View>());
  glyph_container_->SetPreferredSize(gfx::Size(kGlyphSizeDp, kGlyphSizeDp));
  glyph_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal));
  static_cast<views::BoxLayout*>(glyph_container_->GetLayoutManager())
      ->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  static_cast<views::BoxLayout*>(glyph_container_->GetLayoutManager())
      ->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  glyph_label_ = glyph_container_->AddChildView(std::make_unique<views::Label>(
      TabGlyphText(tab_), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_4_MEDIUM));
  glyph_label_->SetAutoColorReadabilityEnabled(false);
  glyph_label_->SetSubpixelRenderingEnabled(false);
  glyph_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);

  // Default to showing glyph container
  glyph_container_->SetVisible(true);

  if (profile && !tab_.url.empty()) {
    favicon::FaviconService* service =
        FaviconServiceFactory::GetForProfile(
            profile, ServiceAccessType::EXPLICIT_ACCESS);
    if (service) {
      service->GetFaviconImageForPageURL(
          GURL(tab_.url),
          base::BindOnce(&MahoSpacesOverlayTabRowView::OnFaviconLoaded,
                         weak_ptr_factory_.GetWeakPtr()),
          &cancelable_task_tracker_);
    }
  }

  auto* text_block = AddChildView(std::make_unique<views::View>());
  auto* text_layout = text_block->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kTextSpacingDp));
  text_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  layout->SetFlexForView(text_block, 1);

  title_label_ = text_block->AddChildView(std::make_unique<views::Label>(
      SpaceBoardTabCompactTitle(tab_), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_5_MEDIUM));
  title_label_->SetAutoColorReadabilityEnabled(false);
  title_label_->SetSubpixelRenderingEnabled(false);
  title_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  text_layout->SetFlexForView(title_label_, 1);

  subtitle_label_ = text_block->AddChildView(std::make_unique<views::Label>(
      SpaceBoardTabSubtitle(tab_), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_5));
  subtitle_label_->SetAutoColorReadabilityEnabled(false);
  subtitle_label_->SetSubpixelRenderingEnabled(false);
  subtitle_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  subtitle_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  subtitle_label_->SetVisible(false);

  chip_row_ = AddChildView(std::make_unique<views::View>());
  auto* chip_layout = chip_row_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kChipSpacingDp));
  chip_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  int chip_count = 0;
  if (tab_.is_pinned) {
    AddStatusChip(u"Pinned");
    ++chip_count;
  }
  if (tab_.is_loading) {
    AddStatusChip(u"Loading");
    ++chip_count;
  }
  if (tab_.is_playing_audio) {
    AddStatusChip(tab_.is_muted ? u"Muted" : u"Audio");
    ++chip_count;
  }
  if (tab_.is_favorite) {  // L3-EXEMPT: views display
    AddStatusChip(u"Saved");
    ++chip_count;
  }
  chip_row_->SetVisible(chip_count > 0);

  GetViewAccessibility().SetRole(ax::mojom::Role::kListItem);
  GetViewAccessibility().SetName(SpaceBoardTabCompactTitle(tab_));

  UpdateColors();
}

MahoSpacesOverlayTabRowView::~MahoSpacesOverlayTabRowView() = default;

void MahoSpacesOverlayTabRowView::OnFaviconLoaded(
    const favicon_base::FaviconImageResult& result) {
  if (!result.image.IsEmpty() && favicon_view_ && glyph_container_) {
    favicon_view_->SetImage(ui::ImageModel::FromImageSkia(result.image.AsImageSkia()));
    favicon_view_->SetVisible(true);
    glyph_container_->SetVisible(false);
  }
}

void MahoSpacesOverlayTabRowView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateColors();
}

void MahoSpacesOverlayTabRowView::UpdateColors() {
  const ui::ColorProvider* color_provider = GetColorProvider();
  if (!color_provider) {
    return;
  }

  const SkColor glyph_fill = SkColorSetA(
      color_provider->GetColor(kMahoColorAccentBlueBright), 0x24);
  const SkColor glyph_border = SkColorSetA(
      color_provider->GetColor(kMahoColorAccentBlue), 0x18);

  SetBackground(nullptr);
  SetBorder(nullptr);
  glyph_container_->SetBackground(
      views::CreateRoundedRectBackground(glyph_fill, kGlyphCornerRadiusDp));
  glyph_container_->SetBorder(
      views::CreateRoundedRectBorder(1, kGlyphCornerRadiusDp, glyph_border));

  glyph_label_->SetEnabledColor(kMahoColorAccentBlueBright);
  title_label_->SetEnabledColor(kMahoColorPrimaryText);
  subtitle_label_->SetEnabledColor(kMahoColorTertiaryText);

  for (views::View* chip : chip_row_->children()) {
    chip->SetBackground(nullptr);
    chip->SetBorder(nullptr);
  }
  for (views::Label* label : chip_labels_) {
    label->SetEnabledColor(kMahoColorTertiaryText);
  }
}

views::View* MahoSpacesOverlayTabRowView::AddStatusChip(
    const std::u16string& text) {
  auto chip = std::make_unique<views::View>();
  chip->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal));
  auto* chip_ptr = chip.get();

  auto* label = chip->AddChildView(std::make_unique<views::Label>(
      text, views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
  label->SetAutoColorReadabilityEnabled(false);
  label->SetSubpixelRenderingEnabled(false);
  label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  chip_labels_.push_back(label);
  chip_row_->AddChildView(std::move(chip));
  return chip_ptr;
}

}  // namespace maho
