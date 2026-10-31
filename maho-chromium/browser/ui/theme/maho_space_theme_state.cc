// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/theme/maho_space_theme_state.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "content/public/browser/browser_thread.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/native_theme/native_theme.h"

base::NoDestructor<base::flat_map<std::string, MahoSpaceThemeState::ThemeData>>
    MahoSpaceThemeState::committed_themes_;
base::NoDestructor<base::flat_map<Browser*, MahoSpaceThemeState::ThemeData>>
    MahoSpaceThemeState::preview_overrides_;

namespace {

// Converts a hue value to Skia's 0–360 range.
// WebUI theme editors emit fractional hue (0.0–1.0).
// Legacy/default Rust SpaceColor may use degree hue (e.g. 220.0).
// Disambiguate: values ≤ 1.0 are treated as fractions, else as degrees.
SkScalar NormalizeHueToDegrees(double hue) {
  if (hue <= 1.0) {
    hue *= 360.0;
  }
  // Wrap into [0, 360).
  hue = std::fmod(hue, 360.0);
  if (hue < 0.0) {
    hue += 360.0;
  }
  return static_cast<SkScalar>(hue);
}

// Extracts HSV from a Rust SpaceColor/ThemeColor dict.
// Hue is normalized defensively for both fractional and degree formats.
// Saturation and brightness are clamped to [0, 1].
std::optional<MahoSpaceThemeState::SpaceHSV> ExtractHSBFromColorDict(
    const base::DictValue* color_dict) {
  if (!color_dict) {
    return std::nullopt;
  }
  auto hue = color_dict->FindDouble("hue");
  auto sat = color_dict->FindDouble("saturation");
  auto bri = color_dict->FindDouble("brightness");
  if (!hue || !sat || !bri) {
    return std::nullopt;
  }
  return MahoSpaceThemeState::SpaceHSV{
      NormalizeHueToDegrees(*hue),
      static_cast<SkScalar>(std::clamp(*sat, 0.0, 1.0)),
      static_cast<SkScalar>(std::clamp(*bri, 0.0, 1.0))};
}

// Converts an sRGB color stop entry to SpaceHSV.
// Zen gradient stops encode color in a "c" field that is either:
//   - An RGB list: [r, g, b] with values 0-255
//   - A hex string: "#RRGGBB"
// Falls back to hue/saturation/brightness fields if "c" is absent.
std::optional<MahoSpaceThemeState::SpaceHSV> ExtractHSBFromGradientStop(
    const base::DictValue* stop) {
  if (!stop) {
    return std::nullopt;
  }

  const base::Value* c_val = stop->Find("c");
  if (c_val) {
    uint8_t r = 0, g = 0, b = 0;
    bool decoded = false;

    if (c_val->is_list()) {
      const auto& list = c_val->GetList();
      if (list.size() == 3) {
        auto to_u8 = [](const base::Value& v) -> uint8_t {
          int i = v.is_int() ? v.GetInt()
                  : v.is_double() ? static_cast<int>(v.GetDouble())
                  : 0;
          return static_cast<uint8_t>(std::clamp(i, 0, 255));
        };
        r = to_u8(list[0]);
        g = to_u8(list[1]);
        b = to_u8(list[2]);
        decoded = true;
      }
    } else if (c_val->is_string()) {
      const std::string& hex = c_val->GetString();
      if (hex.size() == 7 && hex[0] == '#') {
        unsigned int ri = 0, gi = 0, bi = 0;
        if (base::HexStringToUInt(hex.substr(1, 2), &ri) &&
            base::HexStringToUInt(hex.substr(3, 2), &gi) &&
            base::HexStringToUInt(hex.substr(5, 2), &bi)) {
          r = static_cast<uint8_t>(ri);
          g = static_cast<uint8_t>(gi);
          b = static_cast<uint8_t>(bi);
          decoded = true;
        }
      }
    }

    if (decoded) {
      SkScalar hsv[3];
      SkColorToHSV(SkColorSetRGB(r, g, b), hsv);
      return MahoSpaceThemeState::SpaceHSV{hsv[0], hsv[1], hsv[2]};
    }
  }

  return ExtractHSBFromColorDict(stop);
}

MahoSpaceThemeState::ThemeKind ThemeKindFromType(const std::string& type) {
  if (type == "zen") {
    return MahoSpaceThemeState::ThemeKind::kZen;
  }
  if (type == "gradient") {
    return MahoSpaceThemeState::ThemeKind::kGradient;
  }
  return MahoSpaceThemeState::ThemeKind::kSolid;
}

const base::DictValue* FindDominantStop(const base::ListValue& colors) {
  const base::DictValue* primary = nullptr;
  for (const auto& entry : colors) {
    const auto* d = entry.GetIfDict();
    if (!d) {
      continue;
    }
    if (d->FindBool("isPrimary").value_or(false)) {
      primary = d;
      break;
    }
  }
  if (!primary) {
    for (const auto& entry : colors) {
      const auto* d = entry.GetIfDict();
      if (d) {
        primary = d;
        break;
      }
    }
  }
  return primary;
}

std::optional<MahoSpaceThemeState::SpaceHSV> ExtractHSBForStop(
    const base::DictValue* stop,
    MahoSpaceThemeState::ThemeKind kind) {
  if (kind == MahoSpaceThemeState::ThemeKind::kZen) {
    return ExtractHSBFromGradientStop(stop);
  }
  return ExtractHSBFromColorDict(stop);
}

std::vector<MahoSpaceThemeState::SpaceHSV> ExtractGradientStops(
    const base::ListValue& colors,
    MahoSpaceThemeState::ThemeKind kind) {
  std::vector<MahoSpaceThemeState::SpaceHSV> stops;
  const base::DictValue* dominant = FindDominantStop(colors);
  if (dominant) {
    if (std::optional<MahoSpaceThemeState::SpaceHSV> hsv =
            ExtractHSBForStop(dominant, kind)) {
      stops.push_back(*hsv);
    }
  }
  for (const auto& entry : colors) {
    const base::DictValue* d = entry.GetIfDict();
    if (!d || d == dominant) {
      continue;
    }
    if (std::optional<MahoSpaceThemeState::SpaceHSV> hsv =
            ExtractHSBForStop(d, kind)) {
      stops.push_back(*hsv);
    }
  }
  return stops;
}

MahoSpaceThemeState::ColorScheme ColorSchemeFromString(
    const std::string* scheme) {
  if (scheme && *scheme == "light") {
    return MahoSpaceThemeState::ColorScheme::kLight;
  }
  if (scheme && *scheme == "dark") {
    return MahoSpaceThemeState::ColorScheme::kDark;
  }
  return MahoSpaceThemeState::ColorScheme::kAuto;
}

std::optional<MahoSpaceThemeState::ThemeData> ExtractThemeDataFromThemeDict(
    const base::DictValue* theme_dict) {
  if (!theme_dict) {
    return std::nullopt;
  }
  const std::string* type = theme_dict->FindString("type");
  if (!type) {
    return std::nullopt;
  }

  const MahoSpaceThemeState::ThemeKind kind = ThemeKindFromType(*type);

  if (*type == "solid") {
    const base::DictValue* color_dict = theme_dict->FindDict("color");
    std::optional<MahoSpaceThemeState::SpaceHSV> primary_hsv =
        ExtractHSBFromColorDict(color_dict);
    if (!primary_hsv) {
      return std::nullopt;
    }
    float grain_val = static_cast<float>(std::clamp(
        color_dict ? color_dict->FindDouble("grain").value_or(0.0) : 0.0,
        0.0, 1.0));
    MahoSpaceThemeState::ThemeData theme_data;
    theme_data.kind = kind;
    theme_data.primary_hsv = *primary_hsv;
    theme_data.secondary_hsv = std::nullopt;
    theme_data.stops = {*primary_hsv};
    theme_data.opacity = static_cast<float>(std::clamp(
        theme_dict->FindDouble("opacity").value_or(1.0), 0.0, 1.0));
    theme_data.texture = grain_val;
    theme_data.grain = grain_val;
    return theme_data;
  }

  if (*type == "gradient" || *type == "zen") {
    const auto* colors = theme_dict->FindList("gradientColors");
    if (!colors) {
      return std::nullopt;
    }

    if (colors->empty()) {
      MahoSpaceThemeState::ThemeData theme_data;
      theme_data.kind = kind;
      theme_data.primary_hsv = {0.0f, 0.0f, 0.5f};
      theme_data.secondary_hsv = std::nullopt;
      theme_data.stops = {};
      theme_data.opacity = static_cast<float>(std::clamp(
          theme_dict->FindDouble("opacity").value_or(1.0), 0.0, 1.0));
      theme_data.texture = static_cast<float>(std::clamp(
          theme_dict->FindDouble("texture").value_or(0.0), 0.0, 1.0));
      theme_data.grain = 0.0f;
      theme_data.color_scheme =
          ColorSchemeFromString(theme_dict->FindString("scheme"));
      theme_data.achromatic = true;
      return theme_data;
    }

    const base::DictValue* dominant = FindDominantStop(*colors);
    std::vector<MahoSpaceThemeState::SpaceHSV> stops =
        ExtractGradientStops(*colors, kind);
    if (stops.empty()) {
      return std::nullopt;
    }

    MahoSpaceThemeState::SpaceHSV primary_hsv = stops[0];
    std::optional<MahoSpaceThemeState::SpaceHSV> secondary_hsv;
    if (stops.size() > 1) {
      secondary_hsv = stops[1];
    }

    MahoSpaceThemeState::ThemeData theme_data;
    theme_data.kind = kind;
    theme_data.primary_hsv = primary_hsv;
    theme_data.secondary_hsv = secondary_hsv;
    theme_data.stops = stops;
    theme_data.opacity = static_cast<float>(std::clamp(
        theme_dict->FindDouble("opacity").value_or(1.0), 0.0, 1.0));
    theme_data.texture = static_cast<float>(std::clamp(
        theme_dict->FindDouble("texture").value_or(0.0), 0.0, 1.0));
    theme_data.grain = static_cast<float>(std::clamp(
        dominant ? dominant->FindDouble("grain").value_or(0.0) : 0.0, 0.0,
        1.0));
    theme_data.color_scheme =
        ColorSchemeFromString(theme_dict->FindString("scheme"));
    return theme_data;
  }

  return std::nullopt;
}

MahoSpaceThemeState::ThemeData MakeFallbackThemeData(
    const MahoSpaceThemeState::SpaceHSV& hsv) {
  // Default (theme:None) space: match the theme picker's default opacity so it
  // is translucent like an explicitly-themed space. Range [0.25, 0.9], default
  // 0.5 per resources/maho_space_create/react/types.ts.
  constexpr float kDefaultThemeOpacity = 0.5f;
  MahoSpaceThemeState::ThemeData theme_data;
  theme_data.kind = MahoSpaceThemeState::ThemeKind::kSolid;
  theme_data.primary_hsv = hsv;
  theme_data.secondary_hsv = std::nullopt;
  // Solid invariant: exactly one stop (the primary).
  theme_data.stops = {hsv};
  theme_data.opacity = kDefaultThemeOpacity;
  theme_data.texture = 0.0f;
  theme_data.grain = 0.0f;
  return theme_data;
}

SkColor ApplyAppearanceAdjustments(const MahoSpaceThemeState::SpaceHSV& hsv,
                                   bool is_dark,
                                   float saturation_scale,
                                   float light_value_scale,
                                   uint8_t alpha) {
  SkScalar saturation = hsv.saturation * saturation_scale;
  SkScalar value = hsv.value;
  if (!is_dark) {
    saturation = std::min(saturation * 0.85f, 0.9f);
    value = std::min(value * light_value_scale, 1.0f);
  }
  saturation = std::clamp(saturation, 0.0f, 1.0f);
  value = std::clamp(value, 0.0f, 1.0f);

  SkScalar adjusted_hsv[3] = {hsv.hue, saturation, value};
  return SkHSVToColor(alpha, adjusted_hsv);
}

std::optional<base::flat_map<std::string, MahoSpaceThemeState::ThemeData>>
ParseCommittedThemeSnapshot(const std::string& spaces_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(spaces_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return std::nullopt;
  }

  base::flat_map<std::string, MahoSpaceThemeState::ThemeData> themes;
  for (const auto& item : parsed->GetList()) {
    const base::DictValue* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    if (!id || id->empty()) {
      continue;
    }

    std::optional<MahoSpaceThemeState::ThemeData> theme_data =
        ExtractThemeDataFromThemeDict(dict->FindDict("theme"));
    if (!theme_data) {
      std::optional<MahoSpaceThemeState::SpaceHSV> fallback_hsv =
          ExtractHSBFromColorDict(dict->FindDict("color"));
      if (fallback_hsv) {
        theme_data = MakeFallbackThemeData(*fallback_hsv);
      }
    }
    if (theme_data) {
      themes.insert_or_assign(*id, std::move(*theme_data));
    }
  }
  return themes;
}

MahoSpaceThemeState::ChangedSpaceIds ReplaceCommittedThemeSnapshot(
    const std::string& spaces_json,
    base::flat_map<std::string, MahoSpaceThemeState::ThemeData>* current) {
  std::optional<base::flat_map<std::string, MahoSpaceThemeState::ThemeData>>
      parsed = ParseCommittedThemeSnapshot(spaces_json);
  if (!parsed) {
    return {};
  }

  MahoSpaceThemeState::ChangedSpaceIds changed;
  for (const auto& [space_id, old_theme] : *current) {
    auto new_it = parsed->find(space_id);
    if (new_it == parsed->end() || new_it->second != old_theme) {
      changed.push_back(space_id);
    }
  }
  for (const auto& entry : *parsed) {
    if (!current->contains(entry.first)) {
      changed.push_back(entry.first);
    }
  }
  std::sort(changed.begin(), changed.end());
  *current = std::move(*parsed);
  return changed;
}

}  // namespace

