// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_result_row_view.h"

#include <string>
#include <utility>

#include "base/strings/utf_string_conversions.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/styled_label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/style/typography.h"

namespace maho {

namespace {

constexpr int kRowHeightDp = 44;
constexpr int kRowCornerRadiusDp = 10;
constexpr int kRowPaddingHDp = 12;
constexpr int kRowPaddingVDp = 6;
constexpr int kBadgeCornerRadiusDp = 6;
constexpr int kBadgePaddingHDp = 8;
constexpr int kBadgePaddingVDp = 2;
constexpr int kBadgeTextSpacingDp = 10;
constexpr int kTitleSubtitleSpacingDp = 2;

}  // namespace

BEGIN_METADATA(MahoCommandResultRowView)
END_METADATA

MahoCommandResultRowView::MahoCommandResultRowView(
    const CommandSuggestion& suggestion) {
  SetPreferredSize(gfx::Size(0, kRowHeightDp));

  auto* row_layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(kRowPaddingVDp, kRowPaddingHDp), kBadgeTextSpacingDp));

  badge_ = AddChildView(std::make_unique<views::Label>(
      BadgeText(suggestion.type), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_5));
  badge_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  badge_->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::VH(kBadgePaddingVDp, kBadgePaddingHDp)));
  badge_->SetBackground(views::CreateRoundedRectBackground(
      BadgeColor(suggestion.type), kBadgeCornerRadiusDp));
  badge_->SetEnabledColor(SK_ColorWHITE);

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
  title_label_->SetDefaultEnabledColorId(ui::kColorSysOnSurface);

  for (const auto& [start, end] : suggestion.match_ranges) {
    if (start >= 0 && end > start &&
        static_cast<size_t>(end) <= title_text.size()) {
      views::StyledLabel::RangeStyleInfo bold_style;
      bold_style.text_style = views::style::STYLE_BODY_4_BOLD;
      title_label_->AddStyleRange(gfx::Range(start, end), bold_style);
    }
  }

  if (!suggestion.subtitle.empty()) {
    subtitle_label_ =
        text_container->AddChildView(std::make_unique<views::Label>(
            base::UTF8ToUTF16(suggestion.subtitle),
            views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
    subtitle_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    subtitle_label_->SetEnabledColorId(ui::kColorSysOnSurfaceSubtle);
    subtitle_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  }
}

MahoCommandResultRowView::~MahoCommandResultRowView() = default;

void MahoCommandResultRowView::SetSelected(bool selected) {
  if (selected_ == selected)
    return;
  selected_ = selected;
  if (selected) {
    SetBackground(views::CreateRoundedRectBackground(
        ui::kColorSysSurface4, kRowCornerRadiusDp));
  } else {
    SetBackground(nullptr);
  }
}

// static
std::u16string MahoCommandResultRowView::BadgeText(
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
SkColor MahoCommandResultRowView::BadgeColor(CommandSuggestionType type) {
  switch (type) {
    case CommandSuggestionType::kTab:
      return SkColorSetRGB(0x42, 0x85, 0xF4);
    case CommandSuggestionType::kBookmark:
      return SkColorSetRGB(0xFB, 0xBC, 0x04);
    case CommandSuggestionType::kHistory:
      return SkColorSetRGB(0x9A, 0xA0, 0xA6);
    case CommandSuggestionType::kAction:
      return SkColorSetRGB(0xAB, 0x47, 0xBC);
    case CommandSuggestionType::kNavigation:
      return SkColorSetRGB(0x34, 0xA8, 0x53);
    case CommandSuggestionType::kCalculator:
      return SkColorSetRGB(0xFF, 0x63, 0x47);
    case CommandSuggestionType::kUnitConversion:
      return SkColorSetRGB(0xFF, 0x63, 0x47);
    case CommandSuggestionType::kArchivedTab:
      return SkColorSetRGB(0x78, 0x90, 0x9C);
    case CommandSuggestionType::kClosedTab:
      return SkColorSetRGB(0x78, 0x90, 0x9C);
  }
  return SkColorSetRGB(0x9A, 0xA0, 0xA6);
}

}  // namespace maho
