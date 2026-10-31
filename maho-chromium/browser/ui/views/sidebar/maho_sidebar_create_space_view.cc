// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/path_effect.h"
#include "chrome/browser/ui/browser.h"
#include "components/vector_icons/vector_icons.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/gfx/canvas.h"
#include "maho/browser/ui/theme/maho_space_theme_io.h"
#include "maho/browser/ui/views/space_create/maho_space_theme_picker_dialog.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom-shared.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {

constexpr int kCreatePanelCornerRadius = 18;
constexpr int kThemePreviewDotCornerRadius = 7;
constexpr char kDefaultProfileId[] = "default";
constexpr int kProfileMenuCommandIdBase = 1000;
constexpr int kProfileChipCornerRadius = 11;
constexpr int kProfileChipHeight = 24;
constexpr int kProfileChipHorizontalInset = 10;
constexpr int kProfileChipSpacing = 6;
constexpr int kProfileDisclosureIconSize = 12;
constexpr int kThemeRowCornerRadius = 12;
constexpr int kThemeRowHeight = 40;
constexpr int kThemeRowHorizontalInset = 10;
constexpr int kThemeRowSpacing = 10;
constexpr int kThemeTileSize = 22;
constexpr int kThemeTileCornerRadius = 7;
constexpr int kThemeDisclosureIconSize = 12;

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

struct ProfileListingItem {
  std::string id;
  std::string name;
  bool is_default = false;
  bool is_active = false;
};

const gfx::Insets kCreateSurfaceInsets = gfx::Insets();
const gfx::Insets kContentInsets = gfx::Insets::TLBR(24, 24, 20, 24);
const gfx::Insets kBottomRegionInsets = gfx::Insets::TLBR(14, 0, 0, 0);

MahoSidebarCreateSpaceThemeSelection DefaultThemeSelection() {
  return MahoSidebarCreateSpaceThemeSelection();
}

void ApplySpaceIconOnPool(const std::string& space_id, const std::string& icon) {
  if (space_id.empty() || icon.empty()) {
    return;
  }

  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }

  // `update_space_config` is the core's icon seam (SpaceConfigUpdate.icon).
  // There is no `set_space_icon` ShellEvent; that kind was silently dropped.
  base::DictValue changes;
  changes.Set("spaceId", space_id);
  changes.Set("icon", icon);
  base::DictValue event;
  event.Set("kind", "update_space_config");
  event.Set("changes", std::move(changes));
  std::string event_json;
  base::JSONWriter::Write(event, &event_json);
  char* result = maho_core_handle_event(core, event_json.c_str());
  if (result) {
    std::string updates(result);
    maho_string_free(result);
    InvalidateSidebarCoreCacheForUpdatesJson(updates);
  }
}

const ColorPreset& GetColorPresetForSelection(
    const MahoSidebarCreateSpaceThemeSelection& selection) {
  const int max_index = static_cast<int>(std::size(kColorPresets) - 1);
  const int index = std::clamp(selection.preset_index, 0, max_index);
  return kColorPresets[index];
}

int ClampBrightness(int brightness) {
  return std::clamp(brightness, kChooserBrightnessMin, kChooserBrightnessMax);
}

int ClampGrain(int grain) {
  return std::clamp(grain, kChooserGrainMin, kChooserGrainMax);
}

double CircularHueDistance(double lhs, double rhs) {
  const double direct = std::abs(lhs - rhs);
  return std::min(direct, 1.0 - direct);
}

int FindNearestPresetIndex(double hue, double saturation, int fallback_index) {
  const double normalized_hue = std::fmod(hue + 1.0, 1.0);
  double best_dist = 1e9;
  int best_index = fallback_index;
  for (int i = 0; i < static_cast<int>(std::size(kColorPresets)); ++i) {
    const ColorPreset& p = kColorPresets[i];
    const double d = CircularHueDistance(p.hue, normalized_hue) +
                     std::abs(p.saturation - saturation) * 0.5;
    if (d < best_dist) {
      best_dist = d;
      best_index = i;
    }
  }
  return best_index;
}

SkColor BuildSkColorForSelection(
    const MahoSidebarCreateSpaceThemeSelection& selection) {
  const ColorPreset& preset = GetColorPresetForSelection(selection);
  float hsv[3];
  hsv[0] = static_cast<float>(preset.hue * 360.0);
  hsv[1] = static_cast<float>(preset.saturation);
  hsv[2] = static_cast<float>(ClampBrightness(selection.brightness) / 100.0);
  return SkHSVToColor(hsv);
}

