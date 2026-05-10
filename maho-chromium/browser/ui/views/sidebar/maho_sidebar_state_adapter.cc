// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"

#include <map>
#include <set>
#include <string>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_id.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/components/constants/webui_url_constants.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "url/gurl.h"

namespace maho {

namespace {

ShellEventObserver* g_shell_event_observer_for_testing = nullptr;

struct FooterSpaceState {
  size_t space_count = 0;
  int active_space_index = -1;
  std::string active_space_color;
  std::vector<std::string> space_icons;
  std::vector<std::string> space_names;
  std::vector<std::string> space_colors;
  std::vector<std::string> space_ids;
  bool has_core_state = false;
};

content::WebContents* GetActiveWebContents(Browser* browser) {
  if (!browser) {
    return nullptr;
  }

  TabStripModel* tab_strip_model = browser->tab_strip_model();
  if (!tab_strip_model) {
    return nullptr;
  }

  const int active_index = tab_strip_model->active_index();
  if (active_index == TabStripModel::kNoTab ||
      !tab_strip_model->ContainsIndex(active_index)) {
    return nullptr;
  }

  return tab_strip_model->GetWebContentsAt(active_index);
}

const GURL& GetVisibleOrCommittedUrl(content::WebContents* contents) {
  CHECK(contents);
  return contents->GetVisibleURL().is_empty() ? contents->GetLastCommittedURL()
                                              : contents->GetVisibleURL();
}

FooterSpaceState GetCoreFooterSpaceState() {
  FooterSpaceState state;
  MahoCore* core = maho::GetCore();
  if (!core) {
    return state;
  }

  char* json_str = maho_core_get_space_view_models(core);
  if (!json_str) {
    return state;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::string active_id;
  if (char* active_id_str = maho_core_get_active_space_id(core)) {
    std::optional<base::Value> parsed_active_id =
        base::JSONReader::Read(active_id_str, base::JSON_PARSE_RFC);
    active_id = parsed_active_id && parsed_active_id->is_string()
                    ? parsed_active_id->GetString()
                    : std::string(active_id_str);
    maho_string_free(active_id_str);
  }

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return state;
  }

  state.has_core_state = true;
  int index = 0;
  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      ++index;
      continue;
    }
    const std::string* id = dict->FindString("id");
    const std::string* color = dict->FindString("color");
    const std::string* icon = dict->FindString("icon");
    const std::string* name = dict->FindString("name");
    state.space_icons.push_back(icon ? *icon : std::string());
    state.space_names.push_back(name ? *name : std::string());
    state.space_colors.push_back(color ? *color : std::string());
    state.space_ids.push_back(id ? *id : std::string());
    if (id && *id == active_id) {
      state.active_space_index = index;
      state.active_space_color = color ? *color : std::string();
    }
    ++state.space_count;
    ++index;
  }

  return state;
}

bool HasSafeNavigationContext(Browser* browser) {
  if (!browser) {
    return false;
  }

  TabStripModel* tab_strip_model = browser->tab_strip_model();
  if (!tab_strip_model) {
    return false;
  }

  return GetActiveWebContents(browser) != nullptr;
}

bool ShouldShowSearchPlaceholderAddress(const GURL& url) {
  if (url.is_empty() || !url.is_valid()) {
    return true;
  }

  return url.SchemeIs("chrome") && url.host() == "newtab";
}

std::u16string GetTabTitle(TabStripModel* model, int index) {
  if (!model || !model->ContainsIndex(index)) {
    return u"Untitled tab";
  }

  content::WebContents* contents = model->GetWebContentsAt(index);
  if (!contents || contents->GetTitle().empty()) {
    return u"New Tab";
  }
  return contents->GetTitle();
}

std::u16string GetTabHost(TabStripModel* model, int index) {
  if (!model || !model->ContainsIndex(index)) {
    return std::u16string();
  }

  content::WebContents* contents = model->GetWebContentsAt(index);
  if (!contents) {
    return std::u16string();
  }

  const GURL& url = GetVisibleOrCommittedUrl(contents);
  if (!url.is_valid()) {
    return u"maho://new-tab";
  }

  if (!url.host().empty()) {
    return base::UTF8ToUTF16(url.host());
  }

  return base::UTF8ToUTF16(url.spec());
}

