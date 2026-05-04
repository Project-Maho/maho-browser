// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_result_row_view.h"

#include <string>

#include "base/strings/utf_string_conversions.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/styled_label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/style/typography.h"

namespace maho {

namespace {

constexpr int kRowHeightDp = 44;
constexpr int kRowCornerRadiusDp = 10;
constexpr int kRowPaddingHDp = 12;
constexpr int kRowPaddingVDp = 6;
constexpr int kGlyphSizeDp = 24;
constexpr int kGlyphCornerRadiusDp = 6;
constexpr int kMetadataCornerRadiusDp = 6;
constexpr int kMetadataPaddingHDp = 6;
constexpr int kMetadataPaddingVDp = 2;
constexpr int kRowItemSpacingDp = 10;
constexpr int kTitleSubtitleSpacingDp = 2;

constexpr SkColor kTitleColor = SkColorSetRGB(0xF0, 0xF2, 0xF5);
constexpr SkColor kSubtitleColor = SkColorSetRGB(0x78, 0x82, 0x9A);
constexpr SkColor kMetadataTextColor = SkColorSetRGB(0x8A, 0x92, 0xA6);
constexpr SkColor kMetadataBackgroundColor = SkColorSetARGB(0x14, 0xFF, 0xFF,
                                                            0xFF);
constexpr SkColor kSelectedRowColor = SkColorSetRGB(0x3B, 0x4C, 0xE8);
constexpr SkColor kSelectedSecondaryTextColor =
    SkColorSetARGB(0xD0, 0xFF, 0xFF, 0xFF);

}  // namespace

BEGIN_METADATA(MahoCommandResultRowView)
END_METADATA

MahoCommandResultRowView::MahoCommandResultRowView(
    const CommandSuggestion& suggestion)
    : type_(suggestion.type), match_ranges_(suggestion.match_ranges) {
  SetPreferredSize(gfx::Size(0, kRowHeightDp));

  auto* row_layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(kRowPaddingVDp, kRowPaddingHDp), kRowItemSpacingDp));
  row_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  glyph_container_ = AddChildView(std::make_unique<views::View>());
  glyph_container_->SetPreferredSize(gfx::Size(kGlyphSizeDp, kGlyphSizeDp));
  glyph_container_->SetLayoutManager(std::make_unique<views::FillLayout>());

  glyph_label_ = glyph_container_->AddChildView(std::make_unique<views::Label>(
      GlyphText(suggestion.type), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_5));
  glyph_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  glyph_label_->SetVerticalAlignment(gfx::ALIGN_MIDDLE);

  auto* text_container = AddChildView(std::make_unique<views::View>());
  auto* text_layout =
      text_container->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(),
          kTitleSubtitleSpacingDp));
  text_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  row_layout->SetFlexForView(text_container, 1);

  std::u16string title_text = base::UTF8ToUTF16(suggestion.title);
  title_label_ =
      text_container->AddChildView(std::make_unique<views::StyledLabel>());
  title_label_->SetText(title_text);
  title_label_->SetDefaultTextStyle(views::style::STYLE_BODY_4);
  title_label_->SetAutoColorReadabilityEnabled(false);

  if (!suggestion.subtitle.empty()) {
    subtitle_label_ =
        text_container->AddChildView(std::make_unique<views::Label>(
            base::UTF8ToUTF16(suggestion.subtitle),
            views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
    subtitle_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    subtitle_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  }

  metadata_label_ = AddChildView(std::make_unique<views::Label>(
      TypeText(suggestion.type), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_5));
  metadata_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  metadata_label_->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::VH(kMetadataPaddingVDp, kMetadataPaddingHDp)));

  UpdateColors();
}

MahoCommandResultRowView::~MahoCommandResultRowView() = default;

void MahoCommandResultRowView::ApplyTitleStyles(SkColor text_color) {
  title_label_->ClearStyleRanges();

  views::StyledLabel::RangeStyleInfo regular_style;
  regular_style.text_style = views::style::STYLE_BODY_4;
  regular_style.override_color = text_color;
  title_label_->AddStyleRange(gfx::Range(0, title_label_->GetText().size()),
                              regular_style);

  for (const auto& [start, end] : match_ranges_) {
    if (start < 0 || end <= start ||
        static_cast<size_t>(end) > title_label_->GetText().size()) {
      continue;
    }

    views::StyledLabel::RangeStyleInfo bold_style;
    bold_style.text_style = views::style::STYLE_BODY_4_BOLD;
    bold_style.override_color = text_color;
    title_label_->AddStyleRange(gfx::Range(start, end), bold_style);
  }
}