std::string BuildThemeJsonForSelection(
    const MahoSidebarCreateSpaceThemeSelection& selection) {
  const ColorPreset& preset = GetColorPresetForSelection(selection);
  const double brightness = ClampBrightness(selection.brightness) / 100.0;
  const double grain = ClampGrain(selection.grain) / 100.0;
  base::DictValue color_dict;
  color_dict.Set("hue", preset.hue);
  color_dict.Set("saturation", preset.saturation);
  color_dict.Set("brightness", brightness);
  color_dict.Set("grain", grain);

  if (selection.mode == MahoSidebarCreateSpaceThemeMode::kSolid) {
    base::DictValue theme_dict;
    theme_dict.Set("type", "solid");
    theme_dict.Set("color", std::move(color_dict));
    theme_dict.Set("opacity", selection.opacity);

    std::string theme_json;
    base::JSONWriter::Write(theme_dict, &theme_json);
    return theme_json;
  }

  base::ListValue gradient_colors;
  for (double offset : {-50.0 / 360.0, 0.0, 50.0 / 360.0}) {
    base::DictValue stop_dict;
    const double hue = std::fmod(preset.hue + offset + 1.0, 1.0);
    stop_dict.Set("hue", hue);
    stop_dict.Set("saturation", preset.saturation);
    stop_dict.Set("brightness", brightness);
    stop_dict.Set("isCustom", false);
    stop_dict.Set("isPrimary", offset == 0.0);
    gradient_colors.Append(std::move(stop_dict));
  }

  base::DictValue theme_dict;
  theme_dict.Set("type", "zen");
  theme_dict.Set("gradientColors", std::move(gradient_colors));
  theme_dict.Set("harmony", "analogous");
  theme_dict.Set("opacity", selection.opacity);
  theme_dict.Set("texture", grain);
  theme_dict.Set("scheme", selection.zen_scheme);

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
  if (const std::string* scheme = root.FindString("scheme")) {
    selection.zen_scheme = *scheme;
  }
  if (auto opacity = root.FindDouble("opacity")) {
    selection.opacity = *opacity;
  }

  if (const auto* color = root.FindDict("color")) {
    selection.zen_scheme = "auto";
    if (auto brightness = color->FindDouble("brightness")) {
      selection.brightness = static_cast<int>(*brightness * 100.0);
    }
    if (auto grain = color->FindDouble("grain")) {
      selection.grain = static_cast<int>(*grain * 100.0);
    }
    auto hue = color->FindDouble("hue");
    auto saturation = color->FindDouble("saturation");
    if (hue && saturation) {
      selection.preset_index =
          FindNearestPresetIndex(*hue, *saturation, selection.preset_index);
    }
    return selection;
  }

  const auto* gradient_colors = root.FindList("gradientColors");
  if (!gradient_colors || gradient_colors->empty()) {
    return selection;
  }

  const base::DictValue* primary_stop = nullptr;
  for (const auto& stop : *gradient_colors) {
    const auto* stop_dict = stop.GetIfDict();
    if (!stop_dict) {
      continue;
    }
    if (stop_dict->FindBool("isPrimary").value_or(false)) {
      primary_stop = stop_dict;
      break;
    }
  }
  if (!primary_stop) {
    primary_stop = (*gradient_colors)[0].GetIfDict();
  }
  if (!primary_stop) {
    return selection;
  }

  if (auto brightness = primary_stop->FindDouble("brightness")) {
    selection.brightness = static_cast<int>(*brightness * 100.0);
  }
  if (auto texture = root.FindDouble("texture")) {
    selection.grain = static_cast<int>(*texture * 100.0);
  }
  auto hue = primary_stop->FindDouble("hue");
  auto saturation = primary_stop->FindDouble("saturation");
  if (hue && saturation) {
    selection.preset_index =
        FindNearestPresetIndex(*hue, *saturation, selection.preset_index);
  }
  return selection;
}

views::Label* AddLabel(views::View* parent,
                       const std::u16string& text,
                       views::style::TextStyle text_style,
                       SkColor color,
                       bool multiline = false) {
  auto label = std::make_unique<views::Label>(text);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetAutoColorReadabilityEnabled(false);
  label->SetEnabledColor(color);
  label->SetTextStyle(text_style);
  label->SetMultiLine(multiline);
  label->SetElideBehavior(multiline ? gfx::NO_ELIDE : gfx::ELIDE_TAIL);
  label->SetSubpixelRenderingEnabled(false);
  return parent->AddChildView(std::move(label));
}

views::Label* AddSemanticLabel(views::View* parent,
                               const std::u16string& text,
                               views::style::TextStyle text_style,
                               ui::ColorId color_id,
                               bool multiline = false) {
  auto* label = AddLabel(parent, text, text_style, SK_ColorTRANSPARENT,
                         multiline);
  label->SetEnabledColor(color_id);
  return label;
}

CreateResult CreateSpaceOnPool(const std::string& name,
                               const MahoSidebarCreateSpaceThemeSelection&
                                    theme_selection,
                               const std::string& profile_id,
                               const std::string& selected_icon) {
  CreateResult result;
  MahoCore* core = maho::GetCore();
  if (!core) {
    DLOG(WARNING) << "Sidebar create-space failed: Maho core unavailable";
    return result;
  }

  const ColorPreset& preset = GetColorPresetForSelection(theme_selection);
  base::DictValue color_dict;
  color_dict.Set("hue", preset.hue);
  color_dict.Set("saturation", preset.saturation);
  color_dict.Set("brightness",
                 ClampBrightness(theme_selection.brightness) / 100.0);
  color_dict.Set("grain", ClampGrain(theme_selection.grain) / 100.0);
  std::string color_json;
  base::JSONWriter::Write(color_dict, &color_json);

  char* ffi_result = maho_core_create_space(core, name.c_str(),
                                            color_json.c_str(),
                                            profile_id.c_str());
  if (!ffi_result) {
    DLOG(WARNING)
        << "Sidebar create-space failed: maho_core_create_space returned null"
        << " for profile_id='" << profile_id << "'";
    return result;
  }

  SidebarCacheInvalidation invalidation;
  invalidation.fragments = SidebarCoreFragment::kFooter;
  InvalidateSidebarCoreCache(invalidation);

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
  ApplySpaceIconOnPool(result.space_id, selected_icon);
  result.success = true;
  maho_string_free(ffi_result);
  return result;
}

