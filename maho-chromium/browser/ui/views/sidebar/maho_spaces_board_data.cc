// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_spaces_board_data.h"

#include <optional>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/string_escape.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "base/json/json_writer.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "third_party/skia/include/core/SkScalar.h"
#include "url/gurl.h"

namespace maho {

SpaceBoardTabItem::SpaceBoardTabItem() = default;
SpaceBoardTabItem::SpaceBoardTabItem(const SpaceBoardTabItem&) = default;
SpaceBoardTabItem::SpaceBoardTabItem(SpaceBoardTabItem&&) = default;
SpaceBoardTabItem& SpaceBoardTabItem::operator=(const SpaceBoardTabItem&) = default;
SpaceBoardTabItem& SpaceBoardTabItem::operator=(SpaceBoardTabItem&&) = default;
SpaceBoardTabItem::~SpaceBoardTabItem() = default;

SpaceBoardColumn::SpaceBoardColumn() = default;
SpaceBoardColumn::SpaceBoardColumn(const SpaceBoardColumn&) = default;
SpaceBoardColumn::SpaceBoardColumn(SpaceBoardColumn&&) = default;
SpaceBoardColumn& SpaceBoardColumn::operator=(const SpaceBoardColumn&) = default;
SpaceBoardColumn& SpaceBoardColumn::operator=(SpaceBoardColumn&&) = default;
SpaceBoardColumn::~SpaceBoardColumn() = default;

SpaceBoardModel::SpaceBoardModel() = default;
SpaceBoardModel::SpaceBoardModel(const SpaceBoardModel&) = default;
SpaceBoardModel::SpaceBoardModel(SpaceBoardModel&&) = default;
SpaceBoardModel& SpaceBoardModel::operator=(const SpaceBoardModel&) = default;
SpaceBoardModel& SpaceBoardModel::operator=(SpaceBoardModel&&) = default;
SpaceBoardModel::~SpaceBoardModel() = default;

namespace {

double NormalizeHueToDegrees(double hue) {
  if (std::abs(hue) <= 1.0) {
    hue *= 360.0;
  }
  hue = std::fmod(hue, 360.0);
  if (hue < 0.0) {
    hue += 360.0;
  }
  return hue;
}

std::string HexColorFromSkColor(SkColor color) {
  std::array<char, 8> buffer = {};
  snprintf(buffer.data(), buffer.size(), "#%02X%02X%02X", SkColorGetR(color),
           SkColorGetG(color), SkColorGetB(color));
  return std::string(buffer.data());
}

std::optional<std::string> TryHexColorFromRgbList(const base::Value* value) {
  const auto* list = value ? value->GetIfList() : nullptr;
  if (!list || list->size() != 3) {
    return std::nullopt;
  }
  auto to_channel = [](const base::Value& channel) -> std::optional<int> {
    if (channel.is_int()) {
      return std::clamp(channel.GetInt(), 0, 255);
    }
    if (channel.is_double()) {
      return std::clamp(static_cast<int>(channel.GetDouble()), 0, 255);
    }
    return std::nullopt;
  };
  const std::optional<int> r = to_channel((*list)[0]);
  const std::optional<int> g = to_channel((*list)[1]);
  const std::optional<int> b = to_channel((*list)[2]);
  if (!r || !g || !b) {
    return std::nullopt;
  }
  return HexColorFromSkColor(SkColorSetRGB(*r, *g, *b));
}

std::optional<std::string> TryHexColorFromColorDict(const base::DictValue* dict) {
  if (!dict) {
    return std::nullopt;
  }
  const std::string* raw_hex = dict->FindString("c");
  if (raw_hex && raw_hex->size() == 7 && (*raw_hex)[0] == '#') {
    return *raw_hex;
  }
  if (const base::Value* raw_rgb = dict->Find("c")) {
    if (std::optional<std::string> from_rgb = TryHexColorFromRgbList(raw_rgb)) {
      return from_rgb;
    }
  }

  const std::optional<double> hue = dict->FindDouble("hue");
  const std::optional<double> saturation = dict->FindDouble("saturation");
  const std::optional<double> brightness = dict->FindDouble("brightness");
  if (!hue || !saturation || !brightness) {
    return std::nullopt;
  }
  SkScalar hsv[3] = {
      static_cast<SkScalar>(NormalizeHueToDegrees(*hue)),
      static_cast<SkScalar>(std::clamp(*saturation, 0.0, 1.0)),
      static_cast<SkScalar>(std::clamp(*brightness, 0.0, 1.0))};
  return HexColorFromSkColor(SkHSVToColor(hsv));
}

void MaybeAppendPaletteColor(std::vector<std::string>* palette,
                             const std::optional<std::string>& color) {
  if (!palette || !color || color->empty()) {
    return;
  }
  if (std::find(palette->begin(), palette->end(), *color) == palette->end()) {
    palette->push_back(*color);
  }
}

SpaceBoardTabItem ParseTabItem(const base::DictValue& dict) {
  SpaceBoardTabItem item;

  if (const std::string* id = dict.FindString("id")) {
    item.id = *id;
  }

  if (const std::string* custom_title = dict.FindString("customTitle");
      custom_title && !custom_title->empty()) {
    item.title = base::UTF8ToUTF16(*custom_title);
  } else if (const std::string* title = dict.FindString("title")) {
    item.title = base::UTF8ToUTF16(*title);
  }

  if (const std::string* url = dict.FindString("url")) {
    item.url = *url;
  }

  item.is_pinned = dict.FindBool("isPinned").value_or(false);
  item.is_loading = dict.FindBool("isLoading").value_or(false);
  item.is_favorite = dict.FindBool("isFavorite").value_or(false);  // L3-EXEMPT: legacy compat
  item.is_muted = dict.FindBool("isMuted").value_or(false);
  item.is_playing_audio = dict.FindBool("isPlayingAudio").value_or(false);

  return item;
}

std::vector<SpaceBoardTabItem> FetchTabsForSpace(MahoCore* core,
                                                  const std::string& space_id) {
  std::vector<SpaceBoardTabItem> tabs;

  const std::string space_id_json = base::GetQuotedJSONString(space_id);
  char* json_str = maho_core_get_space_tabs(core, space_id_json.c_str(), nullptr);
  if (!json_str) {
    return tabs;
  }

  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return tabs;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    SpaceBoardTabItem tab = ParseTabItem(*dict);
    if (!tab.id.empty()) {
      tabs.push_back(std::move(tab));
    }
  }

