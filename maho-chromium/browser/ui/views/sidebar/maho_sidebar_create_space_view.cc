// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/theme/maho_space_theme_io.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/browser/ui/views/space_create/maho_space_theme_picker_dialog.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider_manager.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {

constexpr int kCreatePanelCornerRadius = 18;
constexpr int kHeroBadgeSize = 48;
constexpr int kHeroBadgeCornerRadius = 14;
constexpr int kCardCornerRadius = 18;
constexpr int kInputCornerRadius = 12;
constexpr int kInputHeight = 44;
constexpr int kSectionGlyphSize = 32;
constexpr int kSectionGlyphCornerRadius = 11;
constexpr int kBottomDotSize = 6;
constexpr int kBottomDotCornerRadius = 3;
constexpr int kThemePreviewDotSize = 14;
constexpr int kThemePreviewDotCornerRadius = 7;
constexpr char kDefaultProfileId[] = "Default";

struct ColorPreset {
  double hue;
  double saturation;
  double brightness;
};

#if __cplusplus >= 202002L
constexpr auto kColorPresets = std::to_array<ColorPreset>({
    {0.10, 0.15, 0.95},
    {0.92, 0.80, 0.90},
    {0.75, 0.80, 0.90},
    {0.00, 0.80, 0.90},
    {0.08, 0.80, 0.90},
    {0.15, 0.80, 0.90},
    {0.35, 0.80, 0.90},
    {0.58, 0.80, 0.90},
    {0.62, 0.30, 0.55},
});
#else
constexpr std::array<ColorPreset, 9> kColorPresets = {{
    {0.10, 0.15, 0.95},
    {0.92, 0.80, 0.90},
    {0.75, 0.80, 0.90},
    {0.00, 0.80, 0.90},
    {0.08, 0.80, 0.90},
    {0.15, 0.80, 0.90},
    {0.35, 0.80, 0.90},
    {0.58, 0.80, 0.90},
    {0.62, 0.30, 0.55},
}};
#endif

constexpr int kChooserBrightnessMin = 15;
constexpr int kChooserBrightnessMax = 100;

constexpr int kChooserGrainMin = 0;
constexpr int kChooserGrainMax = 100;

struct CreateResult {
  bool success = false;
  std::string space_id;
};

const gfx::Insets kCreateSurfaceInsets = gfx::Insets();
const gfx::Insets kContentInsets = gfx::Insets::TLBR(24, 24, 20, 24);
const gfx::Insets kHeroBadgeInsets = gfx::Insets::VH(0, 14);
const gfx::Insets kCardInsets = gfx::Insets::TLBR(14, 14, 14, 14);
const gfx::Insets kInputInsets = gfx::Insets::VH(0, 12);
const gfx::Insets kBottomRegionInsets = gfx::Insets::TLBR(14, 0, 0, 0);

MahoSidebarCreateSpaceThemeSelection DefaultThemeSelection() {
  return MahoSidebarCreateSpaceThemeSelection();
}

const ColorPreset& GetColorPresetForSelection(
    const MahoSidebarCreateSpaceThemeSelection& selection) {
  const int max_index = static_cast<int>(std::size(kColorPresets) - 1);
  const int clamped_index = std::clamp(selection.preset_index, 0, max_index);
  return kColorPresets.at(clamped_index);
}

int ClampBrightness(int brightness) {
  return std::clamp(brightness, kChooserBrightnessMin, kChooserBrightnessMax);
}

int ClampGrain(int grain) {
  return std::clamp(grain, kChooserGrainMin, kChooserGrainMax);
}

SkColor BuildSkColorForSelection(
    const MahoSidebarCreateSpaceThemeSelection& selection) {
  const ColorPreset& preset = GetColorPresetForSelection(selection);
  SkScalar hsv[3] = {
      static_cast<SkScalar>(preset.hue * 360.0),
      static_cast<SkScalar>(preset.saturation),
      static_cast<SkScalar>(ClampBrightness(selection.brightness) / 100.0)};
  return SkHSVToColor(hsv);
}

std::string BuildColorHexForSelection(
    const MahoSidebarCreateSpaceThemeSelection& selection) {
  const SkColor color = BuildSkColorForSelection(selection);
  std::ostringstream stream;
  stream << '#'
         << std::uppercase << std::hex << std::setfill('0')
         << std::setw(2) << static_cast<int>(SkColorGetR(color))
         << std::setw(2) << static_cast<int>(SkColorGetG(color))
         << std::setw(2) << static_cast<int>(SkColorGetB(color));
  return stream.str();
}