std::vector<ProfileListingItem> BuildProfilesOnPool() {
  std::vector<ProfileListingItem> profiles;
  MahoCore* core = maho::GetCore();
  if (!core) {
    return profiles;
  }

  char* json_str = maho_core_list_profiles(core);
  if (!json_str) {
    return profiles;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return profiles;
  }

  // maho_core_get_active_profile_id() may return a JSON-encoded string
  // (e.g. "\"abc123\"") rather than a bare id — mirror the decode pattern
  // used by DecodeActiveSpaceId in maho_browser_main_extra_parts.cc.
  char* active_id_str = maho_core_get_active_profile_id(core);
  std::string active_id;
  if (active_id_str) {
    std::string raw(active_id_str);
    maho_string_free(active_id_str);
    std::optional<base::Value> decoded =
        base::JSONReader::Read(raw, base::JSON_PARSE_RFC);
    active_id = (decoded && decoded->is_string()) ? decoded->GetString() : raw;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    const std::string* name = dict->FindString("name");
    if (!id || !name) {
      continue;
    }

    ProfileListingItem profile;
    profile.id = *id;
    profile.name = *name;
    profile.is_default = dict->FindBool("is_default").value_or(false);
    profile.is_active = (*id == active_id);
    profiles.push_back(std::move(profile));
  }

  return profiles;
}

class CreateSpaceHeroView : public views::View {
  METADATA_HEADER(CreateSpaceHeroView, views::View)

 public:
  CreateSpaceHeroView() {
    SetPreferredSize(gfx::Size(86, 60));
  }
  CreateSpaceHeroView(const CreateSpaceHeroView&) = delete;
  CreateSpaceHeroView& operator=(const CreateSpaceHeroView&) = delete;
  ~CreateSpaceHeroView() override = default;

  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);
    const gfx::Rect b = GetContentsBounds();
    if (b.IsEmpty()) {
      return;
    }
    auto draw_card = [&](const gfx::RectF& rect, SkColor fill,
                         float rotate_deg, float pivot_x, float pivot_y) {
      cc::PaintFlags fill_flags;
      fill_flags.setAntiAlias(true);
      fill_flags.setStyle(cc::PaintFlags::kFill_Style);
      fill_flags.setColor(fill);
      cc::PaintFlags stroke_flags;
      stroke_flags.setAntiAlias(true);
      stroke_flags.setStyle(cc::PaintFlags::kStroke_Style);
      stroke_flags.setStrokeWidth(1.0f);
      stroke_flags.setColor(SkColorSetARGB(0xC8, 0x10, 0x10, 0x14));
      canvas->Save();
      canvas->sk_canvas()->translate(pivot_x, pivot_y);
      canvas->sk_canvas()->rotate(rotate_deg);
      canvas->sk_canvas()->translate(-pivot_x, -pivot_y);
      canvas->DrawRoundRect(rect, 8.0f, fill_flags);
      canvas->DrawRoundRect(rect, 8.0f, stroke_flags);
      canvas->Restore();
    };
    const float cx = b.x() + b.width() / 2.0f;
    const float cy = b.y() + b.height() / 2.0f;
    draw_card(gfx::RectF(cx - 27.0f, cy - 16.0f, 26.0f, 34.0f),
              SkColorSetARGB(0xF6, 0xB2, 0xAE, 0xA5), -8.0f,
              cx - 14.0f, cy);
    draw_card(gfx::RectF(cx + 1.0f, cy - 17.0f, 26.0f, 34.0f),
              SkColorSetARGB(0xF6, 0xC8, 0xC4, 0xBC), 8.0f,
              cx + 14.0f, cy);
    draw_card(gfx::RectF(cx - 16.0f, cy - 19.0f, 32.0f, 38.0f),
              SkColorSetARGB(0xFC, 0xF4, 0xF1, 0xEA), 0.0f, cx, cy);
    cc::PaintFlags screen_flags;
    screen_flags.setAntiAlias(true);
    screen_flags.setStyle(cc::PaintFlags::kFill_Style);
    screen_flags.setColor(SkColorSetARGB(0xEE, 0x2C, 0x2C, 0x34));
    canvas->DrawRoundRect(gfx::RectF(cx - 9.0f, cy - 11.0f, 18.0f, 12.0f),
                          3.0f, screen_flags);
    cc::PaintFlags base_flags;
    base_flags.setAntiAlias(true);
    base_flags.setStyle(cc::PaintFlags::kStroke_Style);
    base_flags.setStrokeWidth(1.4f);
    base_flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
    base_flags.setColor(SkColorSetARGB(0xC8, 0x4A, 0x4A, 0x52));
    canvas->DrawLine(gfx::PointF(cx - 11.0f, cy + 5.0f),
                     gfx::PointF(cx + 11.0f, cy + 5.0f), base_flags);
    cc::PaintFlags leaf_fill;
    leaf_fill.setAntiAlias(true);
    leaf_fill.setStyle(cc::PaintFlags::kFill_Style);
    leaf_fill.setColor(SkColorSetARGB(0xE6, 0x55, 0xA8, 0x65));
    cc::PaintFlags leaf_dark;
    leaf_dark.setAntiAlias(true);
    leaf_dark.setStyle(cc::PaintFlags::kFill_Style);
    leaf_dark.setColor(SkColorSetARGB(0xE6, 0x36, 0x82, 0x4A));
    auto draw_leaf = [&](float lx, float ly, float w, float h,
                         float rotate_deg, const cc::PaintFlags& flags) {
      canvas->Save();
      canvas->sk_canvas()->translate(lx, ly);
      canvas->sk_canvas()->rotate(rotate_deg);
      canvas->DrawRoundRect(gfx::RectF(-w / 2.0f, -h / 2.0f, w, h),
                            std::min(w, h) / 2.0f, flags);
      canvas->Restore();
    };
    draw_leaf(cx - 22.0f, cy - 10.0f, 8.0f, 14.0f, -25.0f, leaf_dark);
    draw_leaf(cx - 18.0f, cy - 16.0f, 9.0f, 16.0f, -10.0f, leaf_fill);
    draw_leaf(cx - 12.0f, cy - 19.0f, 7.0f, 12.0f, 15.0f, leaf_dark);

    cc::PaintFlags pencil_body;
    pencil_body.setAntiAlias(true);
    pencil_body.setStyle(cc::PaintFlags::kFill_Style);
    pencil_body.setColor(SkColorSetARGB(0xEE, 0xE3, 0x6E, 0x4A));
    canvas->DrawRoundRect(gfx::RectF(cx + 14.0f, cy + 2.0f, 14.0f, 4.5f),
                          1.5f, pencil_body);
    cc::PaintFlags pencil_tip;
    pencil_tip.setAntiAlias(true);
    pencil_tip.setStyle(cc::PaintFlags::kFill_Style);
    pencil_tip.setColor(SkColorSetARGB(0xEE, 0x33, 0x33, 0x36));
    canvas->DrawRoundRect(gfx::RectF(cx + 26.0f, cy + 2.5f, 3.0f, 3.5f),
                          1.0f, pencil_tip);
  }
};