MahoSpaceThemeState::ThemeData::ThemeData() = default;
MahoSpaceThemeState::ThemeData::ThemeData(const ThemeData&) = default;
MahoSpaceThemeState::ThemeData& MahoSpaceThemeState::ThemeData::operator=(
    const ThemeData&) = default;
MahoSpaceThemeState::ThemeData::ThemeData(ThemeData&&) noexcept = default;
MahoSpaceThemeState::ThemeData& MahoSpaceThemeState::ThemeData::operator=(
    ThemeData&&) noexcept = default;
MahoSpaceThemeState::ThemeData::~ThemeData() = default;

MahoSpaceThemeState::ChangedSpaceIds MahoSpaceThemeState::UpdateFromCore() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  MahoCore* core = maho::GetCore();
  if (!core) {
    return {};
  }

  char* json_str = maho_core_get_space_view_models(core);
  if (!json_str) {
    return {};
  }
  std::string json(json_str);
  maho_string_free(json_str);
  return ReplaceCommittedThemeSnapshot(json, committed_themes_.get());
}

MahoSpaceThemeState::ChangedSpaceIds
MahoSpaceThemeState::UpdateFromSnapshotForTesting(
    const std::string& spaces_json) {
  return UpdateFromSnapshot(spaces_json);
}

MahoSpaceThemeState::ChangedSpaceIds
MahoSpaceThemeState::UpdateFromSnapshot(const std::string& spaces_json) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return ReplaceCommittedThemeSnapshot(spaces_json, committed_themes_.get());
}

