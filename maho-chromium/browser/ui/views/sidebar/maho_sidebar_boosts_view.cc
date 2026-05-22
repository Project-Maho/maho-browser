// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_sidebar_boosts_view.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "components/vector_icons/vector_icons.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_boosts_data.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_tile_helpers.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/font.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/style/typography.h"

#include "third_party/skia/include/core/SkColor.h"

namespace maho {

namespace {

constexpr int kPaneSpacingDp = 0;
constexpr int kPaneHorizontalInsetDp = 0;
constexpr int kHeaderHorizontalInsetDp = 16;
constexpr int kHeaderTopInsetDp = 16;
constexpr int kHeaderBottomInsetDp = 12;
constexpr int kHeaderSearchHeightDp = 36;
constexpr int kHeaderSearchCornerRadiusDp = 8;
constexpr int kHeaderSearchHorizontalInsetDp = 12;
constexpr int kHeaderSearchIconSpacingDp = 8;
constexpr int kHeaderSearchIconSizeDp = 14;
constexpr int kHeaderSummaryTopInsetDp = 8;
constexpr int kListBottomInsetDp = 10;
constexpr int kListHorizontalInsetDp = 10;
constexpr int kRowSpacingDp = 2;
constexpr int kRowPaddingDp = 10;
constexpr int kRowCornerRadiusDp = 10;
constexpr int kBadgeSizeDp = 28;
constexpr int kBadgeCornerRadiusDp = 8;
constexpr int kEmptyStateIconSizeDp =
    sidebar_layout::kArchiveEmptyStateIconSizeDp;
constexpr int kEmptyStateItemSpacingDp =
    sidebar_layout::kArchiveEmptyStateItemSpacingDp;
constexpr int kEmptyStateTextMaxWidthDp = 188;
constexpr int kEmptyStateHorizontalPaddingDp = 24;

std::u16string BoostCountText(size_t count) {
  return base::UTF8ToUTF16(std::to_string(count)) +
         (count == 1 ? u" boost" : u" boosts");
}

std::u16string HeaderSummaryText(size_t count) {
  if (count == 0) {
    return u"Ready for your next boost";
  }
  return BoostCountText(count) + u" in your library";
}

MahoSidebarTilePalette BadgeAccentPaletteForBoost(const BoostItem& item) {
  const std::string& seed = item.id.empty() ? item.url_pattern : item.id;
  return MahoSidebarTilePaletteForSeed(seed);
}

std::u16string BoostTileLetter(const BoostItem& item) {
  return MahoSidebarTileLetterForText(BoostTitleFromUrlPattern(item.url_pattern),
                                      u"B");
}

class BoostEmptyStateView : public views::View {
  METADATA_HEADER(BoostEmptyStateView, views::View)

 public:
  BoostEmptyStateView() {
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    GetViewAccessibility().SetName(u"Boosts empty state, no saved boosts yet");

    icon_ = AddChildView(std::make_unique<views::ImageView>());
    icon_->GetViewAccessibility().SetIsIgnored(true);
    icon_->SetPreferredSize(
        gfx::Size(kEmptyStateIconSizeDp, kEmptyStateIconSizeDp));
    icon_->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kScienceIcon, kMahoColorSecondaryText,
        kEmptyStateIconSizeDp));

    title_label_ = AddChildView(
        std::make_unique<views::Label>(u"No boosts yet"));
    title_label_->SetTextStyle(views::style::STYLE_BODY_3_MEDIUM);
    title_label_->SetEnabledColor(kMahoColorPrimaryText);
    title_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    title_label_->SetAutoColorReadabilityEnabled(false);
    title_label_->GetViewAccessibility().SetIsIgnored(true);

    subtitle_label_ = AddChildView(std::make_unique<views::Label>(
        u"Custom site boosts you save will show up here so you can toggle them on or off later."));
    subtitle_label_->SetTextStyle(views::style::STYLE_BODY_4);
    subtitle_label_->SetEnabledColor(kMahoColorSecondaryText);
    subtitle_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    subtitle_label_->SetAutoColorReadabilityEnabled(false);
    subtitle_label_->SetMultiLine(true);
    subtitle_label_->GetViewAccessibility().SetIsIgnored(true);
  }