void MahoCommandResultRowView::SetSelected(bool selected) {
  if (selected_ == selected)
    return;
  selected_ = selected;
  UpdateColors();
}

void MahoCommandResultRowView::UpdateColors() {
  if (selected_) {
    SetBackground(views::CreateRoundedRectBackground(kSelectedRowColor,
                                                     kRowCornerRadiusDp));
    SetBorder(nullptr);
  } else {
    SetBackground(nullptr);
    SetBorder(nullptr);
  }

  glyph_container_->SetBackground(views::CreateRoundedRectBackground(
      selected_ ? SkColorSetARGB(0x1E, 0xFF, 0xFF, 0xFF)
                : GlyphBackgroundColor(type_),
      kGlyphCornerRadiusDp));
  glyph_label_->SetEnabledColor(selected_ ? SK_ColorWHITE : kTitleColor);
  ApplyTitleStyles(selected_ ? SK_ColorWHITE : kTitleColor);

  if (subtitle_label_) {
    subtitle_label_->SetEnabledColor(selected_ ? kSelectedSecondaryTextColor
                                               : kSubtitleColor);
  }

  metadata_label_->SetBackground(views::CreateRoundedRectBackground(
      selected_ ? SkColorSetARGB(0x1F, 0xFF, 0xFF, 0xFF)
                : kMetadataBackgroundColor,
      kMetadataCornerRadiusDp));
  metadata_label_->SetEnabledColor(selected_ ? SK_ColorWHITE
                                             : kMetadataTextColor);
}

// static
std::u16string MahoCommandResultRowView::TypeText(
    CommandSuggestionType type) {
  switch (type) {
    case CommandSuggestionType::kTab:
      return u"Tab";
    case CommandSuggestionType::kBookmark:
      return u"Bookmark";
    case CommandSuggestionType::kHistory:
      return u"History";
    case CommandSuggestionType::kAction:
      return u"Action";
    case CommandSuggestionType::kNavigation:
      return u"Go to";
    case CommandSuggestionType::kCalculator:
      return u"Calc";
    case CommandSuggestionType::kUnitConversion:
      return u"Convert";
    case CommandSuggestionType::kArchivedTab:
      return u"Archived";
    case CommandSuggestionType::kClosedTab:
      return u"Closed";
  }
  return u"";
}

// static
std::u16string MahoCommandResultRowView::GlyphText(
    CommandSuggestionType type) {
  switch (type) {
    case CommandSuggestionType::kTab:
      return u"T";
    case CommandSuggestionType::kBookmark:
      return u"B";
    case CommandSuggestionType::kHistory:
      return u"H";
    case CommandSuggestionType::kAction:
      return u"A";
    case CommandSuggestionType::kNavigation:
      return u"\u2192";
    case CommandSuggestionType::kCalculator:
      return u"=";
    case CommandSuggestionType::kUnitConversion:
      return u"\u21C4";
    case CommandSuggestionType::kArchivedTab:
      return u"Ar";
    case CommandSuggestionType::kClosedTab:
      return u"C";
  }
  return u"?";
}

// static
SkColor MahoCommandResultRowView::GlyphBackgroundColor(
    CommandSuggestionType type) {
  switch (type) {
    case CommandSuggestionType::kTab:
      return SkColorSetARGB(0x20, 0x79, 0x9A, 0xFF);
    case CommandSuggestionType::kBookmark:
      return SkColorSetARGB(0x20, 0xF0, 0xC6, 0x66);
    case CommandSuggestionType::kHistory:
      return SkColorSetARGB(0x20, 0xA8, 0xB3, 0xC7);
    case CommandSuggestionType::kAction:
      return SkColorSetARGB(0x20, 0xC0, 0x7D, 0xFF);
    case CommandSuggestionType::kNavigation:
      return SkColorSetARGB(0x20, 0x76, 0xDB, 0xC4);
    case CommandSuggestionType::kCalculator:
      return SkColorSetARGB(0x20, 0xFF, 0x9A, 0x74);
    case CommandSuggestionType::kUnitConversion:
      return SkColorSetARGB(0x20, 0xF1, 0x8D, 0x8D);
    case CommandSuggestionType::kArchivedTab:
      return SkColorSetARGB(0x20, 0x91, 0xA5, 0xC1);
    case CommandSuggestionType::kClosedTab:
      return SkColorSetARGB(0x20, 0xB7, 0x97, 0xE4);
  }
  return SkColorSetARGB(0x20, 0xA8, 0xB3, 0xC7);
}

}  // namespace maho