  return tabs;
}

std::string FetchActiveSpaceId(MahoCore* core) {
  char* json_str = maho_core_get_active_space_id(core);
  if (!json_str) {
    return std::string();
  }
  std::string raw(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(raw, base::JSON_PARSE_RFC);
  if (parsed && parsed->is_string()) {
    return parsed->GetString();
  }
  return raw;
}

}  // namespace

SpaceBoardModel BuildSpacesBoardModel() {
  SpaceBoardModel model;
  MahoCore* core = maho::GetCore();
  if (!core) {
    return model;
  }

  model.active_space_id = FetchActiveSpaceId(core);

  char* spaces_json_str = maho_core_get_space_view_models(core);
  if (!spaces_json_str) {
    return model;
  }

  std::string spaces_json(spaces_json_str);
  maho_string_free(spaces_json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(spaces_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return model;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }

    const std::string* id = dict->FindString("id");
    if (!id || id->empty()) {
      continue;
    }

    SpaceBoardColumn column;
    column.space_id = *id;

    if (const std::string* name = dict->FindString("name")) {
      column.name = base::UTF8ToUTF16(*name);
    }
    column.theme = BuildMahoSpaceDisplayThemeModel(*dict);
    if (const std::string* color = dict->FindString("color")) {
      column.color = *color;
    }
    column.color = GetMahoSpaceDisplayPrimaryColor(column.theme, column.color);
    if (const std::string* icon = dict->FindString("icon")) {
      column.icon = *icon;
    }
    column.is_active = dict->FindBool("isActive").value_or(false);
    if (auto tab_count = dict->FindInt("tabCount")) {
      column.tab_count = *tab_count;
    }

    column.tabs = FetchTabsForSpace(core, column.space_id);
    model.columns.push_back(std::move(column));
  }

  return model;
}

std::u16string SpaceBoardTabCompactTitle(const SpaceBoardTabItem& tab) {
  if (!tab.title.empty()) {
    return tab.title;
  }
  if (!tab.url.empty()) {
    const GURL gurl(tab.url);
    if (gurl.is_valid() && !gurl.host().empty()) {
      return base::UTF8ToUTF16(gurl.host());
    }
    return base::UTF8ToUTF16(tab.url);
  }
  return u"Untitled";
}

std::u16string SpaceBoardTabSubtitle(const SpaceBoardTabItem& tab) {
  if (tab.url.empty()) {
    return std::u16string();
  }
  const GURL gurl(tab.url);
  if (gurl.is_valid() && !gurl.host().empty()) {
    return base::UTF8ToUTF16(gurl.host());
  }
  return base::UTF8ToUTF16(tab.url);
}

std::u16string SpaceBoardColumnBadge(const SpaceBoardColumn& column) {
  if (!column.icon.empty()) {
    return base::UTF8ToUTF16(column.icon);
  }
  for (char16_t ch : column.name) {
    if (base::IsAsciiAlpha(ch)) {
      return std::u16string(1, base::ToUpperASCII(ch));
    }
    if (base::IsAsciiDigit(ch)) {
      return std::u16string(1, ch);
    }
  }
  return u"S";
}

MahoSpaceDisplayThemeModel::MahoSpaceDisplayThemeModel() = default;
MahoSpaceDisplayThemeModel::MahoSpaceDisplayThemeModel(
    const MahoSpaceDisplayThemeModel&) = default;
MahoSpaceDisplayThemeModel::MahoSpaceDisplayThemeModel(
    MahoSpaceDisplayThemeModel&&) = default;
MahoSpaceDisplayThemeModel& MahoSpaceDisplayThemeModel::operator=(
    const MahoSpaceDisplayThemeModel&) = default;
MahoSpaceDisplayThemeModel& MahoSpaceDisplayThemeModel::operator=(
    MahoSpaceDisplayThemeModel&&) = default;
MahoSpaceDisplayThemeModel::~MahoSpaceDisplayThemeModel() = default;

MahoSpaceDisplayThemeModel BuildMahoSpaceDisplayThemeModel(
    const base::DictValue& space_dict) {
  MahoSpaceDisplayThemeModel model;
  const base::DictValue* theme_dict = space_dict.FindDict("theme");
  if (!theme_dict) {
    return model;
  }

  model.has_theme = true;
  if (const std::string* type = theme_dict->FindString("type")) {
    if (*type == "solid") {
      model.kind = MahoSpaceDisplayThemeKind::kSolid;
    } else if (*type == "gradient") {
      model.kind = MahoSpaceDisplayThemeKind::kGradient;
    } else if (*type == "zen") {
      model.kind = MahoSpaceDisplayThemeKind::kZen;
    }
  }
  model.opacity = static_cast<float>(
      std::clamp(theme_dict->FindDouble("opacity").value_or(1.0), 0.0, 1.0));
  model.texture = static_cast<float>(
      std::clamp(theme_dict->FindDouble("texture").value_or(0.0), 0.0, 1.0));

  if (const base::DictValue* color_dict = theme_dict->FindDict("color")) {
    model.grain = static_cast<float>(
        std::clamp(color_dict->FindDouble("grain").value_or(0.0), 0.0, 1.0));
    model.primary_color = TryHexColorFromColorDict(color_dict).value_or(std::string());
    MaybeAppendPaletteColor(&model.palette_colors, model.primary_color);
  }

  if (const base::ListValue* colors = theme_dict->FindList("gradientColors")) {
    for (const auto& entry : *colors) {
      const base::DictValue* color_dict = entry.GetIfDict();
      if (!color_dict) {
        continue;
      }
      std::optional<std::string> hex = TryHexColorFromColorDict(color_dict);
      if (!hex || hex->empty()) {
        continue;
      }
      MaybeAppendPaletteColor(&model.palette_colors, hex);
      if (color_dict->FindBool("isPrimary").value_or(false)) {
        model.primary_color = *hex;
      } else if (model.secondary_color.empty()) {
        model.secondary_color = *hex;
      }
    }
  }

  if (model.primary_color.empty() && !model.palette_colors.empty()) {
    model.primary_color = model.palette_colors.front();
  }
  if (model.secondary_color.empty() && model.palette_colors.size() > 1u) {
    model.secondary_color = model.palette_colors[1];
  }

  base::JSONWriter::Write(base::Value(theme_dict->Clone()), &model.theme_json);
  return model;
}

std::string GetMahoSpaceDisplayPrimaryColor(
    const MahoSpaceDisplayThemeModel& theme,
    const std::string& fallback_color) {
  return theme.primary_color.empty() ? fallback_color : theme.primary_color;
}

std::string GetMahoSpaceDisplaySecondaryColor(
    const MahoSpaceDisplayThemeModel& theme,
    const std::string& fallback_color) {
  if (!theme.secondary_color.empty()) {
    return theme.secondary_color;
  }
  if (!theme.palette_colors.empty()) {
    return theme.palette_colors.back();
  }
  return GetMahoSpaceDisplayPrimaryColor(theme, fallback_color);
}

}  // namespace maho