  void Layout(PassKey) override {
    LayoutSuperclass<views::View>(this);
    if (!icon_ || !title_label_ || !subtitle_label_) {
      return;
    }

    const gfx::Size icon_size = icon_->GetPreferredSize();
    const gfx::Size title_size = title_label_->GetPreferredSize();
    const int subtitle_width = std::max(
        0, std::min(kEmptyStateTextMaxWidthDp,
                    width() - (kEmptyStateHorizontalPaddingDp * 2)));
    const int subtitle_height =
        subtitle_width > 0 ? subtitle_label_->GetHeightForWidth(subtitle_width)
                           : subtitle_label_->GetPreferredSize().height();
    const int content_width = std::max(
        {icon_size.width(), title_size.width(), subtitle_width});
    const int content_height = icon_size.height() + kEmptyStateItemSpacingDp +
                               title_size.height() + kEmptyStateItemSpacingDp +
                               subtitle_height;
    const int content_top = std::max(0, (height() - content_height) / 2);
    const int content_left = (width() - content_width) / 2;

    icon_->SetBounds(content_left + ((content_width - icon_size.width()) / 2),
                     content_top, icon_size.width(), icon_size.height());
    title_label_->SetBounds(
        content_left + ((content_width - title_size.width()) / 2),
        icon_->bounds().bottom() + kEmptyStateItemSpacingDp,
        title_size.width(), title_size.height());
    subtitle_label_->SetBounds(
        content_left + ((content_width - subtitle_width) / 2),
        title_label_->bounds().bottom() + kEmptyStateItemSpacingDp,
        subtitle_width, subtitle_height);
  }

 private:
  raw_ptr<views::ImageView> icon_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> subtitle_label_ = nullptr;
};

BEGIN_METADATA(BoostEmptyStateView)
END_METADATA

std::u16string ToggleAccessibleName(const BoostItem& item) {
  const std::u16string title = BoostTitleFromUrlPattern(item.url_pattern);
  return item.enabled ? u"Disable boost for " + title
                      : u"Enable boost for " + title;
}

class BoostRowView : public views::Button {
  METADATA_HEADER(BoostRowView, views::Button)

 public:
  using ToggleCallback =
      base::RepeatingCallback<void(const std::string&, bool)>;