class SpaceIconPickerButton : public views::Button {
  METADATA_HEADER(SpaceIconPickerButton, views::Button)

 public:
  SpaceIconPickerButton(PressedCallback callback,
                        const MahoSidebarPalette& palette)
      : views::Button(std::move(callback)), palette_(palette) {
    SetAccessibleName(u"Choose space icon");
    SetTooltipText(u"Choose space icon");
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetPreferredSize(gfx::Size(24, 24));
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));
    layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    label_ = AddChildView(std::make_unique<views::Label>(u"+"));
    label_->SetAutoColorReadabilityEnabled(false);
    label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    label_->SetTextStyle(views::style::STYLE_BODY_3_MEDIUM);
    UpdateLabelColor();
  }
  SpaceIconPickerButton(const SpaceIconPickerButton&) = delete;
  SpaceIconPickerButton& operator=(const SpaceIconPickerButton&) = delete;
  ~SpaceIconPickerButton() override = default;

  void SetSelectedIcon(const std::string& icon) {
    selected_icon_ = icon;
    label_->SetText(selected_icon_.empty() ? u"+"
                                           : base::UTF8ToUTF16(selected_icon_));
    UpdateLabelColor();
    SchedulePaint();
  }

  void SetSidebarPalette(const MahoSidebarPalette& palette) {
    palette_ = palette;
    UpdateLabelColor();
    SchedulePaint();
  }

  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    UpdateLabelColor();
    SchedulePaint();
  }

 protected:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::Rect bounds = GetContentsBounds();
    if (bounds.IsEmpty()) {
      return;
    }

    if (!selected_icon_.empty()) {
      cc::PaintFlags fill_flags;
      fill_flags.setAntiAlias(true);
      fill_flags.setStyle(cc::PaintFlags::kFill_Style);
      fill_flags.setColor(palette_.row_hover);

      cc::PaintFlags stroke_flags;
      stroke_flags.setAntiAlias(true);
      stroke_flags.setStyle(cc::PaintFlags::kStroke_Style);
      stroke_flags.setStrokeWidth(1.0f);
      stroke_flags.setColor(palette_.outline);

      gfx::RectF rect(bounds);
      rect.Inset(0.5f);
      canvas->DrawRoundRect(rect, 7.0f, fill_flags);
      canvas->DrawRoundRect(rect, 7.0f, stroke_flags);
      return;
    }

    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1.0f);
    flags.setColor(palette_.outline);
    const float intervals[] = {2.5f, 2.0f};
    flags.setPathEffect(cc::PathEffect::MakeDash(intervals, 2, 0));
    gfx::RectF rect(bounds);
    rect.Inset(0.5f);
    canvas->DrawRoundRect(rect, 5.0f, flags);
  }

 private:
  void UpdateLabelColor() {
    if (!label_) {
      return;
    }
    label_->SetEnabledColor(selected_icon_.empty() ? palette_.secondary_text
                                                   : palette_.primary_text);
  }

  raw_ptr<views::Label> label_ = nullptr;
  std::string selected_icon_;
  MahoSidebarPalette palette_;
};
BEGIN_METADATA(SpaceIconPickerButton)
END_METADATA

