// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/importer/zen_workspaces_parser.h"

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "maho/browser/importer/mozlz4_decompressor.h"

namespace maho::importer {

ParsedTab::ParsedTab() = default;
ParsedTab::~ParsedTab() = default;
ParsedTab::ParsedTab(ParsedTab&&) = default;
ParsedTab& ParsedTab::operator=(ParsedTab&&) = default;

ParsedFolder::ParsedFolder() = default;
ParsedFolder::~ParsedFolder() = default;
ParsedFolder::ParsedFolder(ParsedFolder&&) = default;
ParsedFolder& ParsedFolder::operator=(ParsedFolder&&) = default;

ParsedWorkspace::ParsedWorkspace() = default;
ParsedWorkspace::~ParsedWorkspace() = default;
ParsedWorkspace::ParsedWorkspace(ParsedWorkspace&&) = default;
ParsedWorkspace& ParsedWorkspace::operator=(ParsedWorkspace&&) = default;

ZenParseResult::ZenParseResult() = default;
ZenParseResult::~ZenParseResult() = default;
ZenParseResult::ZenParseResult(ZenParseResult&&) = default;
ZenParseResult& ZenParseResult::operator=(ZenParseResult&&) = default;

namespace {

std::string ExtractUrlFromTab(const base::DictValue& tab) {
  const auto* entries = tab.FindList("entries");
  if (!entries || entries->empty()) {
    return std::string();
  }
  const auto* last_entry = entries->back().GetIfDict();
  if (!last_entry) {
    return std::string();
  }
  const std::string* url = last_entry->FindString("url");
  return url ? *url : std::string();
}

std::string ExtractTitleFromTab(const base::DictValue& tab) {
  const auto* entries = tab.FindList("entries");
  if (!entries || entries->empty()) {
    return std::string();
  }
  const auto* last_entry = entries->back().GetIfDict();
  if (!last_entry) {
    return std::string();
  }
  const std::string* title = last_entry->FindString("title");
  return title ? *title : std::string();
}

std::string ExtractThemeColor(const base::DictValue& space) {
  const auto* theme = space.FindDict("theme");
  if (!theme) {
    return std::string();
  }
  const auto* colors = theme->FindList("gradientColors");
  if (!colors || colors->empty()) {
    return std::string();
  }
  const auto* first_color = colors->front().GetIfDict();
  if (!first_color) {
    return std::string();
  }
  const std::string* hex = first_color->FindString("color");
  return hex ? *hex : std::string();
}

std::string ReadActiveWorkspaceFromPrefs(const base::FilePath& profile_dir) {
  base::FilePath prefs_path = profile_dir.Append(FILE_PATH_LITERAL("prefs.js"));
  std::string prefs_content;
  if (!base::ReadFileToString(prefs_path, &prefs_content)) {
    return std::string();
  }

  constexpr char kPrefKey[] = "\"zen.workspaces.active\", \"";
  size_t pos = prefs_content.find(kPrefKey);
  if (pos == std::string::npos) {
    return std::string();
  }
  pos += strlen(kPrefKey);
  size_t end = prefs_content.find('"', pos);
  if (end == std::string::npos) {
    return std::string();
  }
  return prefs_content.substr(pos, end - pos);
}

}  // namespace

ZenParseResult ParseZenWorkspaces(const base::FilePath& profile_dir) {
  ZenParseResult result;

  base::FilePath sessions_path =
      profile_dir.Append(FILE_PATH_LITERAL("zen-sessions.jsonlz4"));
  auto json_str = DecompressMozLz4(sessions_path);
  if (!json_str.has_value()) {
    LOG(ERROR) << "Failed to decompress zen-sessions.jsonlz4 at " << sessions_path.value();
    result.status = ZenParseStatus::kDecompressFailed;
    return result;
  }

  auto parsed = base::JSONReader::Read(*json_str, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    LOG(ERROR) << "Failed to parse Zen workspaces JSON (malformed or not a dictionary)";
    result.status = ZenParseStatus::kJsonParseFailed;
    return result;
  }

  const auto& root = parsed->GetDict();
  const auto* spaces_list = root.FindList("spaces");
  const auto* tabs_list = root.FindList("tabs");
  const auto* folders_list = root.FindList("folders");

  if (!spaces_list || !tabs_list) {
    LOG(ERROR) << "Zen workspaces JSON missing required 'spaces' or 'tabs' list";
    result.status = ZenParseStatus::kMissingRequiredFields;
    return result;
  }

  for (const auto& space_val : *spaces_list) {
    const auto* space_dict = space_val.GetIfDict();
    if (!space_dict) continue;

    const std::string* uuid = space_dict->FindString("uuid");
    const std::string* name = space_dict->FindString("name");
    if (!uuid || !name) continue;

    const auto* user_context_id_val = space_dict->Find("userContextId");
    if (!user_context_id_val) {
      LOG(WARNING) << "Zen space missing userContextId, skipping workspace";
      continue;
    }
    int user_context_id = 0;
    if (user_context_id_val->is_int()) {
      user_context_id = user_context_id_val->GetInt();
    } else if (user_context_id_val->is_double()) {
      user_context_id = static_cast<int>(user_context_id_val->GetDouble());
    } else {
      LOG(WARNING) << "Zen space userContextId is not a number, skipping workspace";
      continue;
    }
    if (user_context_id <= 0) {
      LOG(WARNING) << "Zen space userContextId is not a positive integer: " << user_context_id << ", skipping workspace";
      continue;
    }

    ParsedWorkspace ws;
    ws.uuid = *uuid;
    ws.name = *name;
    ws.container_id = user_context_id;

    const std::string* icon = space_dict->FindString("icon");
    if (icon) ws.icon = *icon;

    ws.theme_color_hex = ExtractThemeColor(*space_dict);
    result.workspaces.push_back(std::move(ws));
  }

  // Parse folders first so that tabs can find them.
  if (folders_list) {
    std::vector<ParsedFolder> raw_folders;
    for (const auto& folder_val : *folders_list) {
      const auto* folder_dict = folder_val.GetIfDict();
      if (!folder_dict) continue;

      const std::string* id = folder_dict->FindString("id");
      const std::string* name = folder_dict->FindString("name");
      const std::string* ws_id = folder_dict->FindString("workspaceId");
      if (!id || !name || !ws_id) continue;

      ParsedFolder folder;
      folder.id = *id;
      folder.name = *name;
      folder.workspace_id = *ws_id;

      const std::string* parent = folder_dict->FindString("parentId");
      if (parent) folder.parent_id = *parent;

      raw_folders.push_back(std::move(folder));
    }

    for (auto& ws : result.workspaces) {
      std::vector<ParsedFolder> ws_folders;
      for (auto& f : raw_folders) {
        if (f.workspace_id == ws.uuid) {
          ws_folders.push_back(std::move(f));
        }
      }

      std::vector<bool> is_valid_folder(ws_folders.size(), true);
      for (size_t i = 0; i < ws_folders.size(); ++i) {
        const auto& folder = ws_folders[i];
        int depth = 0;
        std::string current_parent = folder.parent_id;
        bool cycle_or_deep = false;
        while (!current_parent.empty()) {
          depth++;
          if (depth >= 5) {
            cycle_or_deep = true;
            break;
          }
          bool parent_found = false;
          for (const auto& parent_folder : ws_folders) {
            if (parent_folder.id == current_parent) {
              if (parent_folder.id == folder.id) {
                cycle_or_deep = true;
                break;
              }
              current_parent = parent_folder.parent_id;
              parent_found = true;
              break;
            }
          }
          if (!parent_found) {
            break;
          }
        }
        if (cycle_or_deep) {
          is_valid_folder[i] = false;
        }
      }

      for (size_t i = 0; i < ws_folders.size(); ++i) {
        if (is_valid_folder[i]) {
          ws.folders.push_back(std::move(ws_folders[i]));
        }
      }
    }
  }

  for (const auto& tab_val : *tabs_list) {
    const auto* tab_dict = tab_val.GetIfDict();
    if (!tab_dict) continue;

    const std::string* workspace_id = tab_dict->FindString("zenWorkspace");
    if (!workspace_id || workspace_id->empty()) continue;

    std::string url = ExtractUrlFromTab(*tab_dict);
    if (url.empty() || url == "about:blank" || url == "about:newtab") continue;

    ParsedTab tab;
    tab.url = std::move(url);
    tab.title = ExtractTitleFromTab(*tab_dict);
    tab.is_pinned = tab_dict->FindBool("pinned").value_or(false);
    tab.is_essential = tab_dict->FindBool("zenEssential").value_or(false);

    const std::string* folder_id = tab_dict->FindString("folderId");
    if (folder_id && !folder_id->empty()) {
      bool found_folder = false;
      for (auto& ws : result.workspaces) {
        if (ws.uuid == *workspace_id) {
          for (auto& folder : ws.folders) {
            if (folder.id == *folder_id) {
              folder.tabs.push_back(std::move(tab));
              found_folder = true;
              break;
            }
          }
          break;
        }
      }
      if (found_folder) continue;
    }

    for (auto& ws : result.workspaces) {
      if (ws.uuid == *workspace_id) {
        if (tab.is_essential) {
          ws.favorites.push_back(std::move(tab));
        } else if (tab.is_pinned) {
          ws.pinned_tabs.push_back(std::move(tab));
        } else {
          ws.regular_tabs.push_back(std::move(tab));
        }
        break;
      }
    }
  }

  result.active_workspace_uuid = ReadActiveWorkspaceFromPrefs(profile_dir);
  result.status = ZenParseStatus::kSuccess;

  return result;
}

}  // namespace maho::importer