std::string BuildThemeJsonForSelection(
    const MahoSidebarCreateSpaceThemeSelection& selection) {
  const ColorPreset& preset = GetColorPresetForSelection(selection);
  base::DictValue color_dict;
  color_dict.Set("hue", preset.hue);
  color_dict.Set("saturation", preset.saturation);
  color_dict.Set("brightness", ClampBrightness(selection.brightness) / 100.0);
  color_dict.Set("grain", ClampGrain(selection.grain) / 100.0);

  base::DictValue theme_dict;
  theme_dict.Set("type", selection.mode == MahoSidebarCreateSpaceThemeMode::kSolid
                             ? "solid"
                             : "gradient");
  theme_dict.Set("color", std::move(color_dict));

  std::string theme_json;
  base::JSONWriter::Write(theme_dict, &theme_json);
  return theme_json;
}

MahoSidebarCreateSpaceThemeSelection BuildSelectionFromThemeJson(
    const std::string& theme_json) {
  MahoSidebarCreateSpaceThemeSelection selection = DefaultThemeSelection();
  if (theme_json.empty()) {
    return selection;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(theme_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return selection;
  }
  const auto& root = parsed->GetDict();

  if (const std::string* type = root.FindString("type")) {
    selection.mode = (*type == "solid")
                         ? MahoSidebarCreateSpaceThemeMode::kSolid
                         : MahoSidebarCreateSpaceThemeMode::kZen;
  }

  if (const auto* color = root.FindDict("color")) {
    if (auto brightness = color->FindDouble("brightness")) {
      selection.brightness = static_cast<int>(*brightness * 100.0);
    }
    if (auto grain = color->FindDouble("grain")) {
      selection.grain = static_cast<int>(*grain * 100.0);
    }
    auto hue = color->FindDouble("hue");
    auto saturation = color->FindDouble("saturation");
    if (hue && saturation) {
      double best_dist = 1e9;
      int best_index = selection.preset_index;
      for (int i = 0; i < static_cast<int>(std::size(kColorPresets)); ++i) {
        const ColorPreset& p = kColorPresets[i];
        const double d = std::abs(p.hue - *hue) +
                         std::abs(p.saturation - *saturation) * 0.5;
        if (d < best_dist) {
          best_dist = d;
          best_index = i;
        }
      }
      selection.preset_index = best_index;
    }
  }
  return selection;
}

std::u16string BuildThemeSummaryForSelection(
    const MahoSidebarCreateSpaceThemeSelection& selection) {
  const std::u16string mode_label =
      selection.mode == MahoSidebarCreateSpaceThemeMode::kSolid ? u"Solid"
                                                                : u"Zen";
  return mode_label + u" · " +
         base::UTF8ToUTF16(BuildColorHexForSelection(selection));
}

views::Label* AddLabel(views::View* parent,
                       const std::u16string& text,
                       views::style::TextStyle text_style,
                       ui::ColorId color_id,
                       bool multiline = false) {
  auto label = std::make_unique<views::Label>(text);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetAutoColorReadabilityEnabled(false);
  label->SetEnabledColor(color_id);
  label->SetTextStyle(text_style);
  label->SetMultiLine(multiline);
  label->SetElideBehavior(multiline ? gfx::NO_ELIDE : gfx::ELIDE_TAIL);
  label->SetSubpixelRenderingEnabled(false);
  return parent->AddChildView(std::move(label));
}

views::View* CreateGlyphBadge(views::View* parent, const std::u16string& glyph) {
  auto* badge = parent->AddChildView(std::make_unique<views::View>());
  badge->SetPreferredSize(gfx::Size(kSectionGlyphSize, kSectionGlyphSize));
  badge->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysSurface3, kSectionGlyphCornerRadius));
  badge->SetBorder(views::CreateRoundedRectBorder(
      1, kSectionGlyphCornerRadius, ui::kColorSysSurfaceVariant));
  auto* layout = badge->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);

  auto* label = AddLabel(badge, glyph, views::style::STYLE_BODY_4_MEDIUM,
                         ui::kColorSysOnSurfaceSubtle);
  label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  return badge;
}