std::string GetActiveSpaceIdJson() {
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  if (!bridge) {
    return std::string();
  }
  const std::string& active_space_id = bridge->GetActiveSpaceId();
  if (active_space_id.empty()) {
    return std::string();
  }
  return base::GetQuotedJSONString(active_space_id);
}

std::string GetRawActiveSpaceId() {
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  if (!bridge) {
    return std::string();
  }
  return bridge->GetActiveSpaceId();
}

std::optional<base::Value> CallCoreFfi(
    char* (*ffi_fn)(MahoCore*, const char*),
    const std::string& space_id_json) {
  MahoCore* core = maho::GetCore();
  if (!core || space_id_json.empty()) {
    return std::nullopt;
  }
  char* json_str = ffi_fn(core, space_id_json.c_str());
  if (!json_str) {
    return std::nullopt;
  }
  std::string json(json_str);
  maho_string_free(json_str);
  return base::JSONReader::Read(json, base::JSON_PARSE_RFC);
}

// Build a URL-to-tab-strip-index map for linking core tab IDs to browser tabs.
std::map<std::string, std::vector<int>> BuildUrlToIndexMap(TabStripModel* model) {
  std::map<std::string, std::vector<int>> result;
  if (!model) {
    return result;
  }
  for (int i = 0; i < model->count(); ++i) {
    content::WebContents* contents = model->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    const GURL& url = GetVisibleOrCommittedUrl(contents);
    if (url.is_valid()) {
      result[url.spec()].push_back(i);
    }
  }
  return result;
}

SidebarTreeNode TabNodeFromCoreJson(const base::DictValue& dict,
                                    TabStripModel* model,
                                    std::map<std::string, std::vector<int>>& url_index,
                                    bool is_pinned,
                                    int depth) {
  SidebarTreeNode node;
  node.kind = SidebarNodeKind::kTab;
  node.depth = depth;
  node.is_pinned = is_pinned;

  const std::string* id = dict.FindString("id");
  if (id) {
    node.tab_id = *id;
  }

  const std::string* title = dict.FindString("title");
  if (title) {
    node.title = base::UTF8ToUTF16(*title);
  }

  const std::string* custom_title = dict.FindString("customTitle");
  if (custom_title) {
    node.custom_title = base::UTF8ToUTF16(*custom_title);
  }

  const std::string* url = dict.FindString("url");
  if (url) {
    node.host = base::UTF8ToUTF16(GURL(*url).host());
    auto it = url_index.find(*url);
    if (it != url_index.end() && !it->second.empty()) {
      node.tab_strip_index = it->second.front();
      it->second.erase(it->second.begin());
      if (it->second.empty()) {
        url_index.erase(it);
      }
      if (model && model->ContainsIndex(node.tab_strip_index)) {
        content::WebContents* contents =
            model->GetWebContentsAt(node.tab_strip_index);
        node.is_active = model->active_index() == node.tab_strip_index;
        if (node.custom_title.empty()) {
          node.title = GetTabTitle(model, node.tab_strip_index);
        }
        node.host = GetTabHost(model, node.tab_strip_index);
        if (contents) {
          node.is_loading = contents->IsLoading();
          node.is_audible = contents->IsCurrentlyAudible();
          node.is_muted = contents->IsAudioMuted();
        }
      }
    }
  }

  std::optional<bool> favorite = dict.FindBool("isFavorite");
  if (favorite.has_value()) {
    node.is_favorite = *favorite;
  }

  const std::string* parent_folder_id = dict.FindString("parentFolderId");
  if (parent_folder_id) {
    node.parent_folder_id = *parent_folder_id;
  }

  return node;
}

struct FolderInfo {
  std::string id;
  std::string name;
  bool is_expanded = true;
  bool is_pinned = false;
  std::vector<std::string> tab_ids;
  std::string parent_folder_id;
};