  BoostRowView(const BoostItem& item, ToggleCallback toggle_callback)
      : views::Button(base::BindRepeating(&BoostRowView::HandleRowPressed,
                                           base::Unretained(this))),
        boost_id_(item.id),
        enabled_(item.enabled),
        badge_accent_palette_(BadgeAccentPaletteForBoost(item)),
        toggle_callback_(std::move(toggle_callback)) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(kRowPaddingDp, kRowPaddingDp, kRowPaddingDp,
                          kRowPaddingDp),
        10));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    SetAccessibleName(BoostTitleFromUrlPattern(item.url_pattern));
    SetTooltipText(ToggleAccessibleName(item));
    SetRequestFocusOnPress(false);
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetPaintToLayer();
    GetViewAccessibility().SetRole(ax::mojom::Role::kSwitch);
    GetViewAccessibility().SetDescription(
        BoostSubtitleFromUrlPattern(item.url_pattern));

    views::FocusRing::Install(this);
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                  kRowCornerRadiusDp);

    badge_ = AddChildView(std::make_unique<views::View>());
    badge_->SetPreferredSize(gfx::Size(kBadgeSizeDp, kBadgeSizeDp));
    badge_->SetBackground(views::CreateRoundedRectBackground(
        badge_accent_palette_.background, kBadgeCornerRadiusDp));
    badge_->SetBorder(views::CreateRoundedRectBorder(
        1, kBadgeCornerRadiusDp, kMahoColorLibraryGlyphBorder));
    badge_->SetLayoutManager(std::make_unique<views::FillLayout>());
    badge_->GetViewAccessibility().SetIsIgnored(true);

    badge_label_ = badge_->AddChildView(std::make_unique<views::Label>(
        BoostTileLetter(item)));
    badge_label_->SetTextStyle(views::style::STYLE_BODY_5_BOLD);
    badge_label_->SetFontList(badge_label_->font_list().Derive(
        1, gfx::Font::NORMAL, gfx::Font::Weight::BOLD));
    badge_label_->SetEnabledColor(badge_accent_palette_.foreground);
    badge_label_->SetAutoColorReadabilityEnabled(false);
    badge_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    badge_label_->GetViewAccessibility().SetIsIgnored(true);

    auto* text_container = AddChildView(std::make_unique<views::View>());
    auto* text_layout = text_container->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
    text_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    layout->SetFlexForView(text_container, 1);

    title_label_ = text_container->AddChildView(std::make_unique<views::Label>(
        BoostTitleFromUrlPattern(item.url_pattern)));
    title_label_->SetTextStyle(views::style::STYLE_BODY_4_MEDIUM);
    title_label_->SetEnabledColor(kMahoColorPrimaryText);
    title_label_->SetAutoColorReadabilityEnabled(false);
    title_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_label_->SetMultiLine(false);
    title_label_->SetElideBehavior(gfx::ELIDE_TAIL);

    subtitle_label_ = text_container->AddChildView(
        std::make_unique<views::Label>(
            BoostSubtitleFromUrlPattern(item.url_pattern)));
    subtitle_label_->SetTextStyle(views::style::STYLE_BODY_5);
    subtitle_label_->SetEnabledColor(kMahoColorSecondaryText);
    subtitle_label_->SetAutoColorReadabilityEnabled(false);
    subtitle_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    subtitle_label_->SetMultiLine(false);
    subtitle_label_->SetElideBehavior(gfx::ELIDE_TAIL);

    UpdateAppearance();
  }

  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    UpdateAppearance();
  }

  void OnFocus() override {
    views::Button::OnFocus();
    UpdateAppearance();
  }

  void OnBlur() override {
    views::Button::OnBlur();
    UpdateAppearance();
  }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    UpdateAppearance();
  }

  bool MatchesBoostId(const std::string& boost_id) const {
    return boost_id_ == boost_id;
  }

  views::Button* action_button_for_testing() { return this; }

 private:
  void HandleRowPressed(const ui::Event& event) {
    if (!toggle_callback_) {
      return;
    }
    toggle_callback_.Run(boost_id_, !enabled_);
  }

  void UpdateAppearance() {
    const bool selected = HasFocus() || GetState() == STATE_PRESSED;
    const bool hovered = GetState() == STATE_HOVERED;
    GetViewAccessibility().SetCheckedState(
        enabled_ ? ax::mojom::CheckedState::kTrue
                 : ax::mojom::CheckedState::kFalse);
    SetBackground(views::CreateRoundedRectBackground(
        selected ? kMahoColorLibraryRowSelected
                 : hovered ? kMahoColorLibraryRowHover
                           : kMahoColorLibraryListRowBackground,
        kRowCornerRadiusDp));
    SetBorder(views::CreateRoundedRectBorder(1, kRowCornerRadiusDp,
                                             kMahoColorLibraryGlyphBorder));

    if (badge_) {
      if (enabled_) {
        badge_->SetBackground(views::CreateRoundedRectBackground(
            badge_accent_palette_.background, kBadgeCornerRadiusDp));
      } else {
        badge_->SetBackground(views::CreateRoundedRectBackground(
            kMahoColorLibraryListRowBackground, kBadgeCornerRadiusDp));
      }
      badge_->SetBorder(views::CreateRoundedRectBorder(
          1, kBadgeCornerRadiusDp,
          kMahoColorLibraryGlyphBorder));
    }
    if (badge_label_) {
      badge_label_->SetEnabledColor(enabled_ ? badge_accent_palette_.foreground
                                             : kMahoColorSecondaryText);
    }
    if (title_label_) {
      title_label_->SetEnabledColor(enabled_ ? kMahoColorPrimaryText
                                             : kMahoColorSecondaryText);
    }
    if (subtitle_label_) {
      subtitle_label_->SetEnabledColor(kMahoColorSecondaryText);
    }
    if (layer()) {
      layer()->SetOpacity(enabled_ ? 1.0f : 0.72f);
    }
  }

  std::string boost_id_;
  bool enabled_ = false;
  const MahoSidebarTilePalette badge_accent_palette_;
  ToggleCallback toggle_callback_;
  raw_ptr<views::View> badge_ = nullptr;
  raw_ptr<views::Label> badge_label_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> subtitle_label_ = nullptr;
};