class SidebarCreateSpaceRowButton : public views::Button {
  METADATA_HEADER(SidebarCreateSpaceRowButton, views::Button)

 public:
  SidebarCreateSpaceRowButton(PressedCallback callback,
                              int corner_radius,
                              SkColor background_color,
                              const MahoSidebarPalette& palette)
      : views::Button(std::move(callback)),
        corner_radius_(corner_radius),
        background_color_(background_color),
        palette_(palette) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
  }
  SidebarCreateSpaceRowButton(const SidebarCreateSpaceRowButton&) = delete;
  SidebarCreateSpaceRowButton& operator=(const SidebarCreateSpaceRowButton&) =
      delete;
  ~SidebarCreateSpaceRowButton() override = default;

  void SetSidebarPalette(const MahoSidebarPalette& palette,
                         SkColor background_color) {
    palette_ = palette;
    background_color_ = background_color;
    RefreshBackground();
  }

  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    RefreshBackground();
  }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    RefreshBackground();
  }

  void OnFocus() override {
    views::Button::OnFocus();
    RefreshBackground();
  }

  void OnBlur() override {
    views::Button::OnBlur();
    RefreshBackground();
  }

 private:
  void RefreshBackground() {
    const SkColor fill_color =
        GetState() == STATE_PRESSED
            ? palette_.row_selected
            : GetState() == STATE_HOVERED || HasFocus() ? palette_.row_hover
                                                        : background_color_;
    SetBackground(
        views::CreateRoundedRectBackground(fill_color, corner_radius_));
  }

  const int corner_radius_;
  SkColor background_color_;
  MahoSidebarPalette palette_;
};
BEGIN_METADATA(SidebarCreateSpaceRowButton)
END_METADATA

[[maybe_unused]] bool IsDefaultProfileSentinel(const std::string& profile_id) {
  return profile_id.empty() || profile_id == "default" || profile_id == "Default";
}