struct RootItemInfo {
  std::string kind;
  std::string id;
};

std::vector<RootItemInfo> ParseRootOrder(const std::string& space_id_json) {
  std::vector<RootItemInfo> root_items;
  auto parsed = CallCoreFfi(maho_core_get_sidebar_root_order, space_id_json);
  if (!parsed || !parsed->is_list()) {
    return root_items;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* kind = dict->FindString("kind");
    const std::string* id = dict->FindString("id");
    if (!kind || !id) {
      continue;
    }
    root_items.push_back(RootItemInfo{*kind, *id});
  }

  return root_items;
}

std::vector<FolderInfo> ParseFolders(const std::string& space_id_json) {
  std::vector<FolderInfo> folders;
  auto parsed = CallCoreFfi(maho_core_get_folder_view_models, space_id_json);
  if (!parsed || !parsed->is_list()) {
    return folders;
  }
  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    FolderInfo fi;
    const std::string* id = dict->FindString("id");
    if (id) fi.id = *id;
    const std::string* name = dict->FindString("name");
    if (name) fi.name = *name;
    std::optional<bool> expanded = dict->FindBool("isExpanded");
    if (expanded.has_value()) fi.is_expanded = *expanded;
    std::optional<bool> pinned = dict->FindBool("isPinned");
    if (pinned.has_value()) fi.is_pinned = *pinned;
    const auto* parent = dict->FindString("parentFolderId");
    if (parent) fi.parent_folder_id = *parent;
    const auto* tab_ids_list = dict->FindList("tabIds");
    if (tab_ids_list) {
      for (const auto& tid : *tab_ids_list) {
        if (tid.is_string()) {
          fi.tab_ids.push_back(tid.GetString());
        }
      }
    }
    folders.push_back(std::move(fi));
  }
  return folders;
}

SidebarTreeNode BuildFolderNode(
    const FolderInfo& fi,
    const std::map<std::string, SidebarTreeNode>& tab_nodes_by_id,
    const std::map<std::string, std::vector<const FolderInfo*>>& children_map,
    int depth) {
  SidebarTreeNode node;
  node.kind = SidebarNodeKind::kFolder;
  node.folder_id = fi.id;
  node.folder_name = base::UTF8ToUTF16(fi.name);
  node.is_expanded = fi.is_expanded;
  node.folder_is_pinned = fi.is_pinned;
  node.parent_of_folder_id = fi.parent_folder_id;
  node.depth = depth;

  for (size_t child_index = 0; child_index < fi.tab_ids.size(); ++child_index) {
    const auto& tid = fi.tab_ids[child_index];
    auto it = tab_nodes_by_id.find(tid);
    if (it != tab_nodes_by_id.end()) {
      SidebarTreeNode child = it->second;
      child.depth = depth + 1;
      child.parent_folder_id = fi.id;
      child.folder_child_index = static_cast<int>(child_index);
      node.children.push_back(std::move(child));
    }
  }

  auto child_it = children_map.find(fi.id);
  if (child_it != children_map.end()) {
    for (const FolderInfo* child_fi : child_it->second) {
      node.children.push_back(
          BuildFolderNode(*child_fi, tab_nodes_by_id, children_map, depth + 1));
    }
  }

  return node;
}

