// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"

#include <map>
#include <set>
#include <string>
#include <utility>

#include "base/base64.h"
#include "base/command_line.h"
#include "base/containers/flat_map.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/timer/elapsed_timer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/translate/chrome_translate_client.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/translate/core/browser/language_state.h"
#include "components/translate/core/browser/translate_manager.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/net/maho_boost_injection_handler.h"
#include "maho/browser/ui/webui/maho_live_folders/maho_live_folder_item_cache.h"
#include "maho/components/constants/webui_url_constants.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"

namespace maho {

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

// LEGACY: replaced by FetchSidebarStateV2 in BuildViewStateModel. Kept
// compiling for backward compat during transition; will be deleted after
// 1-week soak (4.2.3).
FooterSpaceState GetCoreFooterSpaceState() {
  FooterSpaceState state;
  ::MahoCore* core = maho::GetCore();
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

std::optional<SidebarTreeNode> ParseTreeNode(
    const base::Value& value,
    TabStripModel* model,
    const std::map<std::string, int>& stable_id_map,
    const std::string& active_stable_id,
    int depth,
    const base::flat_map<std::string, std::vector<uint8_t>>& favicon_map) {
  const auto* dict = value.GetIfDict();
  if (!dict) {
    return std::nullopt;
  }

  const std::string* kind = dict->FindString("kind");
  if (!kind) {
    return std::nullopt;
  }

  SidebarTreeNode node;
  if (*kind == "tab") {
    const std::string* id = dict->FindString("id");
    const bool is_active = (id && !active_stable_id.empty() && *id == active_stable_id);
    const std::string* raw_url = dict->FindString("url");
    const std::string* raw_title = dict->FindString("title");
    const std::string url_str = raw_url ? *raw_url : std::string();
    const std::u16string title_str = raw_title
        ? base::UTF8ToUTF16(*raw_title)
        : std::u16string();
    if (MahoDisplayPolicy::IsGhostTab(url_str, title_str) && !is_active) {
      return std::nullopt;
    }

    node.kind = SidebarNodeKind::kTab;
    node.depth = depth;
    if (id) {
      node.tab_id = *id;
    }
    if (raw_title) {
      node.title = title_str;
    }
    const std::string* custom_title = dict->FindString("customTitle");
    if (custom_title) {
      node.custom_title = base::UTF8ToUTF16(*custom_title);
    }
    if (raw_url) {
      node.url = url_str;
      node.host = base::UTF8ToUTF16(GURL(url_str).host());
    }

    std::optional<bool> pinned = dict->FindBool("isPinned");
    node.is_pinned = pinned.value_or(false);

    std::optional<bool> favorite = dict->FindBool("isFavorite");
    node.is_favorite = favorite.value_or(false);

    // V2 path: look up prebuilt favicon from the flat_map (base64-decoded in
    // BuildViewStateModel). Falls back to legacy int-array parsing if the map
    // is empty (v1 callers during transition).
    if (!node.tab_id.empty() && !favicon_map.empty()) {
      auto fav_it = favicon_map.find(node.tab_id);
      if (fav_it != favicon_map.end()) {
        node.favicon_png_data = fav_it->second;
      }
    } else if (const auto* favicon_dict = dict->FindDict("favicon")) {
      // LEGACY: per-byte integer array parsing (v1). Will be removed in 4.2.3.
      if (const auto* data_list = favicon_dict->FindList("data")) {
        std::vector<uint8_t> bytes;
        bytes.reserve(data_list->size());
        for (const auto& v : *data_list) {
          if (auto byte = v.GetIfInt()) {
            bytes.push_back(static_cast<uint8_t>(*byte & 0xFF));
          }
        }
        if (!bytes.empty()) {
          node.favicon_png_data = std::move(bytes);
        }
      }
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

    const std::string* parent_folder_id = dict->FindString("parentFolderId");
    if (parent_folder_id) {
      node.parent_folder_id = *parent_folder_id;
    }
  } else if (*kind == "folder") {
    node.kind = SidebarNodeKind::kFolder;
    node.depth = depth;

    const std::string* id = dict->FindString("id");
    if (id) {
      node.folder_id = *id;
    }
    const std::string* name = dict->FindString("name");
    if (name) {
      node.folder_name = base::UTF8ToUTF16(*name);
    }
    // Parse snake_case keys (live JSON from Rust core); fall back to camelCase
    // for backward compatibility with any legacy serialiser.
    std::optional<bool> expanded = dict->FindBool("is_expanded");
    if (!expanded.has_value()) {
      expanded = dict->FindBool("isExpanded");
    }
    node.is_expanded = expanded.value_or(false);
    if (name && *name == "Research") {
      LOG(ERROR) << "[folder-state-debug] Research folderId="
                 << node.folder_id << " isExpanded=" << node.is_expanded;
    }

    std::optional<bool> pinned = dict->FindBool("is_pinned");
    if (!pinned.has_value()) {
      pinned = dict->FindBool("isPinned");
    }
    node.folder_is_pinned = pinned.value_or(false);

    const std::string* parent = dict->FindString("parent_folder_id");
    if (!parent) {
      parent = dict->FindString("parentFolderId");
    }
    if (parent) {
      node.parent_of_folder_id = *parent;
    }

    const std::string* provider_type = dict->FindString("provider_type");
    if (!provider_type) {
      provider_type = dict->FindString("providerType");
    }
    if (provider_type) {
      node.provider_type = *provider_type;
      node.is_live = !node.provider_type.empty();
    }

    const std::string* config_json = dict->FindString("config_json");
    if (!config_json) {
      config_json = dict->FindString("configJson");
    }
    if (config_json) {
      node.config_json = *config_json;
    }

    const auto* children_list = dict->FindList("children");
    if (children_list) {
      int child_idx = 0;
      for (const auto& child_val : *children_list) {
        auto parsed_child = ParseTreeNode(child_val, model, stable_id_map, active_stable_id, depth + 1, favicon_map);
        if (parsed_child.has_value()) {
          SidebarTreeNode child_node = std::move(parsed_child.value());
          if (child_node.kind == SidebarNodeKind::kTab) {
            child_node.parent_folder_id = node.folder_id;
            child_node.folder_child_index = child_idx++;
          }
          node.children.push_back(std::move(child_node));
        }
      }
    }

    if (node.is_live && node.children.empty() && !node.config_json.empty()) {
      const auto* cached_items =
          maho::LiveFolderItemCache::GetInstance().GetItems(node.config_json);
      if (cached_items) {
        int child_idx = 0;
        for (const auto& item : *cached_items) {
          SidebarTreeNode child;
          child.kind = SidebarNodeKind::kTab;
          child.tab_id = item.id;
          child.title = base::UTF8ToUTF16(item.title);
          child.host = base::UTF8ToUTF16(item.subtitle);
          child.url = item.url;
          child.tab_strip_index = -1;
          child.parent_folder_id = node.folder_id;
          child.folder_child_index = child_idx++;
          node.children.push_back(std::move(child));
        }
      }
    }
  } else {
    return std::nullopt;
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

FooterSpaceState::FooterSpaceState() = default;
FooterSpaceState::FooterSpaceState(const FooterSpaceState&) = default;
FooterSpaceState& FooterSpaceState::operator=(const FooterSpaceState&) = default;
FooterSpaceState::FooterSpaceState(FooterSpaceState&&) = default;
FooterSpaceState& FooterSpaceState::operator=(FooterSpaceState&&) = default;
FooterSpaceState::~FooterSpaceState() = default;

SidebarStateBackgroundResult::SidebarStateBackgroundResult() = default;
SidebarStateBackgroundResult::SidebarStateBackgroundResult(
    SidebarStateBackgroundResult&&) = default;
SidebarStateBackgroundResult& SidebarStateBackgroundResult::operator=(
    SidebarStateBackgroundResult&&) = default;
SidebarStateBackgroundResult::~SidebarStateBackgroundResult() = default;

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
  ::MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  std::string updates = maho::core::HandleEvent(core, json.c_str());
  if (kind == "toggle_folder_expanded") {
    LOG(ERROR) << "[folder-state-debug] handle_event updates=" << updates;
  }
  if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance(); bridge) {
    bridge->NotifyChanged();
  }
  MahoBoostInjectionHandler::OnCoreUpdatesJson(updates);
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
  ::MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  std::string updates = maho::core::HandleEvent(core, json.c_str());
  if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance(); bridge) {
    bridge->NotifyChanged();
  }
  MahoBoostInjectionHandler::OnCoreUpdatesJson(updates);
}

void DispatchShellEventDict(const std::string& kind,
                            base::DictValue dict) {
  dict.Set("kind", kind);
  std::string json;
  base::JSONWriter::Write(dict, &json);
  if (g_shell_event_observer_for_testing) {
    g_shell_event_observer_for_testing->OnShellEventDispatched(kind, json);
  }
  ::MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  std::string updates = maho::core::HandleEvent(core, json.c_str());
  if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance(); bridge) {
    bridge->NotifyChanged();
  }
  MahoBoostInjectionHandler::OnCoreUpdatesJson(updates);
}

struct MahoSidebarStateAdapter::CycleSpaceCache {
  FooterSpaceState state;
};

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

