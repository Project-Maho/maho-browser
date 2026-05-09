// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/theme/maho_space_theme_state.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/values.h"
#include "content/public/browser/browser_thread.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "third_party/skia/include/core/SkColor.h"

std::optional<MahoSpaceThemeState::SpaceHSV>
    MahoSpaceThemeState::active_space_hsv_;

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

// Extracts dominant color from a SpaceTheme dict (solid or gradient).
// Solid: uses theme.color
// Gradient: uses the primary gradientColor (isPrimary=true), or first entry.
std::optional<MahoSpaceThemeState::SpaceHSV> ExtractHSBFromThemeDict(
    const base::DictValue* theme_dict) {
  if (!theme_dict) {
    return std::nullopt;
  }
  const std::string* type = theme_dict->FindString("type");
  if (!type) {
    return std::nullopt;
  }

  if (*type == "solid") {
    return ExtractHSBFromColorDict(theme_dict->FindDict("color"));
  }

  if (*type == "gradient") {
    const auto* colors = theme_dict->FindList("gradientColors");
    if (!colors || colors->empty()) {
      return std::nullopt;
    }

    const base::DictValue* primary = nullptr;
    for (const auto& entry : *colors) {
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
      for (const auto& entry : *colors) {
        const auto* d = entry.GetIfDict();
        if (d) {
          primary = d;
          break;
        }
      }
    }
    return ExtractHSBFromColorDict(primary);
  }

  return std::nullopt;
}

}  // namespace

void MahoSpaceThemeState::UpdateFromCore() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  active_space_hsv_.reset();

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

    auto hsv = ExtractHSBFromThemeDict(dict->FindDict("theme"));

    if (!hsv) {
      hsv = ExtractHSBFromColorDict(dict->FindDict("color"));
    }

    if (hsv) {
      active_space_hsv_ = *hsv;
      DVLOG(1) << "MahoSpaceThemeState updated: active_id=" << active_id
               << " hue=" << hsv->hue << " sat=" << hsv->saturation
               << " val=" << hsv->value;
    }
    return;
  }
}

SkColor MahoSpaceThemeState::GetChromeTint(bool is_dark, uint8_t alpha) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!active_space_hsv_) {
    return SK_ColorTRANSPARENT;
  }

  SkScalar hue = active_space_hsv_->hue;
  SkScalar saturation = active_space_hsv_->saturation;
  SkScalar value = active_space_hsv_->value;
  if (!is_dark) {
    saturation = std::min(saturation * 0.85f, 0.9f);
    value = std::min(value * 1.1f, 1.0f);
  }

  SkScalar hsv[3] = {hue, saturation, value};
  return SkHSVToColor(alpha, hsv);
}

void MahoSpaceThemeState::Clear() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  active_space_hsv_.reset();
}