void GroupSplitTabs(std::vector<SidebarTreeNode>& nodes,
                    TabStripModel* model) {
  for (auto& node : nodes) {
    if (!node.children.empty()) {
      GroupSplitTabs(node.children, model);
    }
  }

  std::map<split_tabs::SplitTabId, std::vector<size_t>> split_groups;
  for (size_t i = 0; i < nodes.size(); ++i) {
    const auto& node = nodes[i];
    if (node.kind != SidebarNodeKind::kTab || node.tab_strip_index < 0) {
      continue;
    }
    auto split_id = model->GetSplitForTab(node.tab_strip_index);
    if (split_id.has_value()) {
      split_groups[split_id.value()].push_back(i);
    }
  }

  std::set<size_t> consumed_indices;
  std::vector<SidebarTreeNode> regrouped;
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (consumed_indices.count(i)) {
      continue;
    }
    auto& node = nodes[i];
    if (node.kind == SidebarNodeKind::kTab && node.tab_strip_index >= 0) {
      auto split_id = model->GetSplitForTab(node.tab_strip_index);
      if (split_id.has_value()) {
        auto it = split_groups.find(split_id.value());
        if (it != split_groups.end() && it->second.size() == 2) {
          SidebarTreeNode group;
          group.kind = SidebarNodeKind::kSplitGroup;
          group.split_id = split_id.value().ToString();
          group.split_ratio = 0.5;
          // TODO(maho): Read orientation/ratio from SplitTabVisualData
          // once upstream exposes GetVisualDataForSplit.
          group.split_orientation = "vertical";
          group.depth = node.depth;
          for (size_t idx : it->second) {
            nodes[idx].is_in_split = true;
            group.children.push_back(std::move(nodes[idx]));
            consumed_indices.insert(idx);
          }
          regrouped.push_back(std::move(group));
          continue;
        }
      }
    }
    regrouped.push_back(std::move(node));
  }
  nodes = std::move(regrouped);
}

void BuildTreeSection(
    const std::string& space_id_json,
    char* (*tab_ffi)(MahoCore*, const char*),
    bool is_pinned,
    TabStripModel* model,
    std::map<std::string, std::vector<int>>& url_index,
    const std::vector<FolderInfo>& all_folders,
    std::vector<SidebarTreeNode>& out_tree,
    std::vector<MahoSidebarTabRowModel>& out_flat) {
  auto parsed = CallCoreFfi(tab_ffi, space_id_json);
  if (!parsed || !parsed->is_list()) {
    return;
  }

  std::map<std::string, SidebarTreeNode> tab_nodes_by_id;
  std::vector<SidebarTreeNode> all_tab_nodes;

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    SidebarTreeNode tab_node =
        TabNodeFromCoreJson(*dict, model, url_index, is_pinned, 0);
    tab_nodes_by_id[tab_node.tab_id] = tab_node;
    all_tab_nodes.push_back(std::move(tab_node));

    MahoSidebarTabRowModel row;
    const auto& last = tab_nodes_by_id.rbegin()->second;
    row.tab_id = last.tab_id;
    row.index = last.tab_strip_index;
    row.is_active = last.is_active;
    row.is_pinned = is_pinned;
    row.section = is_pinned ? MahoSidebarTabSection::kPinned
                            : MahoSidebarTabSection::kNormal;
    row.title = last.title;
    row.host = last.host;
    row.is_loading = last.is_loading;
    row.is_audible = last.is_audible;
    row.is_muted = last.is_muted;
    out_flat.push_back(std::move(row));
  }

  std::set<std::string> tabs_in_folders;
  std::map<std::string, const FolderInfo*> folders_by_id;
  std::map<std::string, std::vector<const FolderInfo*>> children_map;

  for (const auto& fi : all_folders) {
    if (fi.is_pinned != is_pinned) {
      continue;
    }
    for (const auto& tid : fi.tab_ids) {
      tabs_in_folders.insert(tid);
    }
    folders_by_id.emplace(fi.id, &fi);
    if (!fi.parent_folder_id.empty()) {
      children_map[fi.parent_folder_id].push_back(&fi);
    }
  }

  const std::vector<RootItemInfo> root_order = ParseRootOrder(space_id_json);
  for (const auto& root_item : root_order) {
    if (root_item.kind == "tab") {
      if (tabs_in_folders.count(root_item.id) != 0) {
        continue;
      }
      auto tab_it = tab_nodes_by_id.find(root_item.id);
      if (tab_it != tab_nodes_by_id.end()) {
        out_tree.push_back(tab_it->second);
      }
      continue;
    }

    if (root_item.kind == "folder") {
      auto folder_it = folders_by_id.find(root_item.id);
      if (folder_it != folders_by_id.end()) {
        out_tree.push_back(
            BuildFolderNode(*folder_it->second, tab_nodes_by_id, children_map, 0));
      }
    }
  }

  if (model) {
    GroupSplitTabs(out_tree, model);
  }

  if (model) {
    GroupSplitTabs(out_tree, model);
  }
}

}  // namespace