BEGIN_METADATA(CreateSpaceHeroView)
END_METADATA

}  // namespace

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

  hero->AddChildView(std::make_unique<CreateSpaceHeroView>());

  title_label_ = AddLabel(hero, u"Create a Space",
                          views::style::STYLE_HEADLINE_4,
                          palette_.primary_text, true);
  title_label_->SetMaximumWidth(360);

  subtitle_label_ = AddLabel(
      hero,
      u"Separate your tabs for life, work, projects, and more.",
      views::style::STYLE_BODY_5, palette_.secondary_text, true);
  subtitle_label_->SetMaximumWidth(360);

  auto* card_stack = content->AddChildView(std::make_unique<views::View>());
  auto* card_stack_layout = card_stack->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets(), 10));
  card_stack_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  name_row_ = card_stack->AddChildView(std::make_unique<views::View>());
  auto* name_row_layout = name_row_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                         gfx::Insets::VH(0, 10), 10));
  name_row_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  name_row_->SetPreferredSize(gfx::Size(0, 44));

  name_leading_icon_button_ = name_row_->AddChildView(
      std::make_unique<SpaceIconPickerButton>(
          base::BindRepeating(&MahoSidebarCreateSpaceView::OnIconPressed,
                              weak_factory_.GetWeakPtr()),
          palette_));

  name_field_ = name_row_->AddChildView(std::make_unique<views::Textfield>());
  name_field_->SetPlaceholderText(u"Space name");
  name_field_->SetAccessibleName(u"Space name");
  name_field_->SetBorder(nullptr);
  name_field_->SetBackgroundColor(SK_ColorTRANSPARENT);
  name_field_->set_controller(this);
  name_row_layout->SetFlexForView(name_field_, 1);

  profile_row_ = card_stack->AddChildView(std::make_unique<views::View>());
  auto* profile_row_layout = profile_row_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kHorizontal,
                                         gfx::Insets::VH(0, 10), 8));
  profile_row_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  profile_row_->SetPreferredSize(gfx::Size(0, 40));

  profile_label_ = AddLabel(profile_row_, u"Profile",
                            views::style::STYLE_BODY_3_MEDIUM,
                            palette_.primary_text);
  profile_row_layout->SetFlexForView(profile_label_, 1);

  profile_button_ = profile_row_->AddChildView(
      std::make_unique<SidebarCreateSpaceRowButton>(
           base::BindRepeating(&MahoSidebarCreateSpaceView::OnProfilePressed,
                               weak_factory_.GetWeakPtr()),
           kProfileChipCornerRadius, palette_.row_hover, palette_));
  profile_button_->SetAccessibleName(u"Choose profile");
  auto* profile_button_layout =
      profile_button_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, kProfileChipHorizontalInset),
          kProfileChipSpacing));
  profile_button_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  profile_button_layout->set_minimum_cross_axis_size(kProfileChipHeight);

  profile_button_label_ = profile_button_->AddChildView(
      std::make_unique<views::Label>(u""));
  profile_button_label_->SetAutoColorReadabilityEnabled(false);
  profile_button_label_->SetEnabledColor(palette_.primary_text);
  profile_button_label_->SetTextStyle(views::style::STYLE_BODY_5);
  profile_button_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  profile_button_label_->SetElideBehavior(gfx::ELIDE_TAIL);

  profile_disclosure_ =
      profile_button_->AddChildView(std::make_unique<views::ImageView>());
  profile_disclosure_->SetPreferredSize(
      gfx::Size(kProfileDisclosureIconSize, kProfileDisclosureIconSize));

  theme_button_ = card_stack->AddChildView(
      std::make_unique<SidebarCreateSpaceRowButton>(
           base::BindRepeating(&MahoSidebarCreateSpaceView::OnThemePressed,
                               weak_factory_.GetWeakPtr()),
           kThemeRowCornerRadius, palette_.row_active, palette_));
  theme_button_->SetAccessibleName(u"Choose theme");
  theme_button_->SetPreferredSize(gfx::Size(0, kThemeRowHeight));
  auto* theme_button_layout =
      theme_button_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, kThemeRowHorizontalInset), kThemeRowSpacing));
  theme_button_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  theme_preview_tile_ = theme_button_->AddChildView(std::make_unique<views::View>());
  theme_preview_tile_->SetPreferredSize(gfx::Size(kThemeTileSize, kThemeTileSize));

  theme_label_ = theme_button_->AddChildView(
      std::make_unique<views::Label>(u"Choose a Theme"));
  theme_label_->SetAutoColorReadabilityEnabled(false);
  theme_label_->SetEnabledColor(palette_.primary_text);
  theme_label_->SetTextStyle(views::style::STYLE_BODY_3_MEDIUM);
  theme_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  theme_button_layout->SetFlexForView(theme_label_, 1);

  theme_disclosure_ =
      theme_button_->AddChildView(std::make_unique<views::ImageView>());
  theme_disclosure_->SetPreferredSize(
      gfx::Size(kThemeDisclosureIconSize, kThemeDisclosureIconSize));

  auto* bottom_region = AddChildView(std::make_unique<views::View>());
  auto* bottom_layout = bottom_region->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         kBottomRegionInsets, 8));
  bottom_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // Error feedback is semantic and intentionally remains danger red.
  error_label_ = AddSemanticLabel(bottom_region, u"",
                                  views::style::STYLE_BODY_5,
                                  kMahoColorDangerRed, true);
  error_label_->SetVisible(false);

  create_button_ = bottom_region->AddChildView(
      std::make_unique<views::MdTextButton>(
          base::BindRepeating(&MahoSidebarCreateSpaceView::OnCreatePressed,
                              weak_factory_.GetWeakPtr()),
          u"Create Space"));
  create_button_->SetStyle(ui::ButtonStyle::kProminent);
  create_button_->SetCornerRadius(26);
  create_button_->SetMinSize(gfx::Size(0, 52));
  create_button_->SetAccessibleName(u"Create Space");

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

  ApplyPalette();
  RefreshProfiles();
  ResetFormState();
}

MahoSidebarCreateSpaceView::~MahoSidebarCreateSpaceView() = default;

void MahoSidebarCreateSpaceView::SetSidebarPalette(
    const MahoSidebarPalette& palette) {
  palette_ = palette;
  ApplyPalette();
}

void MahoSidebarCreateSpaceView::OnThemeChanged() {
  views::View::OnThemeChanged();
  ApplyPalette();
}

void MahoSidebarCreateSpaceView::ApplyPalette() {
  const SkColor surface = palette_.normal_contrast_surfaces.empty()
                              ? palette_.row_selected
                              : palette_.normal_contrast_surfaces.front();
  SetBackground(
      views::CreateRoundedRectBackground(surface, kCreatePanelCornerRadius));
  SetBorder(views::CreateRoundedRectBorder(1, kCreatePanelCornerRadius,
                                            palette_.outline));
  if (title_label_) {
    title_label_->SetEnabledColor(palette_.primary_text);
  }
  if (subtitle_label_) {
    subtitle_label_->SetEnabledColor(palette_.secondary_text);
  }
  if (name_row_) {
    name_row_->SetBackground(
        views::CreateRoundedRectBackground(palette_.row_active, 12));
    name_row_->SetBorder(
        views::CreateRoundedRectBorder(1, 12, palette_.outline));
  }
  if (name_field_) {
    name_field_->SetBackgroundColor(palette_.row_selected);
    name_field_->SetMahoResolvedTextColor(palette_.primary_text);
    name_field_->SetMahoResolvedPlaceholderTextColor(palette_.secondary_text);
  }
  if (name_leading_icon_button_) {
    static_cast<SpaceIconPickerButton*>(name_leading_icon_button_.get())
        ->SetSidebarPalette(palette_);
  }
  if (profile_row_) {
    profile_row_->SetBackground(
        views::CreateRoundedRectBackground(palette_.row_active, 12));
  }
  if (profile_label_) {
    profile_label_->SetEnabledColor(palette_.primary_text);
  }
  if (profile_button_) {
    static_cast<SidebarCreateSpaceRowButton*>(profile_button_.get())
        ->SetSidebarPalette(palette_, palette_.row_hover);
  }
  if (profile_button_label_) {
    profile_button_label_->SetEnabledColor(palette_.primary_text);
  }
  if (profile_disclosure_) {
    profile_disclosure_->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kExpandMoreOldIcon, palette_.secondary_text,
        kProfileDisclosureIconSize));
  }
  if (theme_button_) {
    static_cast<SidebarCreateSpaceRowButton*>(theme_button_.get())
        ->SetSidebarPalette(palette_, palette_.row_active);
  }
  if (theme_preview_tile_) {
    theme_preview_tile_->SetBorder(views::CreateRoundedRectBorder(
        1, kThemeTileCornerRadius, palette_.outline));
  }
  if (theme_label_) {
    theme_label_->SetEnabledColor(palette_.primary_text);
  }
  if (theme_disclosure_) {
    theme_disclosure_->SetImage(ui::ImageModel::FromVectorIcon(
        vector_icons::kSubmenuArrowChromeRefreshOldIcon, palette_.secondary_text,
        kThemeDisclosureIconSize));
  }
  // MdTextButton text, fill and stroke default to OS-theme ColorIds.
  if (create_button_) {
    create_button_->SetEnabledTextColors(palette_.primary_text);
    create_button_->SetTextColor(views::Button::STATE_DISABLED,
                                 palette_.disabled_text);
    create_button_->SetBgColorOverrideDeprecated(palette_.row_selected);
    create_button_->SetStrokeColorOverrideDeprecated(palette_.outline);
  }
  if (cancel_button_) {
    cancel_button_->SetEnabledTextColors(palette_.secondary_text);
    cancel_button_->SetTextColor(views::Button::STATE_DISABLED,
                                 palette_.disabled_text);
  }
  SchedulePaint();
}