views::View* CreateInfoCard(views::View* parent,
                            const std::u16string& badge_glyph,
                            const std::u16string& title,
                            const std::u16string& subtitle) {
  auto* card = parent->AddChildView(std::make_unique<views::View>());
  auto* layout = card->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, kCardInsets, 10));
  layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  card->SetBackground(
      views::CreateRoundedRectBackground(ui::kColorSysSurface1, kCardCornerRadius));
  card->SetBorder(views::CreateRoundedRectBorder(
      1, kCardCornerRadius, ui::kColorSysSurfaceVariant));

  CreateGlyphBadge(card, badge_glyph);

  auto* text_stack = card->AddChildView(std::make_unique<views::View>());
  auto* text_layout = text_stack->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  text_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  layout->SetFlexForView(text_stack, 1);

  AddLabel(text_stack, title, views::style::STYLE_BODY_3_MEDIUM,
           ui::kColorSysOnSurface);
  AddLabel(text_stack, subtitle, views::style::STYLE_BODY_5,
           ui::kColorSysOnSurfaceSubtle, true);

  auto* trailing = AddLabel(card, u"›", views::style::STYLE_BODY_3_MEDIUM,
                            ui::kColorSysOnSurfaceSubtle);
  trailing->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  return card;
}

CreateResult CreateSpaceOnPool(const std::string& name,
                               const MahoSidebarCreateSpaceThemeSelection&
                                   theme_selection) {
  CreateResult result;
  MahoCore* core = maho::GetCore();
  if (!core) {
    DLOG(WARNING) << "Sidebar create-space failed: Maho core unavailable";
    return result;
  }

  base::DictValue color_dict;
  color_dict.Set("color", BuildColorHexForSelection(theme_selection));
  std::string color_json;
  base::JSONWriter::Write(color_dict, &color_json);

  // The inline sidebar create-space flow always passes a concrete profile id.
  // A null FFI result here is therefore a backend/contract failure, not the
  // older nullptr-profile path captured in the original remediation plan.
  char* ffi_result = maho_core_create_space(core, name.c_str(),
                                            color_json.c_str(),
                                            kDefaultProfileId);
  if (!ffi_result) {
    DLOG(WARNING)
        << "Sidebar create-space failed: maho_core_create_space returned null"
        << " for profile_id='" << kDefaultProfileId << "'";
    return result;
  }

  std::optional<base::Value> parsed =
      base::JSONReader::Read(ffi_result, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    DLOG(WARNING)
        << "Sidebar create-space failed: create-space response was not a dict";
    maho_string_free(ffi_result);
    return result;
  }

  const std::string* parsed_id = parsed->GetDict().FindString("id");
  if (!parsed_id || parsed_id->empty()) {
    DLOG(WARNING)
        << "Sidebar create-space failed: create-space response missing id";
    maho_string_free(ffi_result);
    return result;
  }

  result.space_id = *parsed_id;
  const std::string theme_json = BuildThemeJsonForSelection(theme_selection);
  if (!theme_json.empty()) {
    maho_theme::ApplyThemeJsonToSpace(core, result.space_id, theme_json);
  }
  result.success = true;
  maho_string_free(ffi_result);
  return result;
}


}  // namespace


BEGIN_METADATA(MahoSidebarCreateSpaceView)
END_METADATA