BEGIN_METADATA(BoostRowView)
END_METADATA

}  // namespace

BEGIN_METADATA(MahoSidebarBoostsView)
END_METADATA

MahoSidebarBoostsView::MahoSidebarBoostsView() {
  BuildUi();
}

MahoSidebarBoostsView::~MahoSidebarBoostsView() = default;

views::View* MahoSidebarBoostsView::boost_row_for_boost_id_for_testing(
    const std::string& boost_id) {
  if (!list_container_) {
    return nullptr;
  }

  for (const auto& child : list_container_->children()) {
    auto* row = static_cast<BoostRowView*>(child.get());
    if (row->MatchesBoostId(boost_id)) {
      return row;
    }
  }

  return nullptr;
}

views::Button* MahoSidebarBoostsView::button_for_action_for_testing(
    size_t index) {
  if (!list_container_ || index >= list_container_->children().size()) {
    return nullptr;
  }
  auto* row =
      static_cast<BoostRowView*>(list_container_->children()[index].get());
  return row->action_button_for_testing();
}

void MahoSidebarBoostsView::BuildUi() {
  auto* root_layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(0, kPaneHorizontalInsetDp, 0, kPaneHorizontalInsetDp),
      kPaneSpacingDp));
  root_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  GetViewAccessibility().SetName(u"Boosts panel");

   auto* header_strip = AddChildView(std::make_unique<views::View>());
   auto* header_layout = header_strip->SetLayoutManager(
       std::make_unique<views::BoxLayout>(
           views::BoxLayout::Orientation::kVertical,
           gfx::Insets::TLBR(kHeaderTopInsetDp, kHeaderHorizontalInsetDp,
                             kHeaderBottomInsetDp, kHeaderHorizontalInsetDp),
           0));
   header_layout->set_cross_axis_alignment(
       views::BoxLayout::CrossAxisAlignment::kStretch);

   auto* search_shell = header_strip->AddChildView(std::make_unique<views::View>());
   search_shell->SetPreferredSize(gfx::Size(0, kHeaderSearchHeightDp));
   search_shell->SetBackground(views::CreateRoundedRectBackground(
       kMahoColorLibrarySearchPillBackground, kHeaderSearchCornerRadiusDp));
   search_shell->SetBorder(views::CreateRoundedRectBorder(
       1, kHeaderSearchCornerRadiusDp, kMahoColorLibraryGlyphBorder));
   auto* search_layout = search_shell->SetLayoutManager(
       std::make_unique<views::BoxLayout>(
           views::BoxLayout::Orientation::kHorizontal,
           gfx::Insets::VH(0, kHeaderSearchHorizontalInsetDp),
           kHeaderSearchIconSpacingDp));
   search_layout->set_cross_axis_alignment(
       views::BoxLayout::CrossAxisAlignment::kCenter);

   auto* search_icon =
       search_shell->AddChildView(std::make_unique<views::ImageView>());
   search_icon->GetViewAccessibility().SetIsIgnored(true);
   search_icon->SetPreferredSize(
       gfx::Size(kHeaderSearchIconSizeDp, kHeaderSearchIconSizeDp));
   search_icon->SetImage(ui::ImageModel::FromVectorIcon(
       vector_icons::kSearchIcon, kMahoColorSecondaryText,
       kHeaderSearchIconSizeDp));

   auto* placeholder = search_shell->AddChildView(
       std::make_unique<views::Label>(u"Search boosts..."));
   placeholder->SetTextStyle(views::style::STYLE_BODY_5);
   placeholder->SetEnabledColor(kMahoColorSecondaryText);
   placeholder->SetAutoColorReadabilityEnabled(false);
   placeholder->SetHorizontalAlignment(gfx::ALIGN_LEFT);
   placeholder->GetViewAccessibility().SetIsIgnored(true);
   search_shell->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
   search_shell->GetViewAccessibility().SetName(u"Search boosts header");

   header_summary_label_ = header_strip->AddChildView(
       std::make_unique<views::Label>(HeaderSummaryText(0)));
   header_summary_label_->SetTextStyle(views::style::STYLE_BODY_5);
   header_summary_label_->SetEnabledColor(kMahoColorLibrarySectionHeaderText);
   header_summary_label_->SetAutoColorReadabilityEnabled(false);
   header_summary_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
   header_summary_label_->SetBorder(views::CreateEmptyBorder(
       gfx::Insets::TLBR(kHeaderSummaryTopInsetDp, 2, 0, 0)));
   header_summary_label_->GetViewAccessibility().SetIsIgnored(true);
   header_summary_label_->SetVisible(false);

   body_container_ = AddChildView(std::make_unique<views::View>());
   body_container_->SetLayoutManager(std::make_unique<views::FillLayout>());
   root_layout->SetFlexForView(body_container_, 1, true);

  scroll_view_ = body_container_->AddChildView(std::make_unique<views::ScrollView>());
  scroll_view_->SetBackgroundColor(std::nullopt);
  scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_view_->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  scroll_view_->GetViewAccessibility().SetName(u"Boosts results container");

   list_container_ = scroll_view_->SetContents(std::make_unique<views::View>());
   auto* list_layout = list_container_->SetLayoutManager(
       std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                          gfx::Insets::TLBR(0, kListHorizontalInsetDp,
                                                            kListBottomInsetDp,
                                                            kListHorizontalInsetDp),
                                          kRowSpacingDp));
   list_layout->set_cross_axis_alignment(
       views::BoxLayout::CrossAxisAlignment::kStretch);
  list_container_->GetViewAccessibility().SetRole(ax::mojom::Role::kList);
  list_container_->GetViewAccessibility().SetName(u"Boosts results");

  empty_state_view_ = body_container_->AddChildView(
      std::make_unique<BoostEmptyStateView>());

  empty_state_view_->SetVisible(true);
  scroll_view_->SetVisible(false);
}

