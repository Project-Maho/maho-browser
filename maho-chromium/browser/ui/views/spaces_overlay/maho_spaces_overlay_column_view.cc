// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_spaces_overlay_column_view.h"

#include <memory>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho_spaces_overlay_tab_row_view.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"

namespace maho {

namespace {

constexpr int kColumnWidthDp = 184;
constexpr int kColumnHeightDp = 560;
constexpr int kColumnCornerRadiusDp = 16;
constexpr int kColumnInsetDp = 10;
constexpr int kColumnSpacingDp = 8;
constexpr int kTabListSpacingDp = 8;
constexpr int kTabSectionSpacingDp = 3;
constexpr int kTabSectionLabelTopInsetDp = 3;
constexpr int kTabSectionLabelBottomInsetDp = 1;
constexpr int kHeaderBadgeSizeDp = 10;
constexpr int kHeaderBadgeCornerRadiusDp = 5;
constexpr int kHeaderSpacingDp = 8;
constexpr int kHeaderTextSpacingDp = 3;
constexpr int kMetaSpacingDp = 4;
constexpr int kActionButtonCornerRadiusDp = 10;

SkColor ParseSpaceAccentColor(const std::string& color_string,
                              SkColor fallback_color) {
  std::string hex = color_string;
  if (!hex.empty() && hex.front() == '#') {
    hex.erase(hex.begin());
  }
  if (hex.size() != 6) {
    return fallback_color;
  }

  uint32_t value = 0;
  if (!base::HexStringToUInt(hex, &value)) {
    return fallback_color;
  }

  return SkColorSetRGB((value >> 16) & 0xFF, (value >> 8) & 0xFF,
                       value & 0xFF);
}

SkColor SpaceThemeAccentColor(const SpaceBoardColumn& column,
                              SkColor fallback_color) {
  return ParseSpaceAccentColor(
      GetMahoSpaceDisplayPrimaryColor(column.theme, column.color),
      fallback_color);
}

SkColor SpaceThemeSecondaryColor(const SpaceBoardColumn& column,
                                 SkColor fallback_color) {
  return ParseSpaceAccentColor(
      GetMahoSpaceDisplaySecondaryColor(
          column.theme, GetMahoSpaceDisplayPrimaryColor(column.theme, column.color)),
      fallback_color);
}

std::u16string TabCountText(int count) {
  return base::UTF8ToUTF16(base::NumberToString(count)) +
         (count == 1 ? u" tab" : u" tabs");
}

struct TabSectionSpec {
  std::u16string title;
  std::vector<const SpaceBoardTabItem*> tabs;
};

std::vector<TabSectionSpec> BuildTabSections(const SpaceBoardColumn& column) {
  std::vector<const SpaceBoardTabItem*> pinned_tabs;
  std::vector<const SpaceBoardTabItem*> regular_tabs;

  for (const SpaceBoardTabItem& tab : column.tabs) {
    if (tab.is_pinned) {
      pinned_tabs.push_back(&tab);
      continue;
    }
    regular_tabs.push_back(&tab);
  }

  std::vector<TabSectionSpec> sections;
  if (!pinned_tabs.empty()) {
    sections.push_back(TabSectionSpec{u"BOOKMARKS", std::move(pinned_tabs)});
  }
  if (!regular_tabs.empty()) {
    sections.push_back(TabSectionSpec{u"TABS", std::move(regular_tabs)});
  }
  return sections;
}

std::u16string SectionAccessibleName(const std::u16string& title) {
  if (title == u"BOOKMARKS") {
    return u"Bookmarks";
  }
  if (title == u"TABS") {
    return u"Tabs";
  }
  return title;
}

}  // namespace

BEGIN_METADATA(MahoSpacesOverlayColumnView)
END_METADATA

MahoSpacesOverlayColumnView::MahoSpacesOverlayColumnView(
    const SpaceBoardColumn& column,
    bool is_active,
    SpaceSelectedCallback callback)
    : column_(column),
      is_active_(is_active),
      space_selected_callback_(std::move(callback)) {
  SetPreferredSize(gfx::Size(kColumnWidthDp, kColumnHeightDp));
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(kColumnInsetDp, kColumnInsetDp), kColumnSpacingDp));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  SetBackground(views::CreateRoundedRectBackground(kMahoColorCardBackground,
                                                   kColumnCornerRadiusDp));
  SetBorder(views::CreateRoundedRectBorder(1, kColumnCornerRadiusDp,
                                           kMahoColorCardBorder));

  auto* header_row = AddChildView(std::make_unique<views::View>());
  auto* header_layout = header_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kHeaderSpacingDp));
  header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStart);

  badge_container_ = header_row->AddChildView(std::make_unique<views::View>());
  badge_container_->SetPreferredSize(
      gfx::Size(kHeaderBadgeSizeDp, kHeaderBadgeSizeDp));
  badge_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal));
  static_cast<views::BoxLayout*>(badge_container_->GetLayoutManager())
      ->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  static_cast<views::BoxLayout*>(badge_container_->GetLayoutManager())
      ->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  badge_label_ = badge_container_->AddChildView(std::make_unique<views::Label>(
      std::u16string(), views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
  badge_label_->SetAutoColorReadabilityEnabled(false);
  badge_label_->SetSubpixelRenderingEnabled(false);
  badge_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  badge_label_->SetVisible(false);

  auto* heading_stack = header_row->AddChildView(std::make_unique<views::View>());
  auto* heading_layout = heading_stack->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), kHeaderTextSpacingDp));
  heading_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  header_layout->SetFlexForView(heading_stack, 1);

  title_label_ = heading_stack->AddChildView(std::make_unique<views::Label>(
      column_.name, views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_4_MEDIUM));
  title_label_->SetAutoColorReadabilityEnabled(false);
  title_label_->SetSubpixelRenderingEnabled(false);
  title_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_label_->SetElideBehavior(gfx::ELIDE_TAIL);

  auto* meta_row = heading_stack->AddChildView(std::make_unique<views::View>());
  auto* meta_layout = meta_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kMetaSpacingDp));
  meta_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  subtitle_label_ = meta_row->AddChildView(std::make_unique<views::Label>(
      TabCountText(column_.tab_count), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_5));
  subtitle_label_->SetAutoColorReadabilityEnabled(false);
  subtitle_label_->SetSubpixelRenderingEnabled(false);
  subtitle_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);

  auto* current_chip = meta_row->AddChildView(std::make_unique<views::View>());
  current_chip->SetVisible(is_active_);
  current_chip->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  current_chip->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal));
  current_chip_label_ = current_chip->AddChildView(std::make_unique<views::Label>(
      u"Current", views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
  current_chip_label_->SetAutoColorReadabilityEnabled(false);
  current_chip_label_->SetSubpixelRenderingEnabled(false);
  current_chip_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);

  action_button_ = header_row->AddChildView(
      std::make_unique<views::MdTextButton>(
          base::BindRepeating(&MahoSpacesOverlayColumnView::HandleOpenPressed,
                              base::Unretained(this)),
          is_active_ ? u"Open" : u"Switch"));
  action_button_->SetStyle(ui::ButtonStyle::kText);
  action_button_->SetCornerRadius(kActionButtonCornerRadiusDp);
  action_button_->SetMinSize(gfx::Size(0, 20));
  action_button_->SetMaxSize(gfx::Size(0, 20));
  action_button_->SetRequestFocusOnPress(false);
  action_button_->SetAccessibleName(u"Switch to " + column_.name);

  tab_scroll_view_ = AddChildView(std::make_unique<views::ScrollView>());
  tab_scroll_view_->SetBackgroundColor(std::nullopt);
  tab_scroll_view_->SetDrawOverflowIndicator(false);
  tab_scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  tab_scroll_view_->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  layout->SetFlexForView(tab_scroll_view_, 1);

  tab_list_ = tab_scroll_view_->SetContents(std::make_unique<views::View>());
  auto* tab_list_layout = tab_list_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(),
      kTabListSpacingDp));
  tab_list_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  tab_list_->GetViewAccessibility().SetRole(ax::mojom::Role::kList);
  tab_list_->GetViewAccessibility().SetName(column_.name + u" tabs");

  if (column_.tabs.empty()) {
    empty_state_label_ = tab_list_->AddChildView(std::make_unique<views::Label>(
        u"No tabs parked here yet.", views::style::CONTEXT_LABEL,
        views::style::STYLE_BODY_4));
    empty_state_label_->SetAutoColorReadabilityEnabled(false);
    empty_state_label_->SetSubpixelRenderingEnabled(false);
    empty_state_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    empty_state_label_->SetMultiLine(true);
    empty_state_label_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(10, 0)));
  } else {
    for (TabSectionSpec& section : BuildTabSections(column_)) {
      auto* section_view = tab_list_->AddChildView(std::make_unique<views::View>());
      auto* section_layout =
          section_view->SetLayoutManager(std::make_unique<views::BoxLayout>(
              views::BoxLayout::Orientation::kVertical, gfx::Insets(),
              kTabSectionSpacingDp));
      section_layout->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kStretch);
      section_view->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
      section_view->GetViewAccessibility().SetName(
          SectionAccessibleName(section.title));

      auto* section_label = section_view->AddChildView(
          std::make_unique<views::Label>(section.title,
                                         views::style::CONTEXT_LABEL,
                                         views::style::STYLE_BODY_5_MEDIUM));
      section_label->SetAutoColorReadabilityEnabled(false);
      section_label->SetSubpixelRenderingEnabled(false);
      section_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      section_label->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(
          kTabSectionLabelTopInsetDp, 0, kTabSectionLabelBottomInsetDp, 0)));
      section_labels_.push_back(section_label);

      for (const SpaceBoardTabItem* tab : section.tabs) {
        section_view->AddChildView(
            std::make_unique<MahoSpacesOverlayTabRowView>(*tab));
      }
    }
  }

  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  GetViewAccessibility().SetName(column_.name);

  UpdateColors();
}