    if (auto* translate_client =
            ChromeTranslateClient::FromWebContents(contents)) {
      if (auto* translate_manager =
              translate_client->GetTranslateManager()) {
        const auto* language_state = translate_manager->GetLanguageState();
        model.translation_available =
            language_state &&
            language_state->translate_enabled() &&
            !language_state->IsPageTranslated();
        model.is_page_translated =
            language_state && language_state->IsPageTranslated();
      }
    }
  } else {
    // WebContents not yet initialized (startup-state race), but the tab strip
    // already has an active entry — the utility icon should still be available.
    model.has_active_tab = TabStripHasActiveEntry(browser);
    model.idle_text = u"Search or Enter URL...";
    model.display_mode = MahoSidebarAddressDisplayMode::kSearchPlaceholder;
  }
  return model;
}

// LEGACY: replaced by FetchSidebarStateV2 in BuildViewStateModel. Kept
// compiling for backward compat during transition; will be deleted after
// 1-week soak (4.2.3).
MahoSidebarFavoritesModel
MahoSidebarStateAdapter::BuildFavoritesModelForSpaceIdJson(
    std::string active_space_id_json) {
  MahoSidebarFavoritesModel model;
  model.items.reserve(model.slot_count);

  if (IsSidebarSafeMode()) {
    return model;
  }

  ::MahoCore* core = maho::GetCore();
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
  MahoSidebarFavoritesModel model = favorites_model_;
  if (!browser || !browser->tab_strip_model()) {
    return model;
  }
  content::WebContents* active = browser->tab_strip_model()->GetActiveWebContents();
  std::string active_id;
  if (active) {
    if (auto* helper = MahoTabIdHelper::FromWebContents(active)) {
      active_id = helper->stable_tab_id();
    }
  }
  if (!active_id.empty()) {
    for (auto& item : model.items) {
      if (!item.tab_id.empty() && item.tab_id == active_id) {
        item.is_active = true;
        break;
      }
    }
  }
  return model;
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
  auto build_tree_from_tab_strip = [&]() {
    if (!tab_strip_model) {
      return;
    }

    for (int i = 0; i < tab_strip_model->count(); ++i) {
      content::WebContents* contents = tab_strip_model->GetWebContentsAt(i);
      if (contents) {
        const GURL& tab_url = GetVisibleOrCommittedUrl(contents);
        const bool is_active = tab_strip_model->active_index() == i;
        if (MahoDisplayPolicy::IsSystemSurfaceUrl(tab_url) && !is_active) {
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

    auto parsed_tree = std::move(v2_tree_);
    if (parsed_tree && parsed_tree->is_list()) {
      for (const auto& root_val : parsed_tree->GetList()) {
        auto parsed_node = ParseTreeNode(root_val, tab_strip_model, stable_id_map, active_stable_id, 0, v2_favicon_map_);
        if (parsed_node.has_value()) {
          SidebarTreeNode node = std::move(parsed_node.value());
          if ((node.kind == SidebarNodeKind::kTab && node.is_pinned) ||
              (node.kind == SidebarNodeKind::kFolder && node.folder_is_pinned)) {
            model.pinned_tree.push_back(std::move(node));
          } else {
            model.normal_tree.push_back(std::move(node));
          }
        }
      }
    }
    if (tab_strip_model) {
      GroupSplitTabs(model.pinned_tree, tab_strip_model);
      GroupSplitTabs(model.normal_tree, tab_strip_model);
    }
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

  FooterSpaceState space_state;
  if (cycle_space_cache_) {
    space_state = cycle_space_cache_->state;
  } else if (!IsSidebarSafeMode()) {
    space_state = GetCoreFooterSpaceState();
  }
  const int idx = space_state.active_space_index;
  if (idx >= 0 &&
      idx < static_cast<int>(space_state.space_names.size()) &&
      idx < static_cast<int>(space_state.space_icons.size())) {
    model.active_space_icon = space_state.space_icons[idx];
    model.active_space_name = base::UTF8ToUTF16(space_state.space_names[idx]);
  }

  return model;
}

MahoSidebarFooterModel MahoSidebarStateAdapter::BuildFooterModel(
    Browser* browser) const {
  MahoSidebarFooterModel model;
  FooterSpaceState core_state;
  if (cycle_space_cache_) {
    core_state = cycle_space_cache_->state;
  } else if (!IsSidebarSafeMode()) {
    core_state = GetCoreFooterSpaceState();
  }
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

// --- V2 FFI: single-call sidebar state ---

std::string MahoSidebarStateAdapter::FetchSidebarStateV2(
    ::MahoCore* core, const std::string& space_id_json) const {
  char* raw = maho_core_get_sidebar_state_v2(core, space_id_json.c_str());
  if (!raw) {
    return {};
  }
  std::string out(raw);
  maho_string_free(raw);
  return out;
}

MahoSidebarViewStateModel MahoSidebarStateAdapter::BuildViewStateModel(
    Browser* browser) const {
  base::ElapsedTimer maho_perf_t;
  if (!IsSidebarSafeMode()) {
    base::ElapsedTimer footer_t;
    cycle_space_cache_ = std::make_unique<CycleSpaceCache>();
    cycle_space_cache_->state = GetCoreFooterSpaceState();
    LOG(INFO) << "[MAHO_PERF]     GetCoreFooterSpaceState="
              << footer_t.Elapsed().InMillisecondsF() << "ms";
  }

  std::string space_id_json = GetActiveSpaceIdJsonForBrowser(browser);
  base::flat_map<std::string, std::vector<uint8_t>> favicon_map;

  ::MahoCore* core = maho::GetCore();
  if (core && !space_id_json.empty() && !IsSidebarSafeMode()) {
    base::ElapsedTimer v2_t;
    std::string v2_json = FetchSidebarStateV2(core, space_id_json);
    LOG(INFO) << "[MAHO_PERF]     FetchSidebarStateV2="
              << v2_t.Elapsed().InMillisecondsF() << "ms bytes=" << v2_json.size();
    if (!v2_json.empty()) {
      std::optional<base::Value> v2_parsed =
          base::JSONReader::Read(v2_json, base::JSON_PARSE_RFC);
      if (const auto* v2_dict = v2_parsed ? v2_parsed->GetIfDict() : nullptr) {
        const auto* favicons_dict = v2_dict->FindDict("favicons");
        if (favicons_dict) {
          for (const auto [tab_id, favicon_val] : *favicons_dict) {
            if (!favicon_val.is_string()) {
              continue;
            }
            const std::string& base64_str = favicon_val.GetString();
            if (base64_str.empty()) {
              continue;
            }
            std::string decoded_bytes;
            if (!base::Base64Decode(base64_str, &decoded_bytes)) {
              continue;
            }
            std::vector<uint8_t> png_data(decoded_bytes.begin(),
                                          decoded_bytes.end());
            favicon_map[std::string(tab_id)] = std::move(png_data);
          }
        }

        const base::Value* tree_val = v2_dict->Find("tree");
        if (tree_val && tree_val->is_list()) {
          v2_tree_ = tree_val->Clone();
        }
      }
    }
  }

  v2_favicon_map_ = std::move(favicon_map);

  MahoSidebarViewStateModel model;
  base::ElapsedTimer build_t;
  model.top_bar = BuildTopBarModel(browser);
  model.search = BuildSearchModel(browser);
  model.favorites = BuildFavoritesModel(browser);
  model.tab_list = BuildTabListModel(browser);
  model.footer = BuildFooterModel(browser);
  LOG(INFO) << "[MAHO_PERF]     BuildXXXModel(top+search+fav+tabList+footer)="
            << build_t.Elapsed().InMillisecondsF() << "ms";
  v2_favicon_map_.clear();
  v2_tree_.reset();
  cycle_space_cache_.reset();
  LOG(INFO) << "[MAHO_PERF]   BuildViewStateModel TOTAL="
            << maho_perf_t.Elapsed().InMillisecondsF() << "ms";
  return model;
}

// static
SidebarStateBackgroundResult MahoSidebarStateAdapter::BuildStateOnBackground(
    std::string space_id_json,
    bool safe_mode) {
  SidebarStateBackgroundResult result;

  // Phase 0: footer space state (FFI, 1.5-7ms).
  if (!safe_mode) {
    result.footer_state = GetCoreFooterSpaceState();
  }

  // Phase 2: V2 tree + favicons (FFI + JSON parse + base64 decode, 3-21ms).
  ::MahoCore* core = maho::GetCore();
  if (core && !space_id_json.empty() && !safe_mode) {
    char* raw = maho_core_get_sidebar_state_v2(core, space_id_json.c_str());
    if (raw) {
      std::string v2_json(raw);
      maho_string_free(raw);

      std::optional<base::Value> v2_parsed =
          base::JSONReader::Read(v2_json, base::JSON_PARSE_RFC);
      if (const auto* v2_dict =
              v2_parsed ? v2_parsed->GetIfDict() : nullptr) {
        if (const auto* favicons_dict = v2_dict->FindDict("favicons")) {
          for (const auto [tab_id, favicon_val] : *favicons_dict) {
            if (!favicon_val.is_string()) {
              continue;
            }
            const std::string& base64_str = favicon_val.GetString();
            if (base64_str.empty()) {
              continue;
            }
            std::string decoded_bytes;
            if (!base::Base64Decode(base64_str, &decoded_bytes)) {
              continue;
            }
            result.favicon_map[std::string(tab_id)] =
                std::vector<uint8_t>(decoded_bytes.begin(), decoded_bytes.end());
          }
        }
        if (const base::Value* tree_val = v2_dict->Find("tree");
            tree_val && tree_val->is_list()) {
          result.parsed_tree = tree_val->Clone();
        }
      }
    }
  }

  result.favorites_model =
      MahoSidebarStateAdapter::BuildFavoritesModelForSpaceIdJson(space_id_json);

  return result;
}

MahoSidebarViewStateModel MahoSidebarStateAdapter::BuildViewStateModelFromResult(
    Browser* browser,
    SidebarStateBackgroundResult result) const {
  if (result.footer_state.has_core_state) {
    cycle_space_cache_ = std::make_unique<CycleSpaceCache>();
    cycle_space_cache_->state = std::move(result.footer_state);
  }
  v2_tree_ = std::move(result.parsed_tree);
  v2_favicon_map_ = std::move(result.favicon_map);
  favorites_model_ = std::move(result.favorites_model);

  MahoSidebarViewStateModel model;
  model.top_bar = BuildTopBarModel(browser);
  model.search = BuildSearchModel(browser);
  model.favorites = BuildFavoritesModel(browser);
  model.tab_list = BuildTabListModel(browser);
  model.footer = BuildFooterModel(browser);
  v2_favicon_map_.clear();
  v2_tree_.reset();
  cycle_space_cache_.reset();
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