MahoSidebarCreateSpaceView::MahoSidebarCreateSpaceView(Browser* browser)
    : browser_(browser) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, kCreateSurfaceInsets, 0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  scroll_view_ = AddChildView(std::make_unique<views::ScrollView>());
  scroll_view_->SetBackgroundColor(std::nullopt);
  scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_view_->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  layout->SetFlexForView(scroll_view_, 1, true);

  auto* content = scroll_view_->SetContents(std::make_unique<views::View>());
  auto* content_layout = content->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, kContentInsets, 12));
  content_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto* hero = content->AddChildView(std::make_unique<views::View>());
  auto* hero_layout = hero->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 8));
  hero_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto* hero_badge = hero->AddChildView(std::make_unique<views::View>());
  hero_badge->SetPreferredSize(gfx::Size(kHeroBadgeSize, kHeroBadgeSize));
  hero_badge->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysSurface3, kHeroBadgeCornerRadius));
  hero_badge->SetBorder(views::CreateRoundedRectBorder(
      1, kHeroBadgeCornerRadius, ui::kColorSysSurfaceVariant));
  hero_badge->SetProperty(views::kMarginsKey, kHeroBadgeInsets);
  auto* hero_badge_layout = hero_badge->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                         gfx::Insets(), 0));
  hero_badge_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  hero_badge_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* hero_badge_label = AddLabel(hero_badge, u"✦",
                                    views::style::STYLE_HEADLINE_5,
                                    ui::kColorSysOnSurfaceSubtle);
  hero_badge_label->SetHorizontalAlignment(gfx::ALIGN_CENTER);

  auto* title_label = AddLabel(hero, u"Create a new space",
                               views::style::STYLE_HEADLINE_4,
                               ui::kColorSysOnSurface, true);
  title_label->SetMaximumWidth(360);

  auto* subtitle_label = AddLabel(
      hero,
      u"Set up a focused home for tabs, notes, and future personalizations.",
      views::style::STYLE_BODY_5, ui::kColorSysOnSurfaceSubtle, true);
  subtitle_label->SetMaximumWidth(360);

  auto* card_stack = content->AddChildView(std::make_unique<views::View>());
  auto* card_stack_layout = card_stack->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets(), 10));
  card_stack_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto* name_card = card_stack->AddChildView(std::make_unique<views::View>());
  auto* name_card_layout = name_card->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         kCardInsets, 10));
  name_card_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  name_card->SetBackground(
      views::CreateRoundedRectBackground(ui::kColorSysSurface1, kCardCornerRadius));
  name_card->SetBorder(views::CreateRoundedRectBorder(
      1, kCardCornerRadius, ui::kColorSysSurfaceVariant));

  auto* name_header = name_card->AddChildView(std::make_unique<views::View>());
  auto* name_header_layout = name_header->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                         gfx::Insets(), 10));
  name_header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  CreateGlyphBadge(name_header, u"Aa");

  auto* name_header_text =
      name_header->AddChildView(std::make_unique<views::View>());
  auto* name_header_text_layout = name_header_text->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets(), 2));
  name_header_text_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  name_header_layout->SetFlexForView(name_header_text, 1);
  AddLabel(name_header_text, u"Space name", views::style::STYLE_BODY_3_MEDIUM,
           ui::kColorSysOnSurface);
  AddLabel(name_header_text, u"Choose a label you’ll recognize at a glance.",
           views::style::STYLE_BODY_5, ui::kColorSysOnSurfaceSubtle, true);

  name_field_ = name_card->AddChildView(std::make_unique<views::Textfield>());
  name_field_->SetPlaceholderText(u"Space name");
  name_field_->SetAccessibleName(u"Space name");
  name_field_->SetPreferredSize(gfx::Size(0, kInputHeight));
  name_field_->SetBorder(views::CreateRoundedRectBorder(
      1, kInputCornerRadius, ui::kColorSysSurfaceVariant));
  name_field_->SetExtraInsets(kInputInsets);
  name_field_->set_controller(this);

  CreateInfoCard(card_stack, u"☺", u"Profile",
                 u"Default profile selected for this space.");

  theme_button_ = card_stack->AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&MahoSidebarCreateSpaceView::OnThemePressed,
                          weak_factory_.GetWeakPtr()),
      std::u16string()));
  auto* theme_card_layout = theme_button_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                         kCardInsets, 10));
  theme_card_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  theme_button_->SetAccessibleName(u"Choose theme");
  theme_button_->SetBackground(
      views::CreateRoundedRectBackground(ui::kColorSysSurface1, kCardCornerRadius));
  theme_button_->SetBorder(views::CreateRoundedRectBorder(
      1, kCardCornerRadius, ui::kColorSysSurfaceVariant));

  CreateGlyphBadge(theme_button_, u"◐");

  auto* theme_header_text =
      theme_button_->AddChildView(std::make_unique<views::View>());
  auto* theme_header_text_layout = theme_header_text->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets(), 2));
  theme_header_text_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  theme_card_layout->SetFlexForView(theme_header_text, 1);
  AddLabel(theme_header_text, u"Theme", views::style::STYLE_BODY_3_MEDIUM,
           ui::kColorSysOnSurface);
  theme_summary_label_ = AddLabel(theme_header_text, u"",
                                  views::style::STYLE_BODY_5,
                                  ui::kColorSysOnSurfaceSubtle, true);

  theme_preview_dot_ =
      theme_button_->AddChildView(std::make_unique<views::View>());
  theme_preview_dot_->SetPreferredSize(
      gfx::Size(kThemePreviewDotSize, kThemePreviewDotSize));

  auto* trailing = AddLabel(theme_button_, u"›",
                            views::style::STYLE_BODY_3_MEDIUM,
                            ui::kColorSysOnSurfaceSubtle);
  trailing->SetHorizontalAlignment(gfx::ALIGN_CENTER);

  auto* bottom_region = content->AddChildView(std::make_unique<views::View>());
  auto* bottom_layout = bottom_region->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         kBottomRegionInsets, 8));
  bottom_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  create_button_ = bottom_region->AddChildView(
      std::make_unique<views::MdTextButton>(
          base::BindRepeating(&MahoSidebarCreateSpaceView::OnCreatePressed,
                              weak_factory_.GetWeakPtr()),
          u"Create Space"));
  create_button_->SetStyle(ui::ButtonStyle::kProminent);
  create_button_->SetCornerRadius(16);
  create_button_->SetMinSize(gfx::Size(0, 46));
  create_button_->SetAccessibleName(u"Create Space");

  error_label_ = AddLabel(bottom_region, u"", views::style::STYLE_BODY_5,
                          ui::kColorSysError, true);
  error_label_->SetVisible(false);

  auto* cancel_row = bottom_region->AddChildView(std::make_unique<views::View>());
  auto* cancel_row_layout = cancel_row->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                         gfx::Insets(), 0));
  cancel_row_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);

  cancel_button_ = cancel_row->AddChildView(
      std::make_unique<views::MdTextButton>(
          base::BindRepeating(&MahoSidebarCreateSpaceView::OnCancelPressed,
                              weak_factory_.GetWeakPtr()),
          u"Cancel"));
  cancel_button_->SetStyle(ui::ButtonStyle::kText);
  cancel_button_->SetAccessibleName(u"Cancel create space");

  auto* dots_row = bottom_region->AddChildView(std::make_unique<views::View>());
  auto* dots_layout = dots_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
  dots_layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  dots_layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);

  for (int i = 0; i < 3; ++i) {
    auto* dot = dots_row->AddChildView(std::make_unique<views::View>());
    dot->SetPreferredSize(gfx::Size(kBottomDotSize, kBottomDotSize));
    dot->SetBackground(views::CreateRoundedRectBackground(
        i == 0 ? ui::kColorSysOnSurfaceSubtle : ui::kColorSysSurfaceVariant,
        kBottomDotCornerRadius));
  }

  ResetFormState();
}

