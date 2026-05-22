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
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
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
  item.is_favorite = dict.FindBool("isFavorite").value_or(false);
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

}  // namespace maho