std::optional<MahoSpaceThemeState::ThemeData>
MahoSpaceThemeState::GetThemeDataForBrowser(Browser* browser) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!browser || !browser->GetProfile() || browser->GetProfile()->IsOffTheRecord()) {
    return std::nullopt;
  }
  auto preview_it = preview_overrides_->find(browser);
  if (preview_it != preview_overrides_->end()) {
    return preview_it->second;
  }
  const std::string& space_id =
      maho::MahoSpaceProfileBridge::GetInstance()->GetActiveSpaceId(browser);
  auto committed_it = committed_themes_->find(space_id);
  return committed_it == committed_themes_->end()
             ? std::nullopt
             : std::optional<ThemeData>(committed_it->second);
}

std::optional<MahoSpaceThemeState::ThemeData>
MahoSpaceThemeState::GetThemeData() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const std::string& space_id =
      maho::MahoSpaceProfileBridge::GetInstance()->GetActiveSpaceId();
  auto committed_it = committed_themes_->find(space_id);
  return committed_it == committed_themes_->end()
             ? std::nullopt
             : std::optional<ThemeData>(committed_it->second);
}

SkColor MahoSpaceThemeState::GetChromeTint(bool is_dark, uint8_t alpha) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const std::optional<ThemeData> effective = GetThemeData();
  if (!effective || effective->achromatic) {
    return SK_ColorTRANSPARENT;
  }

  float saturation_scale = 1.0f;
  float light_value_scale = 1.1f;
  switch (effective->kind) {
    case ThemeKind::kSolid:
      saturation_scale = 1.0f;
      light_value_scale = 1.1f;
      break;
    case ThemeKind::kGradient:
      saturation_scale = 0.82f;
      light_value_scale = 1.06f;
      break;
    case ThemeKind::kZen:
      saturation_scale = 0.72f;
      light_value_scale = 1.03f;
      break;
  }

  return ApplyAppearanceAdjustments(effective->primary_hsv, is_dark,
                                    saturation_scale, light_value_scale,
                                    alpha);
}