MahoSidebarCreateSpaceView::~MahoSidebarCreateSpaceView() = default;

void MahoSidebarCreateSpaceView::OnThemeChanged() {
  views::View::OnThemeChanged();
  SetBackground(views::CreateRoundedRectBackground(ui::kColorSysSurface,
                                                   kCreatePanelCornerRadius));
  SetBorder(views::CreateRoundedRectBorder(1, kCreatePanelCornerRadius,
                                           ui::kColorSysSurfaceVariant));

  if (!name_field_ || !GetColorProvider()) {
    return;
  }

  name_field_->SetBackgroundColor(
      GetColorProvider()->GetColor(ui::kColorSysSurface3));
  name_field_->SetTextColor(GetColorProvider()->GetColor(ui::kColorSysOnSurface));
  name_field_->set_placeholder_text_color(
      GetColorProvider()->GetColor(ui::kColorSysOnSurfaceSubtle));
}

void MahoSidebarCreateSpaceView::SetCloseCallback(
    base::RepeatingClosure close_callback) {
  close_callback_ = std::move(close_callback);
}

void MahoSidebarCreateSpaceView::PrepareForOpen() {
  ResetFormState();
}

void MahoSidebarCreateSpaceView::RequestNameFocus() {
  if (!name_field_) {
    return;
  }
  name_field_->RequestFocus();
  name_field_->SelectAll(false);
}

void MahoSidebarCreateSpaceView::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  if (sender != name_field_) {
    return;
  }
  if (error_label_) {
    error_label_->SetVisible(false);
  }
  UpdateCreateButtonState();
}

bool MahoSidebarCreateSpaceView::HandleKeyEvent(views::Textfield* sender,
                                                const ui::KeyEvent& event) {
  if (sender != name_field_ || event.type() != ui::EventType::kKeyPressed) {
    return false;
  }

  switch (event.key_code()) {
    case ui::VKEY_RETURN:
      OnCreatePressed();
      return true;
    case ui::VKEY_ESCAPE:
      OnCancelPressed();
      return true;
    default:
      return false;
  }
}