void MahoSidebarCreateSpaceView::SetCloseCallback(
    base::RepeatingClosure close_callback) {
  close_callback_ = std::move(close_callback);
}

void MahoSidebarCreateSpaceView::PrepareForOpen() {
  RefreshProfiles();
  ResetFormState();
}

void MahoSidebarCreateSpaceView::RequestNameFocus() {
  if (!name_field_) {
    return;
  }
  name_field_->RequestFocus();
  name_field_->SelectAll(false);
}

void MahoSidebarCreateSpaceView::ExecuteCommand(int command_id,
                                                 int event_flags) {
  (void)event_flags;
  const int index = command_id - kProfileMenuCommandIdBase;
  if (index < 0 || index >= static_cast<int>(profile_menu_items_.size())) {
    return;
  }
  SetSelectedProfile(profile_menu_items_[index]);
}

bool MahoSidebarCreateSpaceView::IsCommandIdChecked(int command_id) const {
  const int index = command_id - kProfileMenuCommandIdBase;
  if (index < 0 || index >= static_cast<int>(profile_menu_items_.size())) {
    return false;
  }
  return profile_menu_items_[index].id == selected_profile_id_;
}

void MahoSidebarCreateSpaceView::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  if (sender != name_field_) {
    return;
  }

  if (restoring_name_text_) {
    restoring_name_text_ = false;
    return;
  }

  if (awaiting_icon_input_) {
    awaiting_icon_input_ = false;

    std::u16string selected_icon_text = new_contents;
    base::TrimWhitespace(selected_icon_text, base::TRIM_ALL, &selected_icon_text);
    if (!selected_icon_text.empty()) {
      selected_icon_ = base::UTF16ToUTF8(selected_icon_text);
      UpdateNameLeadingIconButton();
    }

    RestoreSavedNameText();
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

  if (awaiting_icon_input_) {
    awaiting_icon_input_ = false;
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

void MahoSidebarCreateSpaceView::OnAfterUserAction(views::Textfield* sender) {
  if (sender != name_field_ || !awaiting_icon_input_) {
    return;
  }

  awaiting_icon_input_ = false;
}

void MahoSidebarCreateSpaceView::ResetFormState() {
  theme_selection_ = DefaultThemeSelection();
  selected_icon_.clear();
  awaiting_icon_input_ = false;
  restoring_name_text_ = false;
  saved_name_text_.clear();
  if (name_field_) {
    name_field_->SetText(u"");
  }
  if (error_label_) {
    error_label_->SetVisible(false);
  }
  if (!profile_menu_items_.empty()) {
    auto it = std::find_if(profile_menu_items_.begin(), profile_menu_items_.end(),
                           [](const ProfileMenuItem& item) {
                             return item.is_active;
                           });
    if (it == profile_menu_items_.end()) {
      it = std::find_if(profile_menu_items_.begin(), profile_menu_items_.end(),
                        [](const ProfileMenuItem& item) {
                          return item.is_default;
                        });
    }
    if (it == profile_menu_items_.end()) {
      it = profile_menu_items_.begin();
    }
    SetSelectedProfile(*it);
  }
  UpdateThemeSummary();
  UpdateNameLeadingIconButton();
  UpdateCreateButtonState();
}

void MahoSidebarCreateSpaceView::RefreshProfiles() {
  profile_menu_items_.clear();

  for (const auto& profile : BuildProfilesOnPool()) {
    ProfileMenuItem item;
    item.id = profile.id;
    item.label = base::UTF8ToUTF16(profile.name);
    item.is_default = profile.is_default;
    item.is_active = profile.is_active;
    profile_menu_items_.push_back(std::move(item));
  }

  if (profile_menu_items_.empty()) {
    ProfileMenuItem fallback;
    fallback.id = kDefaultProfileId;
    fallback.label = u"Default";
    fallback.is_default = true;
    fallback.is_active = true;
    profile_menu_items_.push_back(std::move(fallback));
  }
}

void MahoSidebarCreateSpaceView::SetSelectedProfile(
    const ProfileMenuItem& profile) {
  selected_profile_id_ = profile.id;
  selected_profile_label_ = profile.label;
  UpdateProfileButtonLabel();
}

void MahoSidebarCreateSpaceView::UpdateProfileButtonLabel() {
  if (!profile_button_label_) {
    return;
  }
  profile_button_label_->SetText(selected_profile_label_);
}

void MahoSidebarCreateSpaceView::OnProfilePressed() {
  if (!profile_button_) {
    return;
  }

  profile_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  for (size_t i = 0; i < profile_menu_items_.size(); ++i) {
    profile_menu_model_->AddCheckItem(kProfileMenuCommandIdBase + i,
                                      profile_menu_items_[i].label);
  }

  profile_menu_runner_ = std::make_unique<views::MenuRunner>(
      profile_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);

  const gfx::Rect bounds = profile_button_->GetBoundsInScreen();
  profile_menu_runner_->RunMenuAt(profile_button_->GetWidget(), nullptr, bounds,
                                  views::MenuAnchorPosition::kTopRight,
                                  ui::mojom::MenuSourceType::kNone);
}

void MahoSidebarCreateSpaceView::OnIconPressed() {
  if (!name_field_) {
    return;
  }

  views::Widget* widget = GetWidget();
  if (!widget) {
    return;
  }

  saved_name_text_ = name_field_->GetText();
  awaiting_icon_input_ = true;
  restoring_name_text_ = false;

  name_field_->RequestFocus();
  name_field_->SelectAll(false);
  widget->ShowEmojiPanel();
}

void MahoSidebarCreateSpaceView::RestoreSavedNameText() {
  if (!name_field_) {
    return;
  }

  restoring_name_text_ = true;
  name_field_->SetText(saved_name_text_);
  name_field_->ClearSelection();
}

void MahoSidebarCreateSpaceView::UpdateThemeSummary() {
  if (theme_preview_tile_) {
    theme_preview_tile_->SetBackground(views::CreateRoundedRectBackground(
        BuildSkColorForSelection(theme_selection_),
        kThemePreviewDotCornerRadius));
  }
}

void MahoSidebarCreateSpaceView::UpdateNameLeadingIconButton() {
  if (!name_leading_icon_button_) {
    return;
  }
  static_cast<SpaceIconPickerButton*>(name_leading_icon_button_.get())
      ->SetSelectedIcon(selected_icon_);
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
                     theme_selection_, selected_profile_id_, selected_icon_),
      base::BindOnce(
          [](base::WeakPtr<MahoSidebarCreateSpaceView> view,
             std::string profile_id,
             CreateResult result) {
            if (view) {
              view->OnSpaceCreated(result.success, result.space_id, profile_id);
            }
          },
          weak_factory_.GetWeakPtr(), selected_profile_id_));
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
    const std::string& new_space_id,
    const std::string& profile_id) {
  OnSpaceCreated(success, new_space_id, profile_id);
}