MahoSpacesOverlayColumnView::~MahoSpacesOverlayColumnView() = default;

bool MahoSpacesOverlayColumnView::IsCurrentChipVisibleForTesting() const {
  return current_chip_label_ && current_chip_label_->parent() &&
         current_chip_label_->parent()->GetVisible();
}

std::u16string MahoSpacesOverlayColumnView::GetTitleTextForTesting() const {
  return title_label_ ? std::u16string(title_label_->GetText()) : std::u16string();
}

std::u16string MahoSpacesOverlayColumnView::GetActionButtonTextForTesting()
    const {
  return action_button_ ? std::u16string(action_button_->GetText()) : std::u16string();
}

std::vector<std::u16string>
MahoSpacesOverlayColumnView::GetSectionTitlesForTesting() const {
  std::vector<std::u16string> titles;
  titles.reserve(section_labels_.size());
  for (const views::Label* section_label : section_labels_) {
    titles.push_back(section_label ? std::u16string(section_label->GetText())
                                   : std::u16string());
  }
  return titles;
}

void MahoSpacesOverlayColumnView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateColors();
}

void MahoSpacesOverlayColumnView::UpdateColors() {
  const ui::ColorProvider* color_provider = GetColorProvider();
  if (!color_provider) {
    return;
  }

  const SkColor fallback_accent = color_provider->GetColor(kMahoColorAccentBlue);
  const SkColor accent = SpaceThemeAccentColor(column_, fallback_accent);
  const SkColor secondary = SpaceThemeSecondaryColor(column_, accent);
  const SkColor column_fill = SkColorSetA(secondary, is_active_ ? 0x2F : 0x20);
  const SkColor column_border = is_active_
                                    ? SkColorSetA(accent, 0x6A)
                                    : SkColorSetA(color_provider->GetColor(
                                                      kMahoColorCardBorder),
                                                  0x86);
  const SkColor accent_dot = SkColorSetA(accent, 0xD0);

  SetBackground(
      views::CreateRoundedRectBackground(column_fill, kColumnCornerRadiusDp));
  SetBorder(views::CreateRoundedRectBorder(1, kColumnCornerRadiusDp,
                                           column_border));
  badge_container_->SetBackground(
      views::CreateRoundedRectBackground(accent_dot, kHeaderBadgeCornerRadiusDp));
  badge_container_->SetBorder(nullptr);
  title_label_->SetEnabledColor(kMahoColorPrimaryText);
  subtitle_label_->SetEnabledColor(kMahoColorTertiaryText);

  const SkColor section_label_color =
      SkColorSetA(color_provider->GetColor(kMahoColorTertiaryText), 0xCC);
  for (views::Label* section_label : section_labels_) {
    section_label->SetEnabledColor(section_label_color);
  }

  if (current_chip_label_ && current_chip_label_->parent()) {
    current_chip_label_->parent()->SetBackground(nullptr);
    current_chip_label_->parent()->SetBorder(nullptr);
    current_chip_label_->SetEnabledColor(accent);
  }

  if (action_button_) {
    action_button_->SetBackground(nullptr);
    action_button_->SetBorder(nullptr);
    action_button_->SetEnabledTextColors(accent);
  }

  if (empty_state_label_) {
    empty_state_label_->SetEnabledColor(kMahoColorSecondaryText);
  }
}

void MahoSpacesOverlayColumnView::HandleOpenPressed() {
  if (space_selected_callback_) {
    space_selected_callback_.Run(column_.space_id);
  }
}

}  // namespace maho
