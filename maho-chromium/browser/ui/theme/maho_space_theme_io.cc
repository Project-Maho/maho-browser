// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/theme/maho_space_theme_io.h"

#include <optional>
#include <string>

#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace maho_theme {

namespace {

bool ValidateThemeValue(const base::Value& theme_value) {
  if (!theme_value.is_dict()) {
    return false;
  }

  const auto& dict = theme_value.GetDict();
  const std::string* type = dict.FindString("type");
  if (!type || (*type != "solid" && *type != "gradient")) {
    return false;
  }

  if (*type == "solid") {
    const auto* color_dict = dict.FindDict("color");
    return color_dict && color_dict->FindDouble("hue") &&
           color_dict->FindDouble("saturation") &&
           color_dict->FindDouble("brightness");
  }

  const auto* gradient_colors = dict.FindList("gradientColors");
  if (!gradient_colors || gradient_colors->empty()) {
    return false;
  }
  return dict.FindString("harmony") && dict.FindDouble("opacity") &&
         dict.FindDouble("texture");
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

    base::Value theme_value(theme->Clone());
    std::string theme_json;
    base::JSONWriter::WriteWithOptions(theme_value,
                                       base::JSONWriter::OPTIONS_PRETTY_PRINT,
                                       &theme_json);
    return theme_json;
  }

  return std::nullopt;
}

}  // namespace

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
  base::JSONWriter::Write(event, &event_json);
  char* result = maho_core_handle_event(core, event_json.c_str());
  if (result) {
    maho_string_free(result);
  }
  return true;
}

}  // namespace maho_theme