void SetShellEventObserverForTesting(ShellEventObserver* observer) {
  g_shell_event_observer_for_testing = observer;
}

void DispatchShellEvent(
    const std::string& kind,
    const std::vector<std::pair<std::string, std::string>>& fields) {
  base::DictValue event;
  event.Set("kind", kind);
  for (const auto& [key, value] : fields) {
    event.Set(key, value);
  }
  std::string json;
  base::JSONWriter::Write(event, &json);
  if (g_shell_event_observer_for_testing) {
    g_shell_event_observer_for_testing->OnShellEventDispatched(kind, json);
  }
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  maho::core::HandleEvent(core, json.c_str());
  if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance(); bridge) {
    bridge->NotifyChanged();
  }
}

void DispatchShellEventEx(
    const std::string& kind,
    const std::vector<ShellEventField>& fields) {
  base::DictValue event;
  event.Set("kind", kind);
  for (const auto& field : fields) {
    switch (field.type) {
      case ShellEventField::Type::kInt:
        event.Set(field.key, field.int_value);
        break;
      case ShellEventField::Type::kBool:
        event.Set(field.key, field.bool_value);
        break;
      case ShellEventField::Type::kString:
        event.Set(field.key, field.string_value);
        break;
    }
  }
  std::string json;
  base::JSONWriter::Write(event, &json);
  if (g_shell_event_observer_for_testing) {
    g_shell_event_observer_for_testing->OnShellEventDispatched(kind, json);
  }
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  maho::core::HandleEvent(core, json.c_str());
  if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance(); bridge) {
    bridge->NotifyChanged();
  }
}

void DispatchShellEventDict(const std::string& kind,
                            base::DictValue dict) {
  dict.Set("kind", kind);
  std::string json;
  base::JSONWriter::Write(dict, &json);
  if (g_shell_event_observer_for_testing) {
    g_shell_event_observer_for_testing->OnShellEventDispatched(kind, json);
  }
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  maho::core::HandleEvent(core, json.c_str());
  if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance(); bridge) {
    bridge->NotifyChanged();
  }
}

MahoSidebarStateAdapter::MahoSidebarStateAdapter() = default;

MahoSidebarStateAdapter::~MahoSidebarStateAdapter() = default;

MahoSidebarTopBarModel MahoSidebarStateAdapter::BuildTopBarModel(
    Browser* browser) const {
  MahoSidebarTopBarModel model;
  model.navigation_enabled = HasSafeNavigationContext(browser);
  model.can_go_back = model.navigation_enabled && chrome::CanGoBack(browser);
  model.can_go_forward =
      model.navigation_enabled && chrome::CanGoForward(browser);
  model.can_reload = model.navigation_enabled && chrome::CanReload(browser);
  if (content::WebContents* contents = GetActiveWebContents(browser)) {
    model.is_loading = contents->IsLoading();
  }
  return model;
}

MahoSidebarSearchModel MahoSidebarStateAdapter::BuildSearchModel(
    Browser* browser) const {
  MahoSidebarSearchModel model;
  model.force_placeholder_for_empty_favorites = favorites_model_.items.empty();
  if (content::WebContents* contents = GetActiveWebContents(browser)) {
    const GURL& url = GetVisibleOrCommittedUrl(contents);
    model.has_active_tab = true;
    model.current_url = url.is_valid() ? url.spec() : std::string();
    model.idle_text = GetActiveHostText(browser);
    if (model.idle_text.empty()) {
      model.idle_text = model.current_url.empty()
                            ? u"Search or Enter URL..."
                            : base::UTF8ToUTF16(model.current_url);
    }
    model.display_mode =
        (model.force_placeholder_for_empty_favorites ||
         ShouldShowSearchPlaceholderAddress(url))
            ? MahoSidebarAddressDisplayMode::kSearchPlaceholder
            : MahoSidebarAddressDisplayMode::kAddressCard;
  } else {
    model.idle_text = u"Search or Enter URL...";
    model.display_mode = MahoSidebarAddressDisplayMode::kSearchPlaceholder;
  }
  return model;
}

