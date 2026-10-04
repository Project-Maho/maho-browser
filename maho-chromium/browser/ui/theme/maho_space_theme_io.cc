// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/theme/maho_space_theme_io.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"  // nogncheck
#include "maho/third_party/maho/maho_ffi.h"
#include "third_party/skia/include/core/SkColor.h"

namespace maho_theme {

namespace {

SkScalar NormalizeHueToDegrees(double hue) {
  if (hue <= 1.0) {
    hue *= 360.0;
  }
  hue = std::fmod(hue, 360.0);
  if (hue < 0.0) {
    hue += 360.0;
  }
  return static_cast<SkScalar>(hue);
}

bool ValidateColorStop(const base::DictValue& entry) {
  std::optional<bool> is_primary = entry.FindBool("isPrimary");
  std::optional<bool> is_custom = entry.FindBool("isCustom");
  if (!is_primary.has_value() || !is_custom.has_value()) {
    return false;
  }

  bool has_hsb = entry.FindDouble("hue") && entry.FindDouble("saturation") &&
                 entry.FindDouble("brightness");
  const base::Value* c_val = entry.Find("c");
  if (!has_hsb && !c_val) {
    return false;
  }
  if (c_val) {
    if (c_val->is_string()) {
      if (c_val->GetString().empty()) {
        return false;
      }
    } else if (c_val->is_list()) {
      const auto& c_list = c_val->GetList();
      if (c_list.size() != 3) {
        return false;
      }
      for (const auto& elem : c_list) {
        if (!elem.is_int() && !elem.is_double()) {
          return false;
        }
      }
    } else {
      return false;
    }
  }
  return true;
}

static constexpr const char* kKnownHarmonies[] = {
    "single_analogous", "analogous",       "complementary",
    "triadic",          "split_complementary", "custom",
    "floating",
};

static constexpr const char* kKnownSchemes[] = {"auto", "light", "dark"};

bool ValidateHarmony(const base::DictValue& dict) {
  const std::string* harmony = dict.FindString("harmony");
  if (!harmony) {
    return true;
  }
  bool known = false;
  for (const char* h : kKnownHarmonies) {
    if (*harmony == h) {
      known = true;
      break;
    }
  }
  if (!known) {
    return false;
  }
  if (*harmony == "floating") {
    const std::string* scheme = dict.FindString("scheme");
    if (!scheme) {
      return false;
    }
    bool valid_scheme = false;
    for (const char* s : kKnownSchemes) {
      if (*scheme == s) {
        valid_scheme = true;
        break;
      }
    }
    if (!valid_scheme) {
      return false;
    }
  }
  return true;
}

bool ValidateThemeValue(const base::Value& theme_value) {
  if (!theme_value.is_dict()) {
    return false;
  }

  const auto& dict = theme_value.GetDict();
  const std::string* type = dict.FindString("type");
  if (!type ||
      (*type != "solid" && *type != "gradient" && *type != "zen")) {
    return false;
  }

  if (*type == "solid") {
    const auto* color_dict = dict.FindDict("color");
    return color_dict && color_dict->FindDouble("hue") &&
           color_dict->FindDouble("saturation") &&
           color_dict->FindDouble("brightness") &&
           color_dict->FindDouble("grain");
  }

  const auto* gradient_colors = dict.FindList("gradientColors");
  if (!gradient_colors || gradient_colors->empty()) {
    return false;
  }

  for (const auto& stop : *gradient_colors) {
    const auto* entry = stop.GetIfDict();
    if (!entry || !ValidateColorStop(*entry)) {
      return false;
    }
  }

  if (!dict.FindDouble("opacity") || !dict.FindDouble("texture")) {
    return false;
  }

  return ValidateHarmony(dict);
}

std::optional<std::string> ValidateThemeJson(const std::string& contents) {
  std::optional<base::Value> theme_value =
      base::JSONReader::Read(contents, base::JSON_PARSE_RFC);
  if (!theme_value || !ValidateThemeValue(*theme_value)) {
    return std::nullopt;
  }
  return contents;
}

std::optional<std::string> GetSpaceThemeJson(MahoCore* core,
                                            const std::string& space_id) {
  if (!core) {
    return std::nullopt;
  }

  char* json_str = maho_core_get_space_view_models(core);
  if (!json_str) {
    return std::nullopt;
  }

  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return std::nullopt;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    if (!id || *id != space_id) {
      continue;
    }
    const auto* theme = dict->FindDict("theme");
    if (!theme) {
      return std::nullopt;
    }

    return SerializeSpaceThemeJson(*theme);
  }