void MahoSidebarCreateSpaceView::ApplyThemeSelection(
    const MahoSidebarCreateSpaceThemeSelection& selection) {
  theme_selection_ = selection;
  UpdateThemeSummary();
}

void MahoSidebarCreateSpaceView::OnSpaceCreated(bool success,
                                                const std::string& new_space_id,
                                                const std::string& profile_id) {
  if (!success || new_space_id.empty()) {
    if (error_label_) {
      error_label_->SetText(u"Couldn’t create space. Try again.");
      error_label_->SetVisible(true);
    }
    UpdateCreateButtonState();
    return;
  }

  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  if (bridge) {
    std::optional<base::FilePath> profile_path =
        maho::MahoSpaceProfileBridge::ProfileBasenameForId(profile_id);
    if (!profile_path) {
      if (error_label_) {
        error_label_->SetText(u"Couldn’t create space. Try again.");
        error_label_->SetVisible(true);
      }
      UpdateCreateButtonState();
      return;
    }
    bridge->RegisterSpace(new_space_id, *profile_path);
    MahoSidebarView::ActivateSpaceAndTab(browser_, new_space_id);

    views::View* ancestor = parent();
    MahoSidebarView* sidebar_view = nullptr;
    while (ancestor) {
      if (auto* candidate = views::AsViewClass<MahoSidebarView>(ancestor)) {
        sidebar_view = candidate;
        break;
      }
      ancestor = ancestor->parent();
    }
    if (sidebar_view) {
      sidebar_view->ScheduleRefreshAll();
    }
  }

  ResetFormState();
  if (close_callback_) {
    close_callback_.Run();
    return;
  }
  SetVisible(false);
}

BEGIN_METADATA(MahoSidebarCreateSpaceView)
END_METADATA

}  // namespace maho