MahoSidebarFavoritesModel
MahoSidebarStateAdapter::BuildFavoritesModelForSpaceIdJson(
    std::string active_space_id_json) {
  MahoSidebarFavoritesModel model;
  model.items.reserve(model.slot_count);

  MahoCore* core = maho::GetCore();
  if (!core || active_space_id_json.empty()) {
    return model;
  }

  char* json_str = maho_core_get_favorite_tabs(core, active_space_id_json.c_str());
  if (!json_str) {
    return model;
  }

  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return model;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }

    const std::string* url = dict->FindString("url");
    if (!url || url->empty()) {
      continue;
    }

    GURL favorite_url(*url);
    if (!favorite_url.is_valid()) {
      continue;
    }

    MahoSidebarFavoriteItemModel favorite_item;
    const std::string* title = dict->FindString("title");
    if (title) {
      favorite_item.title = base::UTF8ToUTF16(*title);
    }
    const std::string* custom_title = dict->FindString("customTitle");
    if (custom_title && !custom_title->empty()) {
      favorite_item.title = base::UTF8ToUTF16(*custom_title);
    }
    const std::string* id = dict->FindString("id");
    if (id) {
      favorite_item.tab_id = *id;
    }
    favorite_item.subtitle = favorite_url.host().empty()
                                  ? base::UTF8ToUTF16(favorite_url.spec())
                                  : base::UTF8ToUTF16(favorite_url.host());
    favorite_item.url = favorite_url;
    model.items.push_back(std::move(favorite_item));
    if (model.items.size() == model.slot_count) {
      break;
    }
  }

  return model;
}

void MahoSidebarStateAdapter::SetFavoritesModel(
    MahoSidebarFavoritesModel favorites_model) {
  favorites_model_ = std::move(favorites_model);
}

MahoSidebarFavoritesModel MahoSidebarStateAdapter::BuildFavoritesModel(
    Browser* browser) const {
  return favorites_model_;
}

MahoSidebarTabListModel MahoSidebarStateAdapter::BuildTabListModel(
    Browser* browser) const {
  MahoSidebarTabListModel model;
  TabStripModel* tab_strip_model = browser ? browser->tab_strip_model() : nullptr;

  model.active_space_id = GetRawActiveSpaceId();
  std::string space_id_json = GetActiveSpaceIdJson();

  if (tab_strip_model) {
    model.active_tab.index = tab_strip_model->active_index();
    if (tab_strip_model->ContainsIndex(model.active_tab.index)) {
      model.active_tab.title = GetTabTitle(tab_strip_model, model.active_tab.index);
      model.active_tab.host = GetTabHost(tab_strip_model, model.active_tab.index);
    }
  }

  auto url_index = BuildUrlToIndexMap(tab_strip_model);
  auto all_folders = ParseFolders(space_id_json);
  const bool tree_requested = !space_id_json.empty();
  bool used_flat_fallback = false;

  auto build_flat_from_tab_strip = [&]() {
    if (!tab_strip_model) {
      return;
    }

    for (int i = 0; i < tab_strip_model->count(); ++i) {
      MahoSidebarTabRowModel row;
      row.index = i;
      row.is_active = tab_strip_model->active_index() == i;
      row.is_pinned = tab_strip_model->IsTabPinned(i);
      row.section = row.is_pinned ? MahoSidebarTabSection::kPinned
                                  : MahoSidebarTabSection::kNormal;
      row.title = GetTabTitle(tab_strip_model, i);
      row.host = GetTabHost(tab_strip_model, i);
      content::WebContents* contents = tab_strip_model->GetWebContentsAt(i);
      if (contents) {
        row.is_loading = contents->IsLoading();
        row.is_audible = contents->IsCurrentlyAudible();
        row.is_muted = contents->IsAudioMuted();
      }
      if (row.is_pinned) {
        model.pinned_tabs.push_back(std::move(row));
      } else {
        model.tabs.push_back(std::move(row));
      }
    }
  };

  if (tree_requested) {
    BuildTreeSection(space_id_json, maho_core_get_pinned_tabs, true,
                     tab_strip_model, url_index, all_folders,
                     model.pinned_tree, model.pinned_tabs);
    BuildTreeSection(space_id_json, maho_core_get_space_tabs, false,
                     tab_strip_model, url_index, all_folders,
                     model.normal_tree, model.tabs);

    if (tab_strip_model && model.pinned_tree.empty() && model.normal_tree.empty() &&
        model.pinned_tabs.empty() && model.tabs.empty()) {
      used_flat_fallback = true;
      build_flat_from_tab_strip();
    }
  } else {
    used_flat_fallback = true;
    build_flat_from_tab_strip();
  }

  LOG(INFO) << "MahoPinnedDebug BuildTabListModel: active_space_id='"
            << model.active_space_id << "' tree_mode=" << tree_requested
            << " flat_fallback=" << used_flat_fallback
            << " pinned_tree=" << model.pinned_tree.size()
            << " normal_tree=" << model.normal_tree.size()
            << " pinned_tabs=" << model.pinned_tabs.size()
            << " normal_tabs=" << model.tabs.size();

  return model;
}

