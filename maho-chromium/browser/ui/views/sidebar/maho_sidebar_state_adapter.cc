// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"

#include <map>
#include <set>
#include <string>
#include <utility>

#include "base/command_line.h"
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
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/components/constants/webui_url_constants.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "url/gurl.h"

namespace maho {

bool IsSidebarIneligibleUrl(const GURL& url) {
  if (!url.is_valid() || url.is_empty()) {
    return true;
  }
  if (url.SchemeIs("about")) {
    return true;
  }
  if (url.SchemeIs("chrome")) {
    if (url.host() == "newtab" || url.host() == "downloads" ||
        url.host() == "history") {
      return true;
    }
  }
  return false;
}

namespace {

ShellEventObserver* g_shell_event_observer_for_testing = nullptr;

// Returns true when the browser was launched with --maho-sidebar-safe-mode.
// In safe mode all maho-core FFI calls in the sidebar state path are skipped
// so the browser stays usable (e.g. for maho-ai QA) even when the core is
// in a bad state.
bool IsSidebarSafeMode() {
  const base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();
  return cmd && cmd->HasSwitch("maho-sidebar-safe-mode");
}

struct FooterSpaceState {
  size_t space_count = 0;
  int active_space_index = -1;
  std::string active_space_color;
  MahoSpaceDisplayThemeModel active_space_theme;
  std::vector<std::string> space_icons;
  std::vector<std::string> space_names;
  std::vector<std::string> space_colors;
  std::vector<MahoSpaceDisplayThemeModel> space_themes;
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
    const MahoSpaceDisplayThemeModel theme = BuildMahoSpaceDisplayThemeModel(*dict);
    const std::string display_color =
        GetMahoSpaceDisplayPrimaryColor(theme, color ? *color : std::string());
    state.space_icons.push_back(icon ? *icon : std::string());
    state.space_names.push_back(name ? *name : std::string());
    state.space_colors.push_back(display_color);
    state.space_themes.push_back(theme);
    state.space_ids.push_back(id ? *id : std::string());
    if (id && *id == active_id) {
      state.active_space_index = index;
      state.active_space_color = display_color;
      state.active_space_theme = theme;
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

// Returns true if the tab strip has a valid active index entry, even when the
// WebContents at that index is not yet initialized (startup-state race).
// This is intentionally weaker than HasSafeNavigationContext: it answers
// "does a tab exist?" rather than "can we navigate?".
bool TabStripHasActiveEntry(Browser* browser) {
  if (!browser) {
    return false;
  }
  TabStripModel* tab_strip_model = browser->tab_strip_model();
  if (!tab_strip_model) {
    return false;
  }
  const int active_index = tab_strip_model->active_index();
  return active_index != TabStripModel::kNoTab &&
         tab_strip_model->ContainsIndex(active_index);
}

bool ShouldShowSearchPlaceholderAddress(const GURL& url) {
  if (url.is_empty() || !url.is_valid()) {
    return true;
  }

  if (url.SchemeIs("chrome") && url.host() == "newtab") {
    return true;
  }

  if (url.IsAboutBlank()) {
    return true;
  }

  return false;
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
  if (!url.is_valid() || MahoDisplayPolicy::IsNtpOrBlankUrl(url)) {
    return std::u16string();
  }

  if (!url.host().empty()) {
    return base::UTF8ToUTF16(url.host());
  }

  return base::UTF8ToUTF16(url.spec());
}

std::string GetActiveSpaceIdJsonForBrowser(Browser* browser) {
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  if (!bridge) {
    return std::string();
  }
  const std::string& active_space_id = bridge->GetActiveSpaceId(browser);
  if (active_space_id.empty()) {
    return std::string();
  }
  return base::GetQuotedJSONString(active_space_id);
}

std::string GetRawActiveSpaceIdForBrowser(Browser* browser) {
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  if (!bridge) {
    return std::string();
  }
  return bridge->GetActiveSpaceId(browser);
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

std::optional<base::Value> CallCoreFfi(
    char* (*ffi_fn)(MahoCore*, const char*, const int64_t*),
    const std::string& space_id_json,
    const int64_t* window_id_ptr) {
  MahoCore* core = maho::GetCore();
  if (!core || space_id_json.empty()) {
    return std::nullopt;
  }
  char* json_str = ffi_fn(core, space_id_json.c_str(), window_id_ptr);
  if (!json_str) {
    return std::nullopt;
  }
  std::string json(json_str);
  maho_string_free(json_str);
  return base::JSONReader::Read(json, base::JSON_PARSE_RFC);
}

// Build a stable-tab-ID-to-strip-index map using MahoTabIdHelper (primary path).
std::map<std::string, int> BuildStableIdToIndexMap(TabStripModel* model) {
  std::map<std::string, int> result;
  if (!model) {
    return result;
  }
  for (int i = 0; i < model->count(); ++i) {
    content::WebContents* contents = model->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    auto* helper = MahoTabIdHelper::FromWebContents(contents);
    if (helper && !helper->stable_tab_id().empty()) {
      result[helper->stable_tab_id()] = i;
    }
  }
  return result;
}


SidebarTreeNode TabNodeFromCoreJson(const base::DictValue& dict,
                                     TabStripModel* model,
                                     const std::map<std::string, int>& stable_id_map,
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
    node.url = *url;
    node.host = base::UTF8ToUTF16(GURL(*url).host());
  }

  // Match by stable tab ID via MahoTabIdHelper.
  if (!node.tab_id.empty()) {
    auto stable_it = stable_id_map.find(node.tab_id);
    if (stable_it != stable_id_map.end()) {
      node.tab_strip_index = stable_it->second;
    }
  }

  if (model && model->ContainsIndex(node.tab_strip_index)) {
    content::WebContents* contents =
        model->GetWebContentsAt(node.tab_strip_index);
    if (contents && !node.tab_id.empty()) {
      if (auto* helper = MahoTabIdHelper::FromWebContents(contents);
          helper && helper->stable_tab_id() != node.tab_id) {
        helper->SetRestoredTabId(node.tab_id);
      }
    }
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
  auto parsed = CallCoreFfi(maho_core_get_sidebar_root_order, space_id_json, nullptr);
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
    std::optional<base::Value> parsed_tabs,
    const std::string& space_id_json,
    bool is_pinned,
    const std::string& active_stable_id,
    TabStripModel* model,
    const std::map<std::string, int>& stable_id_map,
    const std::vector<FolderInfo>& all_folders,
    std::vector<SidebarTreeNode>& out_tree) {
  if (!parsed_tabs || !parsed_tabs->is_list()) {
    return;
  }

  std::map<std::string, SidebarTreeNode> tab_nodes_by_id;
  std::vector<SidebarTreeNode> all_tab_nodes;

  auto is_active_entry = [&active_stable_id](const base::Value::Dict& d) {
    if (active_stable_id.empty()) return false;
    const std::string* id = d.FindString("id");
    return id && *id == active_stable_id;
  };

  for (const auto& item : parsed_tabs->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const bool is_active = is_active_entry(*dict);
    const std::string* raw_url = dict->FindString("url");
    if (raw_url) {
      if (IsSidebarIneligibleUrl(GURL(*raw_url)) && !is_active) {
        continue;
      }
    }
    const std::string* raw_title = dict->FindString("title");
    const std::string url_str = raw_url ? *raw_url : std::string();
    const std::u16string title_str = raw_title
        ? base::UTF8ToUTF16(*raw_title)
        : std::u16string();
    if (MahoDisplayPolicy::IsGhostTab(url_str, title_str) && !is_active) {
      continue;
    }
    SidebarTreeNode tab_node =
        TabNodeFromCoreJson(*dict, model, stable_id_map, is_pinned, 0);
    tab_nodes_by_id[tab_node.tab_id] = tab_node;
    all_tab_nodes.push_back(std::move(tab_node));
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
}

// Overlays live active-tab fields onto a matched node so stale persisted data
// (e.g. customTitle="newtab") never wins over the real page state.
// Preserves any legitimate user-set custom_title; stale NTP sentinel values
// are handled narrowly in ResolveTabDisplayText on the display-policy side.
void OverlayActiveTabFields(SidebarTreeNode& node,
                             TabStripModel* model,
                             int active_index,
                             content::WebContents* active_contents) {
  node.tab_strip_index = active_index;
  node.is_active = true;
  node.is_pinned = model->IsTabPinned(active_index);
  node.title = GetTabTitle(model, active_index);
  node.host = GetTabHost(model, active_index);
  if (active_contents) {
    const GURL& live_url = GetVisibleOrCommittedUrl(active_contents);
    node.url = live_url.is_valid() ? live_url.spec() : std::string();
    node.is_loading = active_contents->IsLoading();
    node.is_audible = active_contents->IsCurrentlyAudible();
    node.is_muted = active_contents->IsAudioMuted();
    if (auto* helper = MahoTabIdHelper::FromWebContents(active_contents);
        helper && !helper->stable_tab_id().empty()) {
      node.tab_id = helper->stable_tab_id();
    }
  }
}

// Recursively searches |nodes| for a tab node matching the active tab by
// (a) tab_strip_index == active_index or (b) tab_id == active_stable_id.
// On match, overlays live fields via OverlayActiveTabFields and returns true.
// Both criteria are checked because stable IDs can race during startup (the ID
// may not be assigned yet) and because tab_strip_index may shift under reorder.
bool FindAndOverlayActiveTabInTree(std::vector<SidebarTreeNode>& nodes,
                                    int active_index,
                                    const std::string& active_stable_id,
                                    TabStripModel* model,
                                    content::WebContents* active_contents) {
  for (auto& node : nodes) {
    if (node.kind == SidebarNodeKind::kTab) {
      const bool index_match =
          active_index >= 0 && node.tab_strip_index == active_index;
      const bool id_match = !active_stable_id.empty() && !node.tab_id.empty() &&
                            node.tab_id == active_stable_id;
      if (index_match || id_match) {
        OverlayActiveTabFields(node, model, active_index, active_contents);
        return true;
      }
    }
    if (!node.children.empty() &&
        FindAndOverlayActiveTabInTree(node.children, active_index,
                                      active_stable_id, model,
                                      active_contents)) {
      return true;
    }
  }
  return false;
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
        ShouldShowSearchPlaceholderAddress(url)
            ? MahoSidebarAddressDisplayMode::kSearchPlaceholder
            : MahoSidebarAddressDisplayMode::kAddressCard;
  } else {
    // WebContents not yet initialized (startup-state race), but the tab strip
    // already has an active entry — the utility icon should still be available.
    model.has_active_tab = TabStripHasActiveEntry(browser);
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

  if (IsSidebarSafeMode()) {
    return model;
  }

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
    const std::string* custom_title = dict->FindString("customTitle");

    const std::u16string title_u16 =
        title ? base::UTF8ToUTF16(*title) : std::u16string();
    const std::u16string custom_title_u16 =
        (custom_title && !custom_title->empty())
            ? base::UTF8ToUTF16(*custom_title)
            : std::u16string();

    favorite_item.title = MahoDisplayPolicy::ResolveTabDisplayText(
        favorite_url, title_u16, custom_title_u16);

    const std::string* id = dict->FindString("id");
    if (id) {
      favorite_item.tab_id = *id;
    }
    if (!MahoDisplayPolicy::IsNtpOrBlankUrl(favorite_url)) {
      favorite_item.subtitle = favorite_url.host().empty()
                                    ? base::UTF8ToUTF16(favorite_url.spec())
                                    : base::UTF8ToUTF16(favorite_url.host());
    }
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

  model.active_space_id = GetRawActiveSpaceIdForBrowser(browser);
  std::string space_id_json = GetActiveSpaceIdJsonForBrowser(browser);

  if (tab_strip_model) {
    model.active_tab.index = tab_strip_model->active_index();
    if (tab_strip_model->ContainsIndex(model.active_tab.index)) {
      model.active_tab.title = GetTabTitle(tab_strip_model, model.active_tab.index);
      model.active_tab.host = GetTabHost(tab_strip_model, model.active_tab.index);
    }
  }

  auto stable_id_map = BuildStableIdToIndexMap(tab_strip_model);
  const bool tree_requested = !IsSidebarSafeMode() && !space_id_json.empty();
  std::vector<FolderInfo> all_folders;
  if (tree_requested) {
    all_folders = ParseFolders(space_id_json);
  }
  auto build_tree_from_tab_strip = [&]() {
    if (!tab_strip_model) {
      return;
    }

    for (int i = 0; i < tab_strip_model->count(); ++i) {
      content::WebContents* contents = tab_strip_model->GetWebContentsAt(i);
      if (contents) {
        const GURL& tab_url = GetVisibleOrCommittedUrl(contents);
        if (IsSidebarIneligibleUrl(tab_url)) {
          continue;
        }
      }
      SidebarTreeNode node;
      node.kind = SidebarNodeKind::kTab;
      node.tab_strip_index = i;
      node.is_active = tab_strip_model->active_index() == i;
      node.is_pinned = tab_strip_model->IsTabPinned(i);
      node.title = GetTabTitle(tab_strip_model, i);
      node.host = GetTabHost(tab_strip_model, i);
      if (contents) {
        const GURL& tab_url = GetVisibleOrCommittedUrl(contents);
        node.url = tab_url.is_valid() ? tab_url.spec() : std::string();
        node.is_loading = contents->IsLoading();
        node.is_audible = contents->IsCurrentlyAudible();
        node.is_muted = contents->IsAudioMuted();
        auto* helper = MahoTabIdHelper::FromWebContents(contents);
        if (helper && !helper->stable_tab_id().empty()) {
          node.tab_id = helper->stable_tab_id();
        }
      }
      if (node.is_pinned) {
        model.pinned_tree.push_back(std::move(node));
      } else {
        model.normal_tree.push_back(std::move(node));
      }
    }
  };

  if (tree_requested) {
    std::string active_stable_id;
    if (tab_strip_model) {
      const int active_index = tab_strip_model->active_index();
      if (active_index != TabStripModel::kNoTab &&
          tab_strip_model->ContainsIndex(active_index)) {
        if (auto* active_contents =
                tab_strip_model->GetWebContentsAt(active_index)) {
          if (auto* helper = MahoTabIdHelper::FromWebContents(active_contents);
              helper && !helper->stable_tab_id().empty()) {
            active_stable_id = helper->stable_tab_id();
          }
        }
      }
    }

    auto pinned_parsed = CallCoreFfi(maho_core_get_pinned_tabs, space_id_json);
    BuildTreeSection(std::move(pinned_parsed), space_id_json, true,
                     active_stable_id, tab_strip_model, stable_id_map,
                     all_folders, model.pinned_tree);

    auto normal_parsed = CallCoreFfi(maho_core_get_space_tabs, space_id_json, nullptr);
    BuildTreeSection(std::move(normal_parsed), space_id_json, false,
                     active_stable_id, tab_strip_model, stable_id_map,
                     all_folders, model.normal_tree);

    // When tree_requested is true, respect the core's result even if both
    // trees are empty.  Falling back to the tab-strip here would silently
    // mask an empty-core state and lose split-group consistency.
  } else {
    build_tree_from_tab_strip();
  }

  // Active-tab live overlay: refresh live fields (URL/title/loading/audio) on
  // the active tab's row so that stale persisted state (e.g. customTitle from
  // a previous navigation) never wins over the real page state. The row is
  // guaranteed to exist in the tree because BuildTreeSection exempts the
  // active entry from URL/ghost filters via active_stable_id.
  if (tab_strip_model) {
    const int active_index = tab_strip_model->active_index();
    if (active_index != TabStripModel::kNoTab &&
        tab_strip_model->ContainsIndex(active_index)) {
      content::WebContents* active_contents =
          tab_strip_model->GetWebContentsAt(active_index);

      std::string active_stable_id;
      if (active_contents) {
        if (auto* helper = MahoTabIdHelper::FromWebContents(active_contents);
            helper && !helper->stable_tab_id().empty()) {
          active_stable_id = helper->stable_tab_id();
        }
      }

      if (!FindAndOverlayActiveTabInTree(
              model.pinned_tree, active_index, active_stable_id,
              tab_strip_model, active_contents)) {
        FindAndOverlayActiveTabInTree(model.normal_tree, active_index,
                                      active_stable_id, tab_strip_model,
                                      active_contents);
      }
    }
  }

  return model;
}

MahoSidebarFooterModel MahoSidebarStateAdapter::BuildFooterModel(
    Browser* browser) const {
  MahoSidebarFooterModel model;
  const FooterSpaceState core_state =
      IsSidebarSafeMode() ? FooterSpaceState{} : GetCoreFooterSpaceState();
  const auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  const size_t bridge_space_count =
      bridge ? bridge->GetRegisteredSpaceCount() : 0u;

  const size_t space_count = core_state.has_core_state ? core_state.space_count
                                                       : bridge_space_count;
  model.status_text = space_count > 0 ? u"Spaces" : u"No spaces";
  model.space_count = static_cast<int>(space_count);
  int active_space_index = core_state.has_core_state
                               ? core_state.active_space_index
                               : (bridge ? bridge->GetActiveSpaceIndex(browser)
                                         : -1);
  std::string active_space_color = core_state.has_core_state
                                       ? core_state.active_space_color
                                       : std::string();
  MahoSpaceDisplayThemeModel active_space_theme = core_state.active_space_theme;
  if (bridge && !core_state.space_ids.empty()) {
    const std::string bridge_active_space_id =
        bridge->GetActiveSpaceId(browser);
    if (!bridge_active_space_id.empty()) {
      for (size_t i = 0; i < core_state.space_ids.size(); ++i) {
        if (core_state.space_ids[i] == bridge_active_space_id) {
          active_space_index = static_cast<int>(i);
          if (i < core_state.space_colors.size()) {
            active_space_color = core_state.space_colors[i];
          }
          if (i < core_state.space_themes.size()) {
            active_space_theme = core_state.space_themes[i];
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
  model.space_theme = active_space_theme;
  model.space_icons = core_state.space_icons;
  model.space_names = core_state.space_names;
  model.space_colors = core_state.space_colors;
  model.space_themes = core_state.space_themes;
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