void MahoSpaceThemeState::Clear() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  committed_themes_->clear();
  preview_overrides_->clear();
}

bool MahoSpaceThemeState::SetPreviewOverride(Browser* browser,
                                             const std::string& theme_json) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!browser || !browser->GetProfile() || browser->GetProfile()->IsOffTheRecord()) {
    return false;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(theme_json, base::JSON_PARSE_RFC);
  if (!parsed) {
    return false;
  }
  const base::DictValue* theme_dict = parsed->GetIfDict();
  if (!theme_dict) {
    return false;
  }
  std::optional<ThemeData> parsed_theme =
      ExtractThemeDataFromThemeDict(theme_dict);
  if (!parsed_theme) {
    return false;
  }
  preview_overrides_->insert_or_assign(browser, std::move(*parsed_theme));
  return true;
}

void MahoSpaceThemeState::ClearPreviewOverride(Browser* browser) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (browser) {
    preview_overrides_->erase(browser);
  }
}

bool MahoSpaceThemeState::HasPreviewOverride(Browser* browser) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return browser && browser->GetProfile() &&
         !browser->GetProfile()->IsOffTheRecord() &&
         preview_overrides_->contains(browser);
}

size_t MahoSpaceThemeState::GetPreviewOverrideCountForTesting() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return preview_overrides_->size();
}

bool MahoSpaceThemeState::IsOsDarkMode() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return ui::NativeTheme::GetInstanceForNativeUi()->preferred_color_scheme() ==
         ui::NativeTheme::PreferredColorScheme::kDark;
}
