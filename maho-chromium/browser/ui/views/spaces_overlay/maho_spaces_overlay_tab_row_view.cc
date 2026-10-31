// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_spaces_overlay_tab_row_view.h"

#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/cursor/mojom/cursor_type.mojom-shared.h"
#include "ui/views/controls/image_view.h"
#include "url/gurl.h"

#include <string>

#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"

namespace maho {

namespace {

constexpr int kRowPaddingVerticalDp = 3;
constexpr int kGlyphSizeDp = 16;
constexpr int kTextSpacingDp = 6;
constexpr int kHeaderSpacingDp = 6;
constexpr int kChipSpacingDp = 4;

}  // namespace

BEGIN_METADATA(MahoSpacesOverlayTabRowView)
END_METADATA

MahoSpacesOverlayTabRowView::MahoSpacesOverlayTabRowView(
    Profile* profile,
    const SpaceBoardTabItem& tab,
    const std::string& space_id,
    TabSelectedCallback callback)
    : tab_(tab), space_id_(space_id), callback_(std::move(callback)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(kRowPaddingVerticalDp, 6), kHeaderSpacingDp));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  favicon_view_ = AddChildView(std::make_unique<views::ImageView>());
  favicon_view_->SetPreferredSize(gfx::Size(kGlyphSizeDp, kGlyphSizeDp));
  // The globe placeholder is painted from the palette in UpdateColors().

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
  if (!result.image.IsEmpty() && favicon_view_) {
    favicon_loaded_ = true;
    favicon_view_->SetImage(ui::ImageModel::FromImageSkia(result.image.AsImageSkia()));
  }
}

void MahoSpacesOverlayTabRowView::SetPalette(
    const MahoSidebarPalette& palette) {
  palette_ = palette;
  UpdateColors();
}

void MahoSpacesOverlayTabRowView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateColors();
}

void MahoSpacesOverlayTabRowView::UpdateColors() {
  if (IsMouseHovered() && !callback_.is_null()) {
    SetBackground(
        views::CreateRoundedRectBackground(palette_.row_hover, 6));
  } else {
    SetBackground(nullptr);
  }
  SetBorder(nullptr);

  if (!favicon_loaded_) {
    favicon_view_->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kGlobeIcon, palette_.neutral_glyph, kGlyphSizeDp));
  }
  title_label_->SetEnabledColor(palette_.primary_text);
  // Subtitle and status chips are readable text: secondary role.
  subtitle_label_->SetEnabledColor(palette_.secondary_text);

  for (views::View* chip : chip_row_->children()) {
    chip->SetBackground(nullptr);
    chip->SetBorder(nullptr);
  }
  for (views::Label* label : chip_labels_) {
    label->SetEnabledColor(palette_.secondary_text);
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

bool MahoSpacesOverlayTabRowView::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsLeftMouseButton() && !callback_.is_null()) {
    callback_.Run(space_id_, tab_.id);
    return true;
  }
  return views::View::OnMousePressed(event);
}

ui::Cursor MahoSpacesOverlayTabRowView::GetCursor(const ui::MouseEvent& event) {
  return ui::mojom::CursorType::kHand;
}

void MahoSpacesOverlayTabRowView::OnMouseEntered(const ui::MouseEvent& event) {
  views::View::OnMouseEntered(event);
  UpdateColors();
}

void MahoSpacesOverlayTabRowView::OnMouseExited(const ui::MouseEvent& event) {
  views::View::OnMouseExited(event);
  UpdateColors();
}

}  // namespace maho