  return std::nullopt;
}

}  // namespace

std::optional<std::string> SpaceColorDictToHex(
    const base::DictValue& color) {
  const std::optional<double> hue = color.FindDouble("hue");
  const std::optional<double> saturation = color.FindDouble("saturation");
  const std::optional<double> brightness = color.FindDouble("brightness");
  if (!hue || !saturation || !brightness) {
    return std::nullopt;
  }

  const SkScalar hsv[] = {
      NormalizeHueToDegrees(*hue),
      static_cast<SkScalar>(std::clamp(*saturation, 0.0, 1.0)),
      static_cast<SkScalar>(std::clamp(*brightness, 0.0, 1.0)),
  };
  const SkColor color_value = SkHSVToColor(hsv);
  return base::StringPrintf("#%02X%02X%02X", SkColorGetR(color_value),
                            SkColorGetG(color_value),
                            SkColorGetB(color_value));
}

std::optional<std::string> SerializeSpaceThemeJson(
    const base::DictValue& theme) {
  std::string theme_json;
  if (!base::JSONWriter::WriteWithOptions(
          base::Value(theme.Clone()), base::JSONWriter::OPTIONS_PRETTY_PRINT,
          &theme_json)) {
    return std::nullopt;
  }
  return theme_json;
}

std::optional<std::string> SerializeSpaceThemeJson(MahoCore* core,
                                                   const std::string& space_id) {
  return GetSpaceThemeJson(core, space_id);
}

bool ExportSpaceThemeToFile(MahoCore* core,
                            const std::string& space_id,
                            const base::FilePath& path) {
  std::optional<std::string> theme_json = GetSpaceThemeJson(core, space_id);
  if (!theme_json) {
    return false;
  }
  return base::WriteFile(path, *theme_json);
}

std::optional<std::string> ReadValidatedThemeJson(const base::FilePath& path) {
  std::string contents;
  if (!base::ReadFileToString(path, &contents)) {
    return std::nullopt;
  }
  return ValidateThemeJson(contents);
}

bool ApplyThemeJsonToSpace(MahoCore* core,
                           const std::string& space_id,
                           const std::string& theme_json) {
  if (!core) {
    return false;
  }

  std::optional<base::Value> theme_value =
      base::JSONReader::Read(theme_json, base::JSON_PARSE_RFC);
  if (!theme_value || !ValidateThemeValue(*theme_value)) {
    return false;
  }

  base::DictValue changes;
  changes.Set("spaceId", space_id);
  changes.Set("theme", std::move(*theme_value));
  base::DictValue event;
  event.Set("kind", "update_space_config");
  event.Set("changes", std::move(changes));
  std::string event_json;
  base::JSONWriter::Write(base::Value(std::move(event)), &event_json);
  char* result = maho_core_handle_event(core, event_json.c_str());
  if (result) {
    std::string updates(result);
    maho_string_free(result);
    maho::InvalidateSidebarCoreCacheForUpdatesJson(updates);
  }
  // Persist to disk so the theme (incl. opacity) survives restart; otherwise
  // update_space_config only mutates in-memory core state and is lost on relaunch.
  maho_core_save_state(core);
  return true;
}

}  // namespace maho_theme
