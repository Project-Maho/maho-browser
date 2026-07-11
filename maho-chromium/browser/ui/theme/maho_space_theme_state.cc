// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/theme/maho_space_theme_state.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "content/public/browser/browser_thread.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "third_party/skia/include/core/SkColor.h"

std::optional<MahoSpaceThemeState::ThemeData> MahoSpaceThemeState::active_theme_;
std::optional<MahoSpaceThemeState::ThemeData> MahoSpaceThemeState::preview_override_;

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

const base::DictValue* FindSecondaryStop(const base::ListValue& colors,
                                         const base::DictValue* primary) {
  for (const auto& entry : colors) {
    const auto* stop = entry.GetIfDict();
    if (!stop || stop == primary) {
      continue;
    }
    return stop;
  }
  return nullptr;
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
    return MahoSpaceThemeState::ThemeData{
        .kind = kind,
        .primary_hsv = *primary_hsv,
        .secondary_hsv = std::nullopt,
        .opacity = static_cast<float>(std::clamp(
            theme_dict->FindDouble("opacity").value_or(1.0), 0.0, 1.0)),
        .texture = 0.0f,
        .grain = static_cast<float>(std::clamp(
            color_dict ? color_dict->FindDouble("grain").value_or(0.0) : 0.0,
            0.0, 1.0)),
    };
  }

  if (*type == "gradient" || *type == "zen") {
    const auto* colors = theme_dict->FindList("gradientColors");
    if (!colors || colors->empty()) {
      return std::nullopt;
    }

    const base::DictValue* dominant = FindDominantStop(*colors);
    if (!dominant) {
      return std::nullopt;
    }

    std::optional<MahoSpaceThemeState::SpaceHSV> primary_hsv =
        ExtractHSBForStop(dominant, kind);
    if (!primary_hsv) {
      return std::nullopt;
    }

    std::optional<MahoSpaceThemeState::SpaceHSV> secondary_hsv;
    if (const base::DictValue* secondary =
            FindSecondaryStop(*colors, dominant)) {
      secondary_hsv = ExtractHSBForStop(secondary, kind);
    }

    return MahoSpaceThemeState::ThemeData{
        .kind = kind,
        .primary_hsv = *primary_hsv,
        .secondary_hsv = secondary_hsv,
        .opacity = static_cast<float>(std::clamp(
            theme_dict->FindDouble("opacity").value_or(1.0), 0.0, 1.0)),
        .texture = static_cast<float>(std::clamp(
            theme_dict->FindDouble("texture").value_or(0.0), 0.0, 1.0)),
        .grain = static_cast<float>(std::clamp(
            dominant ? dominant->FindDouble("grain").value_or(0.0) : 0.0,
            0.0, 1.0)),
    };
  }

  return std::nullopt;
}

MahoSpaceThemeState::ThemeData MakeFallbackThemeData(
    const MahoSpaceThemeState::SpaceHSV& hsv) {
  return MahoSpaceThemeState::ThemeData{
      .kind = MahoSpaceThemeState::ThemeKind::kSolid,
      .primary_hsv = hsv,
      .secondary_hsv = std::nullopt,
      .opacity = 1.0f,
      .texture = 0.0f,
      .grain = 0.0f,
  };
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

}  // namespace

void MahoSpaceThemeState::UpdateFromCore() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  active_theme_.reset();

  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }

  char* active_id_cstr = maho_core_get_active_space_id(core);
  if (!active_id_cstr) {
    return;
  }
  std::optional<base::Value> parsed_active_id =
      base::JSONReader::Read(active_id_cstr, base::JSON_PARSE_RFC);
  std::string active_id = parsed_active_id && parsed_active_id->is_string()
                              ? parsed_active_id->GetString()
                              : std::string(active_id_cstr);
  maho_string_free(active_id_cstr);

  char* json_str = maho_core_get_space_view_models(core);
  if (!json_str) {
    return;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }

    const std::string* id = dict->FindString("id");
    if (!id || *id != active_id) {
      continue;
    }

    std::optional<ThemeData> theme_data =
        ExtractThemeDataFromThemeDict(dict->FindDict("theme"));

    if (!theme_data) {
      std::optional<SpaceHSV> fallback_hsv =
          ExtractHSBFromColorDict(dict->FindDict("color"));
      if (fallback_hsv) {
        theme_data = MakeFallbackThemeData(*fallback_hsv);
      }
    }

    if (theme_data) {
      active_theme_ = *theme_data;
      DVLOG(1) << "MahoSpaceThemeState updated: active_id=" << active_id
               << " kind=" << static_cast<int>(theme_data->kind)
               << " hue=" << theme_data->primary_hsv.hue
               << " sat=" << theme_data->primary_hsv.saturation
               << " val=" << theme_data->primary_hsv.value
               << " has_secondary=" << theme_data->secondary_hsv.has_value()
               << " opacity=" << theme_data->opacity
               << " texture=" << theme_data->texture;
    }
    return;
  }
}

std::optional<MahoSpaceThemeState::ThemeData>
MahoSpaceThemeState::GetThemeData() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (preview_override_.has_value()) {
    return preview_override_;
  }
  return active_theme_;
}

SkColor MahoSpaceThemeState::GetChromeTint(bool is_dark, uint8_t alpha) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const std::optional<ThemeData>& effective =
      preview_override_.has_value() ? preview_override_ : active_theme_;
  if (!effective) {
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
  active_theme_.reset();
  preview_override_.reset();
}

bool MahoSpaceThemeState::SetPreviewOverride(const std::string& theme_json) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
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
  preview_override_ = std::move(parsed_theme);
  return true;
}

void MahoSpaceThemeState::ClearPreviewOverride() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  preview_override_.reset();
}

bool MahoSpaceThemeState::HasPreviewOverride() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return preview_override_.has_value();
}