void MahoSidebarBoostsView::ReloadBoosts() {
  std::vector<BoostItem> boosts = ParseBoosts();

  list_container_->RemoveAllChildViews();
  if (header_summary_label_) {
    header_summary_label_->SetVisible(!boosts.empty());
    if (!boosts.empty()) {
      header_summary_label_->SetText(HeaderSummaryText(boosts.size()));
    }
  }
  if (boosts.empty()) {
    empty_state_view_->SetVisible(true);
    scroll_view_->SetVisible(false);
    scroll_view_->GetViewAccessibility().SetValue(u"No boosts");
    return;
  }

  const size_t boost_count = boosts.size();
  for (const auto& item : boosts) {
    list_container_->AddChildView(std::make_unique<BoostRowView>(
        item, base::BindRepeating(&MahoSidebarBoostsView::ToggleBoost,
                                  base::Unretained(this))));
  }

  empty_state_view_->SetVisible(false);
  scroll_view_->SetVisible(true);
  scroll_view_->GetViewAccessibility().SetValue(BoostCountText(boost_count));
}

void MahoSidebarBoostsView::ToggleBoost(const std::string& boost_id,
                                        bool new_enabled) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }

  base::DictValue event_dict;
  event_dict.Set("type", "UpdateBoost");

  base::DictValue payload;
  payload.Set("id", boost_id);
  payload.Set("enabled", new_enabled);
  event_dict.Set("payload", std::move(payload));

  std::string event_json;
  base::JSONWriter::Write(event_dict, &event_json);
  maho::core::HandleEvent(core, event_json.c_str());

  ReloadBoosts();
}

}  // namespace maho
