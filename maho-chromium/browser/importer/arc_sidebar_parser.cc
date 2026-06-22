// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/importer/arc_sidebar_parser.h"

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "url/gurl.h"

namespace maho::importer {

ArcParseResult::ArcParseResult() = default;
ArcParseResult::~ArcParseResult() = default;
ArcParseResult::ArcParseResult(ArcParseResult&&) = default;
ArcParseResult& ArcParseResult::operator=(ArcParseResult&&) = default;

namespace {

const base::DictValue* UnwrapValue(const base::Value& val) {
  const auto* dict = val.GetIfDict();
  if (!dict) return nullptr;
  const auto* inner = dict->FindDict("value");
  return inner ? inner : dict;
}

// Arc alternating array: [uuid, obj, uuid, obj, ...]
std::vector<const base::DictValue*> ParseAlternatingArray(
    const base::ListValue* list) {
  std::vector<const base::DictValue*> result;
  if (!list) return result;
  for (size_t i = 1; i < list->size(); i += 2) {
    const auto* unwrapped = UnwrapValue((*list)[i]);
    if (unwrapped) {
      result.push_back(unwrapped);
    }
  }
  return result;
}

const base::ListValue* FindChildrenIds(const base::DictValue& item) {
  const auto* direct = item.FindList("childrenIds");
  if (direct) return direct;

  const auto* wrapped_0 = item.FindDict("childrenIds");
  if (wrapped_0) {
    const auto* inner = wrapped_0->FindList("_0");
    if (inner) return inner;
    const auto* items_list = wrapped_0->FindList("items");
    if (items_list) return items_list;
  }
  return nullptr;
}

bool IsTabItem(const base::DictValue& item) {
  return item.FindDict("data") && item.FindDict("data")->FindDict("tab");
}

bool IsFolderItem(const base::DictValue& item) {
  if (!item.FindDict("data")) return false;
  const auto* data = item.FindDict("data");
  return data->FindDict("list") || data->FindDict("itemContainer");
}

std::string ExtractTabUrl(const base::DictValue& item) {
  const auto* data = item.FindDict("data");
  if (!data) return std::string();
  const auto* tab = data->FindDict("tab");
  if (!tab) return std::string();
  const std::string* url = tab->FindString("savedURL");
  return url ? *url : std::string();
}

std::string ExtractTabTitle(const base::DictValue& item) {
  const auto* data = item.FindDict("data");
  if (!data) return std::string();
  const auto* tab = data->FindDict("tab");
  if (!tab) return std::string();
  const std::string* title = tab->FindString("savedTitle");
  return title ? *title : std::string();
}

std::string ExtractItemTitle(const base::DictValue& item) {
  const std::string* title = item.FindString("title");
  return title ? *title : std::string();
}

void ParseItemsIntoWorkspace(
    ParsedWorkspace& ws,
    const std::vector<const base::DictValue*>& all_items,
    const base::ListValue* container_ids,
    bool is_pinned_section) {
  if (!container_ids) return;

  for (size_t i = 1; i < container_ids->size(); i += 2) {
    const auto& id_val = (*container_ids)[i];
    if (!id_val.is_string()) continue;
    const std::string& item_id = id_val.GetString();

    for (const auto* item : all_items) {
      const std::string* iid = item->FindString("id");
      if (!iid || *iid != item_id) continue;

      if (IsTabItem(*item)) {
        ParsedTab tab;
        tab.url = ExtractTabUrl(*item);
        tab.title = ExtractTabTitle(*item);
        if (tab.url.empty()) break;
        GURL gurl(tab.url);
        if (!gurl.is_valid() || !gurl.SchemeIsHTTPOrHTTPS()) {
          LOG(WARNING) << "Rejecting invalid or non-HTTP(S) Arc sidebar item URL";
          break;
        }
        tab.is_pinned = is_pinned_section;
        if (is_pinned_section) {
          ws.pinned_tabs.push_back(std::move(tab));
        } else {
          ws.regular_tabs.push_back(std::move(tab));
        }
      } else if (IsFolderItem(*item)) {
        ParsedFolder folder;
        folder.id = *iid;
        folder.name = ExtractItemTitle(*item);
        folder.workspace_id = ws.uuid;
        ws.folders.push_back(std::move(folder));
      }
      break;
    }
  }
}

}  // namespace

ArcParseResult ParseArcSidebar(const base::FilePath& arc_support_dir) {
  ArcParseResult result;

  base::FilePath sidebar_path =
      arc_support_dir.Append(FILE_PATH_LITERAL("StorableSidebar.json"));

  std::string content;
  if (!base::ReadFileToString(sidebar_path, &content) || content.empty()) {
    return result;
  }

  auto parsed = base::JSONReader::Read(content, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return result;
  }

  const auto& root = parsed->GetDict();

  // Path B (preferred): firebaseSyncState.syncData
  const auto* sync_data = root.FindDictByDottedPath(
      "firebaseSyncState.syncData");

  const base::ListValue* space_models = nullptr;
  const base::ListValue* items_list = nullptr;

  if (sync_data) {
    space_models = sync_data->FindList("spaceModels");
    items_list = sync_data->FindList("items");
  }

  // Path A (fallback): sidebar.containers[1]
  if (!space_models) {
    const auto* containers = root.FindListByDottedPath("sidebar.containers");
    if (containers && containers->size() > 1) {
      const auto* container = (*containers)[1].GetIfDict();
      if (container) {
        space_models = container->FindList("spaces");
        items_list = container->FindList("items");
      }
    }
  }

  if (!space_models || !items_list) {
    return result;
  }

  auto all_items = ParseAlternatingArray(items_list);

  auto spaces = ParseAlternatingArray(space_models);
  for (const auto* space_dict : spaces) {
    ParsedWorkspace ws;
    const std::string* title = space_dict->FindString("title");
    ws.name = title ? *title : "Space";
    ws.uuid = std::to_string(result.workspaces.size());

    const auto* custom_info = space_dict->FindDict("customInfo");
    if (custom_info) {
      const auto* icon_type = custom_info->FindDict("iconType");
      if (icon_type) {
        const std::string* emoji = icon_type->FindString("emoji_v2");
        if (emoji) {
          ws.icon = *emoji;
        } else {
          const std::string* icon_name = icon_type->FindString("icon");
          if (icon_name) ws.icon = *icon_name;
        }
      }
    }

    // Parse containerIDs: [type, uuid, type, uuid, ...]
    const auto* container_ids = space_dict->FindList("containerIDs");
    if (container_ids) {
      for (size_t i = 0; i + 1 < container_ids->size(); i += 2) {
        const auto& type_val = (*container_ids)[i];
        const auto& uuid_val = (*container_ids)[i + 1];
        if (!type_val.is_string() || !uuid_val.is_string()) continue;

        const std::string& type = type_val.GetString();
        bool is_pinned = (type == "pinned");

        for (const auto* item : all_items) {
          const std::string* iid = item->FindString("id");
          if (!iid || *iid != uuid_val.GetString()) continue;

          const auto* children = FindChildrenIds(*item);
          ParseItemsIntoWorkspace(ws, all_items, children, is_pinned);
          break;
        }
      }
    }

    result.workspaces.push_back(std::move(ws));
  }

  return result;
}

}  // namespace maho::importer