MahoSidebarFooterModel MahoSidebarStateAdapter::BuildFooterModel(
    Browser* browser) const {
  MahoSidebarFooterModel model;
  const FooterSpaceState core_state = GetCoreFooterSpaceState();
  const auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  const size_t bridge_space_count =
      bridge ? bridge->GetRegisteredSpaceCount() : 0u;

  const size_t space_count = core_state.has_core_state ? core_state.space_count
                                                       : bridge_space_count;
  model.status_text = space_count > 0 ? u"Spaces" : u"No spaces";
  model.space_count = static_cast<int>(space_count);
  int active_space_index = core_state.has_core_state
                               ? core_state.active_space_index
                               : (bridge ? bridge->GetActiveSpaceIndex() : -1);
  std::string active_space_color = core_state.has_core_state
                                       ? core_state.active_space_color
                                       : std::string();
  if (bridge && !core_state.space_ids.empty()) {
    const std::string bridge_active_space_id = bridge->GetActiveSpaceId();
    if (!bridge_active_space_id.empty()) {
      for (size_t i = 0; i < core_state.space_ids.size(); ++i) {
        if (core_state.space_ids[i] == bridge_active_space_id) {
          active_space_index = static_cast<int>(i);
          if (i < core_state.space_colors.size()) {
            active_space_color = core_state.space_colors[i];
          }
          break;
        }
      }
    }
  }
  model.active_space_index = active_space_index;
  model.can_open_space_controls = space_count > 0;
  model.can_create_space = browser != nullptr;
  model.space_color = active_space_color;
  model.space_icons = core_state.space_icons;
  model.space_names = core_state.space_names;
  model.space_colors = core_state.space_colors;
  model.space_ids = core_state.space_ids;
  model.update_pill.visible = false;
  model.update_pill.has_update = false;
  model.update_pill.label.clear();
  model.update_pill.accessible_label.clear();
  return model;
}

MahoSidebarViewStateModel MahoSidebarStateAdapter::BuildViewStateModel(
    Browser* browser) const {
  MahoSidebarViewStateModel model;
  model.top_bar = BuildTopBarModel(browser);
  model.search = BuildSearchModel(browser);
  model.favorites = BuildFavoritesModel(browser);
  model.tab_list = BuildTabListModel(browser);
  model.footer = BuildFooterModel(browser);
  return model;
}

std::u16string MahoSidebarStateAdapter::GetActiveHostText(Browser* browser) const {
  TabStripModel* tab_strip_model = browser ? browser->tab_strip_model() : nullptr;
  if (!tab_strip_model || tab_strip_model->active_index() == TabStripModel::kNoTab) {
    return u"Search or Enter URL...";
  }

  return GetTabHost(tab_strip_model, tab_strip_model->active_index());
}

}  // namespace maho