void MahoSidebarCreateSpaceView::ResetFormState() {
  theme_selection_ = DefaultThemeSelection();
  if (name_field_) {
    name_field_->SetText(u"");
  }
  if (error_label_) {
    error_label_->SetVisible(false);
  }
  UpdateThemeSummary();
  UpdateCreateButtonState();
}

void MahoSidebarCreateSpaceView::UpdateThemeSummary() {
  if (theme_summary_label_) {
    theme_summary_label_->SetText(BuildThemeSummaryForSelection(theme_selection_));
  }
  if (theme_preview_dot_) {
    theme_preview_dot_->SetBackground(views::CreateRoundedRectBackground(
        BuildSkColorForSelection(theme_selection_),
        kThemePreviewDotCornerRadius));
  }
}

void MahoSidebarCreateSpaceView::UpdateCreateButtonState() {
  if (!create_button_ || !name_field_) {
    return;
  }
  std::u16string trimmed_name;
  base::TrimWhitespace(name_field_->GetText(), base::TRIM_ALL, &trimmed_name);
  create_button_->SetEnabled(!trimmed_name.empty());
}

void MahoSidebarCreateSpaceView::OnCreatePressed() {
  if (!name_field_) {
    return;
  }

  std::u16string trimmed_name;
  base::TrimWhitespace(name_field_->GetText(), base::TRIM_ALL, &trimmed_name);
  if (trimmed_name.empty()) {
    return;
  }

  create_button_->SetEnabled(false);
  if (error_label_) {
    error_label_->SetVisible(false);
  }

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&CreateSpaceOnPool, base::UTF16ToUTF8(trimmed_name),
                     theme_selection_),
      base::BindOnce(
          [](base::WeakPtr<MahoSidebarCreateSpaceView> view,
             CreateResult result) {
            if (view) {
              view->OnSpaceCreated(result.success, result.space_id);
            }
          },
          weak_factory_.GetWeakPtr()));
}

void MahoSidebarCreateSpaceView::OnThemePressed() {
  if (!browser_) {
    return;
  }

  const std::string initial_json = BuildThemeJsonForSelection(theme_selection_);
  MahoSpaceThemePickerDialog::Open(
      browser_, initial_json,
      base::BindOnce(
          [](base::WeakPtr<MahoSidebarCreateSpaceView> view,
             const std::string& committed_theme_json,
             bool cancelled) {
            if (!view || cancelled) {
              return;
            }
            view->ApplyThemeSelection(
                BuildSelectionFromThemeJson(committed_theme_json));
          },
          weak_factory_.GetWeakPtr()));
}

void MahoSidebarCreateSpaceView::OnCancelPressed() {
  if (close_callback_) {
    close_callback_.Run();
    return;
  }
  if (GetWidget()) {
    GetWidget()->Close();
    return;
  }
  SetVisible(false);
}

void MahoSidebarCreateSpaceView::SetNameForTesting(const std::u16string& text) {
  if (!name_field_) {
    return;
  }
  name_field_->SetText(text);
  UpdateCreateButtonState();
}

void MahoSidebarCreateSpaceView::CompleteCreateSpaceForTesting(
    bool success,
    const std::string& new_space_id) {
  OnSpaceCreated(success, new_space_id);
}


void MahoSidebarCreateSpaceView::ApplyThemeSelection(
    const MahoSidebarCreateSpaceThemeSelection& selection) {
  theme_selection_ = selection;
  UpdateThemeSummary();
}

void MahoSidebarCreateSpaceView::OnSpaceCreated(bool success,
                                                const std::string& new_space_id) {
  if (!success) {
    if (error_label_) {
      error_label_->SetText(u"Couldn’t create space. Try again.");
      error_label_->SetVisible(true);
    }
    UpdateCreateButtonState();
    return;
  }

  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  if (bridge) {
    bridge->RegisterSpace(new_space_id,
                          base::FilePath::FromASCII(kDefaultProfileId));
    bridge->SwitchToSpace(new_space_id);
  }
  MahoSpaceThemeState::UpdateFromCore();
  ui::ColorProviderManager::Get().ResetColorProviderCache();

  ResetFormState();
  if (close_callback_) {
    close_callback_.Run();
    return;
  }
  SetVisible(false);
}

}  // namespace maho
