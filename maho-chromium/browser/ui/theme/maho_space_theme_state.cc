// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/theme/maho_space_theme_state.h"

#include <algorithm>
#include <optional>
#include <string>

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "content/public/browser/browser_thread.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "third_party/skia/include/core/SkColor.h"

std::optional<MahoSpaceThemeState::SpaceHSV>
    MahoSpaceThemeState::active_space_hsv_;

namespace {

SkColor ParseHexColor(const std::string& hex) {
  if (hex.empty() || hex[0] != '#') {
    return SK_ColorTRANSPARENT;
  }

  if (hex.size() == 7) {
    uint32_t rgb = 0;
    if (!base::HexStringToUInt(hex.substr(1), &rgb)) {
      return SK_ColorTRANSPARENT;
    }
    return SkColorSetRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
  }

  if (hex.size() == 4) {
    std::string expanded;
    expanded.reserve(6);
    for (size_t i = 1; i < 4; ++i) {
      expanded.push_back(hex[i]);
      expanded.push_back(hex[i]);
    }

    uint32_t rgb = 0;
    if (!base::HexStringToUInt(expanded, &rgb)) {
      return SK_ColorTRANSPARENT;
    }
    return SkColorSetRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
  }

  return SK_ColorTRANSPARENT;
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
    const std::string* color = dict->FindString("color");
    if (!id || *id != active_id || !color) {
      continue;
    }

    const SkColor rgb = ParseHexColor(*color);
    if (rgb == SK_ColorTRANSPARENT) {
      return;
    }

    SkScalar hsv[3];
    SkColorToHSV(rgb, hsv);
    active_space_hsv_ = SpaceHSV{hsv[0], hsv[1], hsv[2]};
    DVLOG(1) << "MahoSpaceThemeState updated: active_id=" << active_id
             << " hue=" << hsv[0] << " sat=" << hsv[1]
             << " val=" << hsv[2];
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
