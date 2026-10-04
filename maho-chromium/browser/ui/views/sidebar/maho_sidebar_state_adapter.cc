// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"

#include <algorithm>
#include <cstdint>
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
#include "base/memory/raw_ptr_exclusion.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/lock.h"
#include "base/task/single_thread_task_runner.h"
#include "base/thread_annotations.h"
#include "base/timer/elapsed_timer.h"
#include "base/values.h"
#include "chrome/browser/translate/chrome_translate_client.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
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

constexpr bool HasFragment(SidebarCoreFragment fragments,
                           SidebarCoreFragment fragment) {
  return (static_cast<uint8_t>(fragments) & static_cast<uint8_t>(fragment)) !=
         0;
}

std::string ParseSpaceIdJson(const std::string& space_id_json) {
  auto parsed = base::JSONReader::Read(space_id_json, base::JSON_PARSE_RFC);
  return parsed && parsed->is_string() ? parsed->GetString() : std::string();
}

FooterSpaceState FetchFooterState(::MahoCore* core);
SidebarStateBackgroundResult FetchTreeBundle(::MahoCore* core,
                                             const std::string& space_id_json);
MahoSidebarFavoritesModel FetchFavorites(::MahoCore* core,
                                         const std::string& space_id_json);

class SidebarCoreStateCache {
 public:
  SidebarStateBackgroundResult GetOrFetch(::MahoCore* core,
                                          const std::string& space_id_json,
                                          bool bypass_cache,
                                          bool require_current_core = true) {
    SidebarStateBackgroundResult result;
    const uint64_t core_generation = GetCoreGeneration();
    if (!core || bypass_cache) {
      return result;
    }

    const std::string space_id = ParseSpaceIdJson(space_id_json);

    bool fetch_footer = false;
    bool fetch_tree = false;
    bool fetch_favorites = false;
    uint64_t footer_generation = 0;
    uint64_t tree_generation = 0;
    uint64_t favorites_generation = 0;
    SidebarCoreCacheFetchersForTesting fetchers;
    {
      base::AutoLock lock(lock_);
      EnsureCoreLocked(core);
      SpaceEntry* entry = space_id.empty() ? nullptr : &spaces_[space_id];
      footer_generation = footer_.generation;
      if (footer_.value) {
        result.footer_state = *footer_.value;
        ++counters_.footer_hits;
      } else {
        fetch_footer = true;
        ++counters_.footer_misses;
      }
      if (entry) {
        tree_generation = entry->tree_generation;
        favorites_generation = entry->favorites_generation;
      }
      if (entry && entry->tree_valid) {
        if (entry->tree) {
          result.parsed_tree = entry->tree->Clone();
        }
        result.favicon_map = entry->favicon_map;
        ++counters_.tree_bundle_hits;
      } else if (entry) {
        fetch_tree = true;
        ++counters_.tree_bundle_misses;
      }
      if (entry && entry->favorites) {
        result.favorites_model = *entry->favorites;
        ++counters_.favorites_hits;
      } else if (entry) {
        fetch_favorites = true;
        ++counters_.favorites_misses;
      }
      fetchers = fetchers_for_testing_;
    }

    FooterSpaceState fetched_footer;
    SidebarStateBackgroundResult fetched_tree;
    MahoSidebarFavoritesModel fetched_favorites;
    if (fetch_footer) {
      fetched_footer =
          fetchers.footer ? fetchers.footer.Run(core) : FetchFooterState(core);
      result.footer_state = fetched_footer;
    }
    if (fetch_tree) {
      fetched_tree = fetchers.tree_bundle
                         ? fetchers.tree_bundle.Run(core, space_id_json)
                         : FetchTreeBundle(core, space_id_json);
      if (fetched_tree.parsed_tree) {
        result.parsed_tree = fetched_tree.parsed_tree->Clone();
      }
      result.favicon_map = fetched_tree.favicon_map;
    }
    if (fetch_favorites) {
      fetched_favorites = fetchers.favorites
                              ? fetchers.favorites.Run(core, space_id_json)
                              : FetchFavorites(core, space_id_json);
      result.favorites_model = fetched_favorites;
    }

    {
      base::AutoLock lock(lock_);
      if (core_ != core || core_generation_ != core_generation ||
          (require_current_core && (maho::GetCore() != core ||
                                   GetCoreGeneration() != core_generation))) {
        counters_.stale_publications_discarded +=
            fetch_footer + fetch_tree + fetch_favorites;
        return result;
      }
      SpaceEntry* entry = nullptr;
      if (!space_id.empty()) {
        auto it = spaces_.find(space_id);
        if (it != spaces_.end()) {
          entry = &it->second;
        }
      }
      if (fetch_footer) {
        if (footer_.generation == footer_generation) {
          footer_.value = std::move(fetched_footer);
        } else {
          ++counters_.stale_publications_discarded;
        }
      }
      if (fetch_tree) {
        if (entry && entry->tree_generation == tree_generation) {
          // A null/absent tree means the fetch could not produce one (core
          // handle swapped, import gate raised, space_id unparsable) — NOT that
          // the space is genuinely empty. Publishing it as a valid cache entry
          // would serve that empty tree back on every later hit until an
          // unrelated invalidation happened to clear it, extending a momentary
          // failure into a visibly empty sidebar. Leave the entry invalid so
          // the next cycle re-fetches.
          if (fetched_tree.parsed_tree) {
            entry->tree = fetched_tree.parsed_tree->Clone();
            entry->favicon_map = std::move(fetched_tree.favicon_map);
            entry->tree_valid = true;
          }
        } else {
          ++counters_.stale_publications_discarded;
        }
      }
      if (fetch_favorites) {
        if (entry && entry->favorites_generation == favorites_generation) {
          entry->favorites = std::move(fetched_favorites);
        } else {
          ++counters_.stale_publications_discarded;
        }
      }
    }
    return result;
  }

  void EraseSpace(const std::string& space_id) {
    base::AutoLock lock(lock_);
    spaces_.erase(space_id);
  }

  void Invalidate(const SidebarCacheInvalidation& invalidation) {
    base::AutoLock lock(lock_);
    if (HasFragment(invalidation.fragments, SidebarCoreFragment::kFooter)) {
      ++footer_.generation;
      footer_.value.reset();
    }
    const bool all_spaces = invalidation.space_ids.empty();
    for (auto& [space_id, entry] : spaces_) {
      if (!all_spaces && std::find(invalidation.space_ids.begin(),
                                   invalidation.space_ids.end(),
                                   space_id) == invalidation.space_ids.end()) {
        continue;
      }
      if (HasFragment(invalidation.fragments,
                      SidebarCoreFragment::kTreeBundle)) {
        ++entry.tree_generation;
        entry.tree.reset();
        entry.favicon_map.clear();
        entry.tree_valid = false;
      }
      if (HasFragment(invalidation.fragments,
                      SidebarCoreFragment::kFavorites)) {
        ++entry.favorites_generation;
        entry.favorites.reset();
      }
    }
  }

  SidebarCoreCacheCounters counters() const {
    base::AutoLock lock(lock_);
    return counters_;
  }

  void ResetForTesting() {
    base::AutoLock lock(lock_);
    core_ = nullptr;
    footer_ = FooterEntry();
    spaces_.clear();
    counters_ = SidebarCoreCacheCounters();
    fetchers_for_testing_ = SidebarCoreCacheFetchersForTesting();
  }

  void SetFetchersForTesting(SidebarCoreCacheFetchersForTesting fetchers) {
    base::AutoLock lock(lock_);
    fetchers_for_testing_ = std::move(fetchers);
  }

 private:
  struct FooterEntry {
    uint64_t generation = 0;
    std::optional<FooterSpaceState> value;
  };
  struct SpaceEntry {
    uint64_t tree_generation = 0;
    uint64_t favorites_generation = 0;
    bool tree_valid = false;
    std::optional<base::Value> tree;
    base::flat_map<std::string, std::vector<uint8_t>> favicon_map;
    std::optional<MahoSidebarFavoritesModel> favorites;
  };

  void EnsureCoreLocked(::MahoCore* core) EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    if (core_ == core && core_generation_ == GetCoreGeneration()) {
      return;
    }
    if (core_) {
      ++footer_.generation;
      for (auto& [space_id, entry] : spaces_) {
        ++entry.tree_generation;
        ++entry.favorites_generation;
      }
    }
    core_ = core;
    core_generation_ = GetCoreGeneration();
    footer_.value.reset();
    for (auto& [space_id, entry] : spaces_) {
      entry.tree.reset();
      entry.favicon_map.clear();
      entry.tree_valid = false;
      entry.favorites.reset();
    }
  }

  mutable base::Lock lock_;
  RAW_PTR_EXCLUSION ::MahoCore* core_ GUARDED_BY(lock_) = nullptr;
  uint64_t core_generation_ GUARDED_BY(lock_) = 0;
  FooterEntry footer_ GUARDED_BY(lock_);
  std::map<std::string, SpaceEntry> spaces_ GUARDED_BY(lock_);
  SidebarCoreCacheCounters counters_ GUARDED_BY(lock_);
  SidebarCoreCacheFetchersForTesting fetchers_for_testing_ GUARDED_BY(lock_);
};

SidebarCoreStateCache& GetSidebarCoreStateCache() {
  static base::NoDestructor<SidebarCoreStateCache> cache;
  return *cache;
}

}  // namespace

SidebarCoreFragment operator|(SidebarCoreFragment lhs,
                              SidebarCoreFragment rhs) {
  return static_cast<SidebarCoreFragment>(static_cast<uint8_t>(lhs) |
                                          static_cast<uint8_t>(rhs));
}

SidebarCoreFragment operator&(SidebarCoreFragment lhs,
                              SidebarCoreFragment rhs) {
  return static_cast<SidebarCoreFragment>(static_cast<uint8_t>(lhs) &
                                          static_cast<uint8_t>(rhs));
}

bool IsSidebarSafeMode() {
  const base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();
  return cmd && cmd->HasSwitch("maho-sidebar-safe-mode");
}

namespace {

const GURL& GetVisibleOrCommittedUrl(content::WebContents* contents) {
  CHECK(contents);
  return contents->GetVisibleURL().is_empty() ? contents->GetLastCommittedURL()
                                              : contents->GetVisibleURL();
}

std::u16string GetTabTitle(content::WebContents* contents) {
  if (!contents || contents->GetTitle().empty() ||
      MahoDisplayPolicy::IsNtpOrBlankUrl(GetVisibleOrCommittedUrl(contents))) {
    return u"New Tab";
  }
  return contents->GetTitle();
}

std::u16string GetTabHost(content::WebContents* contents) {
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

std::u16string GetTabTitle(TabStripModel* model, int index) {
  if (!model || !model->ContainsIndex(index)) {
    return u"Untitled tab";
  }

  content::WebContents* contents = model->GetWebContentsAt(index);
  if (!contents || contents->GetTitle().empty() ||
      MahoDisplayPolicy::IsNtpOrBlankUrl(GetVisibleOrCommittedUrl(contents))) {
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

// Build a stable-tab-ID-to-strip-index map using MahoTabIdHelper (primary
// path).
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
    const bool is_active =
        (id && !active_stable_id.empty() && *id == active_stable_id);
    const std::string* raw_url = dict->FindString("url");
    const std::string* raw_title = dict->FindString("title");
    const std::string url_str = raw_url ? *raw_url : std::string();
    const std::u16string title_str =
        raw_title ? base::UTF8ToUTF16(*raw_title) : std::u16string();
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

    std::optional<bool> favorite =
        dict->FindBool("isFavorite");             // L3-EXEMPT: legacy compat
    node.is_favorite = favorite.value_or(false);  // L3-EXEMPT: legacy compat

    const std::string* lifecycle = dict->FindString("lifecycleState");
    node.is_suspended = (lifecycle && *lifecycle == "suspended");

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
      if (contents) {
        if (node.custom_title.empty()) {
          node.title = GetTabTitle(contents);
        }
        node.host = GetTabHost(contents);
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
        auto parsed_child =
            ParseTreeNode(child_val, model, stable_id_map, active_stable_id,
                          depth + 1, favicon_map);
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
  // is_pinned is intentionally NOT overlaid from the native strip: it is
  // role-derived from core (Maho pinned != native strip pin) and feeds the
  // row's SidebarDragOrigin, so overwriting it here would misreport the
  // active pinned row as coming from the normal section during drags.
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

// LEGACY: replaced by FetchSidebarStateV2 in BuildViewStateModel. Kept
// compiling for backward compat during transition; will be deleted after
// 1-week soak (4.2.3).
FooterSpaceState GetCoreFooterSpaceStateRaw() {
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
    const MahoSpaceDisplayThemeModel theme =
        BuildMahoSpaceDisplayThemeModel(*dict);
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

namespace {

FooterSpaceState FetchFooterState(::MahoCore* core) {
  if (!core || core != maho::GetCore()) {
    return FooterSpaceState();
  }
  return GetCoreFooterSpaceStateRaw();
}

SidebarStateBackgroundResult FetchTreeBundle(::MahoCore* core,
                                             const std::string& space_id_json) {
  SidebarStateBackgroundResult result;
  char* raw = maho_core_get_sidebar_state_v2(core, space_id_json.c_str());
  if (!raw) {
    return result;
  }
  std::string v2_json(raw);
  maho_string_free(raw);

  auto parsed = base::JSONReader::Read(v2_json, base::JSON_PARSE_RFC);
  const auto* dict = parsed ? parsed->GetIfDict() : nullptr;
  if (!dict) {
    return result;
  }
  if (const auto* favicons = dict->FindDict("favicons")) {
    for (const auto [tab_id, favicon] : *favicons) {
      if (!favicon.is_string() || favicon.GetString().empty()) {
        continue;
      }
      std::string decoded;
      if (base::Base64Decode(favicon.GetString(), &decoded)) {
        result.favicon_map[std::string(tab_id)] =
            std::vector<uint8_t>(decoded.begin(), decoded.end());
      }
    }
  }
  if (const base::Value* tree = dict->Find("tree"); tree && tree->is_list()) {
    result.parsed_tree = tree->Clone();
  }
  return result;
}

MahoSidebarFavoritesModel FetchFavorites(::MahoCore* core,
                                         const std::string& space_id_json) {
  if (!core || core != maho::GetCore()) {
    return MahoSidebarFavoritesModel();
  }
  return MahoSidebarStateAdapter::BuildFavoritesModelForSpaceIdJson(
      space_id_json);
}

std::string FindSpaceId(const base::DictValue& update) {
  if (const std::string* space_id = update.FindString("space_id")) {
    return *space_id;
  }
  if (const std::string* space_id = update.FindString("spaceId")) {
    return *space_id;
  }
  if (const base::DictValue* tab = update.FindDict("tab")) {
    if (const std::string* space_id = tab->FindString("space_id")) {
      return *space_id;
    }
    if (const std::string* space_id = tab->FindString("spaceId")) {
      return *space_id;
    }
  }
  return std::string();
}

SidebarCacheInvalidation InvalidationForUpdate(const base::DictValue& update) {
  SidebarCacheInvalidation invalidation;
  const std::string* kind = update.FindString("kind");
  if (!kind) {
    return invalidation;
  }
  const std::string space_id = FindSpaceId(update);
  auto set_space = [&]() {
    if (!space_id.empty()) {
      invalidation.space_ids.push_back(space_id);
    }
  };

  if (*kind == "full_state") {
    invalidation.fragments = SidebarCoreFragment::kAll;
  } else if (*kind == "tab_created" || *kind == "tab_updated" ||
             *kind == "tab_order_changed" ||
             *kind == "navigation_state_changed" ||
             *kind == "tab_favicon_changed" || *kind == "tab_role_changed") {
    invalidation.fragments =
        SidebarCoreFragment::kTreeBundle | SidebarCoreFragment::kFavorites;
    set_space();
  } else if (*kind == "tab_closed") {
    invalidation.fragments =
        SidebarCoreFragment::kTreeBundle | SidebarCoreFragment::kFavorites;
  } else if (*kind == "tab_lifecycle_changed") {
    invalidation.fragments = SidebarCoreFragment::kTreeBundle;
    set_space();
  } else if (*kind == "tabs_migrated") {
    invalidation.fragments =
        SidebarCoreFragment::kTreeBundle | SidebarCoreFragment::kFavorites;
    if (const std::string* from = update.FindString("from_space_id")) {
      invalidation.space_ids.push_back(*from);
    }
    if (const std::string* to = update.FindString("to_space_id")) {
      invalidation.space_ids.push_back(*to);
    }
  } else if (*kind == "folder_created" || *kind == "folder_updated" ||
             *kind == "folder_deleted") {
    invalidation.fragments = SidebarCoreFragment::kTreeBundle;
  } else if (*kind == "folder_order_changed") {
    invalidation.fragments = SidebarCoreFragment::kTreeBundle;
    set_space();
  } else if (*kind == "space_created" || *kind == "space_updated" ||
             *kind == "space_order_changed" ||
             *kind == "active_space_changed" || *kind == "profile_created" ||
             *kind == "profile_updated" || *kind == "profile_deleted" ||
             *kind == "active_profile_changed" || *kind == "settings_changed") {
    invalidation.fragments = SidebarCoreFragment::kFooter;
  } else if (*kind == "space_deleted") {
    invalidation.fragments = SidebarCoreFragment::kAll;
    set_space();
  } else if (*kind == "space_config_updated") {
    invalidation.fragments = SidebarCoreFragment::kAll;
    set_space();
  } else if (*kind == "split_view_changed") {
    // Split grouping and ratios are overlaid from the live TabStripModel after
    // the cached core tree is parsed; no cached fragment contains split state.
  } else if (*kind == "favorite_limit_reached") {
    // The attempted role transition was rejected, so cached tree/favorites
    // state did not change.
  }
  return invalidation;
}

}  // namespace

void GroupSplitTabs(std::vector<SidebarTreeNode>& nodes, TabStripModel* model) {
  for (auto& node : nodes) {
    if (!node.children.empty()) {
      GroupSplitTabs(node.children, model);
    }
  }

  std::map<split_tabs::SplitTabId, std::vector<size_t>> split_groups;
  std::vector<std::optional<split_tabs::SplitTabId>> node_splits;
  node_splits.resize(nodes.size());
  for (size_t i = 0; i < nodes.size(); ++i) {
    const auto& node = nodes[i];
    if (node.kind != SidebarNodeKind::kTab || node.tab_strip_index < 0) {
      continue;
    }
    auto split_id = model->GetSplitForTab(node.tab_strip_index);
    node_splits[i] = split_id;
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
      const auto& split_id = node_splits[i];
      if (split_id.has_value()) {
        auto it = split_groups.find(split_id.value());
        if (it != split_groups.end() && it->second.size() >= 2) {
          SidebarTreeNode group;
          group.kind = SidebarNodeKind::kSplitGroup;
          group.split_id = split_id.value().ToString();
          std::vector<size_t> ordered_indices = it->second;
          std::vector<tabs::TabInterface*> split_tab_interfaces;

          if (auto* split_data = model->GetSplitData(split_id.value())) {
            split_tab_interfaces = split_data->ListTabs();
            group.split_ratio = split_data->visual_data()->split_ratio();
            group.split_orientation =
                (split_data->visual_data()->split_layout() ==
                 split_tabs::SplitTabLayout::kSideBySide)
                    ? "vertical"
                    : "horizontal";
          } else {
            group.split_ratio = 0.5;
            group.split_orientation = "vertical";
          }

          auto split_order_for_node = [&](size_t node_index) -> size_t {
            if (node_index >= nodes.size() ||
                !model->ContainsIndex(nodes[node_index].tab_strip_index)) {
              return split_tab_interfaces.size();
            }
            content::WebContents* contents =
                model->GetWebContentsAt(nodes[node_index].tab_strip_index);
            for (size_t order = 0; order < split_tab_interfaces.size();
                 ++order) {
              if (split_tab_interfaces[order] &&
                  split_tab_interfaces[order]->GetContents() == contents) {
                return order;
              }
            }
            return split_tab_interfaces.size();
          };

          std::stable_sort(ordered_indices.begin(), ordered_indices.end(),
                           [&](size_t lhs, size_t rhs) {
                             const size_t lhs_order = split_order_for_node(lhs);
                             const size_t rhs_order = split_order_for_node(rhs);
                             if (lhs_order != rhs_order) {
                               return lhs_order < rhs_order;
                             }
                             return nodes[lhs].tab_strip_index <
                                    nodes[rhs].tab_strip_index;
                           });

          DVLOG(1) << "[MAHO_SPLIT_SIDE_DBG] GroupSplitTabs split_id="
                   << group.split_id
                   << " encountered0_id=" << nodes[it->second[0]].tab_id
                   << " encountered0_index="
                   << nodes[it->second[0]].tab_strip_index
                   << " encountered1_id=" << nodes[it->second[1]].tab_id
                   << " encountered1_index="
                   << nodes[it->second[1]].tab_strip_index
                   << " emitted0_id=" << nodes[ordered_indices[0]].tab_id
                   << " emitted0_index="
                   << nodes[ordered_indices[0]].tab_strip_index
                   << " emitted1_id=" << nodes[ordered_indices[1]].tab_id
                   << " emitted1_index="
                   << nodes[ordered_indices[1]].tab_strip_index;

          group.depth = node.depth;
          // Both split panes are shown together in the viewport, so if either
          // pane is the active tab, both must render as active.
          bool split_has_active = false;
          for (size_t idx : it->second) {
            if (nodes[idx].is_active) {
              split_has_active = true;
              break;
            }
          }
          for (size_t idx : ordered_indices) {
            nodes[idx].is_in_split = true;
            if (split_has_active) {
              nodes[idx].is_active = true;
            }
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

FooterSpaceState::FooterSpaceState() = default;
FooterSpaceState::FooterSpaceState(const FooterSpaceState&) = default;
FooterSpaceState& FooterSpaceState::operator=(const FooterSpaceState&) =
    default;
FooterSpaceState::FooterSpaceState(FooterSpaceState&&) = default;
FooterSpaceState& FooterSpaceState::operator=(FooterSpaceState&&) = default;
FooterSpaceState::~FooterSpaceState() = default;

SidebarStateBackgroundResult::SidebarStateBackgroundResult() = default;
SidebarStateBackgroundResult::SidebarStateBackgroundResult(
    SidebarStateBackgroundResult&&) = default;
SidebarStateBackgroundResult& SidebarStateBackgroundResult::operator=(
    SidebarStateBackgroundResult&&) = default;
SidebarStateBackgroundResult::~SidebarStateBackgroundResult() = default;

SidebarCacheInvalidation::SidebarCacheInvalidation() = default;
SidebarCacheInvalidation::SidebarCacheInvalidation(
    const SidebarCacheInvalidation&) = default;
SidebarCacheInvalidation& SidebarCacheInvalidation::operator=(
    const SidebarCacheInvalidation&) = default;
SidebarCacheInvalidation::SidebarCacheInvalidation(
    SidebarCacheInvalidation&&) = default;
SidebarCacheInvalidation& SidebarCacheInvalidation::operator=(
    SidebarCacheInvalidation&&) = default;
SidebarCacheInvalidation::~SidebarCacheInvalidation() = default;

SidebarCoreCacheFetchersForTesting::SidebarCoreCacheFetchersForTesting() =
    default;
SidebarCoreCacheFetchersForTesting::SidebarCoreCacheFetchersForTesting(
    const SidebarCoreCacheFetchersForTesting&) = default;
SidebarCoreCacheFetchersForTesting&
SidebarCoreCacheFetchersForTesting::operator=(
    const SidebarCoreCacheFetchersForTesting&) = default;
SidebarCoreCacheFetchersForTesting::SidebarCoreCacheFetchersForTesting(
    SidebarCoreCacheFetchersForTesting&&) = default;
SidebarCoreCacheFetchersForTesting&
SidebarCoreCacheFetchersForTesting::operator=(
    SidebarCoreCacheFetchersForTesting&&) = default;
SidebarCoreCacheFetchersForTesting::~SidebarCoreCacheFetchersForTesting() =
    default;

void SetShellEventObserverForTesting(ShellEventObserver* observer) {
  g_shell_event_observer_for_testing = observer;
}

SidebarCacheInvalidation GetSidebarCacheInvalidationForUpdateJsonForTesting(
    std::string_view update_json) {
  auto parsed = base::JSONReader::Read(update_json, base::JSON_PARSE_RFC);
  const auto* dict = parsed ? parsed->GetIfDict() : nullptr;
  return dict ? InvalidationForUpdate(*dict) : SidebarCacheInvalidation();
}

void InvalidateSidebarCoreCache(
    const SidebarCacheInvalidation& invalidation) {
  GetSidebarCoreStateCache().Invalidate(invalidation);
}

void InvalidateSidebarCoreCacheForUpdatesJson(std::string_view updates_json) {
  auto parsed = base::JSONReader::Read(updates_json, base::JSON_PARSE_RFC);
  const auto* updates = parsed ? parsed->GetIfList() : nullptr;
  if (!updates) {
    return;
  }
  for (const auto& update : *updates) {
    if (const auto* dict = update.GetIfDict()) {
      const std::string* kind = dict->FindString("kind");
      if (kind && *kind == "space_deleted") {
        std::string space_id = FindSpaceId(*dict);
        if (!space_id.empty()) {
          GetSidebarCoreStateCache().EraseSpace(space_id);
        }
      }
      GetSidebarCoreStateCache().Invalidate(InvalidationForUpdate(*dict));
    }
  }
}

SidebarCoreCacheCounters GetSidebarCoreCacheCountersForTesting() {
  return GetSidebarCoreStateCache().counters();
}

void ResetSidebarCoreCacheForTesting() {
  GetSidebarCoreStateCache().ResetForTesting();
}

void SetSidebarCoreCacheFetchersForTesting(
    SidebarCoreCacheFetchersForTesting fetchers) {
  GetSidebarCoreStateCache().SetFetchersForTesting(std::move(fetchers));
}

SidebarStateBackgroundResult BuildSidebarStateOnBackgroundForTesting(
    ::MahoCore* core,
    std::string space_id_json,
    bool bypass_cache) {
  return GetSidebarCoreStateCache().GetOrFetch(
      core, space_id_json, bypass_cache, /*require_current_core=*/false);
}

MahoSidebarFavoritesModel RefreshSidebarFavoritesForTesting(
    ::MahoCore* core,
    std::string space_id_json) {
  const std::string space_id = ParseSpaceIdJson(space_id_json);
  if (!core || space_id.empty()) {
    return MahoSidebarFavoritesModel();
  }
  SidebarCacheInvalidation invalidation;
  invalidation.fragments = SidebarCoreFragment::kFavorites;
  invalidation.space_ids.push_back(space_id);
  GetSidebarCoreStateCache().Invalidate(invalidation);
  return GetSidebarCoreStateCache()
      .GetOrFetch(core, space_id_json, /*bypass_cache=*/false,
                  /*require_current_core=*/false)
      .favorites_model;
}

void NotifyChangedHelper(const std::string& kind) {
  if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance(); bridge) {
    const bool is_structural = !(
        kind == "tab_title_updated" || kind == "tab_url_updated" ||
        kind == "tab_favicon_updated" || kind == "tab_loading_state_changed" ||
        kind == "update_tab_scroll_position" || kind == "update_tab_preview");
    if (is_structural) {
      bridge->NotifyChanged(true);
    } else {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(
              [](bool is_structural) {
                if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
                    bridge) {
                  bridge->NotifyChanged(is_structural);
                }
              },
              false),
          base::Milliseconds(16));
    }
  }
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
  DispatchCoreEvent(
      std::move(json),
      base::BindOnce([](std::string kind, CoreEventResult result) {
        if (result.status != CoreEventStatus::kApplied) {
          LOG(WARNING) << "Sidebar core event did not apply: "
                       << static_cast<int>(result.status);
          return;
        }
        InvalidateSidebarCoreCacheForUpdatesJson(result.updates_json);
        NotifyChangedHelper(kind);
        MahoBoostInjectionHandler::OnCoreUpdatesJson(result.updates_json);
      }, kind));
}

std::string DispatchCreateFolder(const std::string& space_id,
                                 const std::string& name) {
  if (space_id.empty()) {
    return std::string();
  }
  base::DictValue event;
  event.Set("kind", "create_folder");
  event.Set("space_id", space_id);
  event.Set("name", name);
  std::string json;
  base::JSONWriter::Write(event, &json);
  if (g_shell_event_observer_for_testing) {
    g_shell_event_observer_for_testing->OnShellEventDispatched("create_folder",
                                                               json);
  }
  ::MahoCore* core = maho::GetCore();
  if (!core) {
    return std::string();
  }
  std::string updates = maho::core::HandleEvent(core, json.c_str());
  InvalidateSidebarCoreCacheForUpdatesJson(updates);
  NotifyChangedHelper("create_folder");
  MahoBoostInjectionHandler::OnCoreUpdatesJson(updates);

  auto parsed = base::JSONReader::Read(updates, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return std::string();
  }
  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* kind = dict->FindString("kind");
    if (!kind || *kind != "folder_created") {
      continue;
    }
    const auto* folder = dict->FindDict("folder");
    if (!folder) {
      continue;
    }
    const std::string* id = folder->FindString("id");
    if (id && !id->empty()) {
      return *id;
    }
  }
  return std::string();
}

void DispatchShellEventEx(const std::string& kind,
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
  DispatchCoreEvent(
      std::move(json),
      base::BindOnce([](std::string kind, CoreEventResult result) {
        if (result.status != CoreEventStatus::kApplied) {
          LOG(WARNING) << "Sidebar core event did not apply: "
                       << static_cast<int>(result.status);
          return;
        }
        InvalidateSidebarCoreCacheForUpdatesJson(result.updates_json);
        NotifyChangedHelper(kind);
        MahoBoostInjectionHandler::OnCoreUpdatesJson(result.updates_json);
      }, kind));
}

void DispatchShellEventDict(const std::string& kind, base::DictValue dict) {
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
  DispatchCoreEvent(
      std::move(json),
      base::BindOnce([](std::string kind, CoreEventResult result) {
        if (result.status != CoreEventStatus::kApplied) {
          LOG(WARNING) << "Sidebar core event did not apply: "
                       << static_cast<int>(result.status);
          return;
        }
        InvalidateSidebarCoreCacheForUpdatesJson(result.updates_json);
        NotifyChangedHelper(kind);
        MahoBoostInjectionHandler::OnCoreUpdatesJson(result.updates_json);
      }, kind));
}

struct MahoSidebarStateAdapter::CycleSpaceCache {
  FooterSpaceState state;
};

MahoSidebarStateAdapter::MahoSidebarStateAdapter() = default;

MahoSidebarStateAdapter::~MahoSidebarStateAdapter() = default;

MahoSidebarTopBarModel MahoSidebarStateAdapter::BuildTopBarModel(
    Browser* browser) const {
  MahoSidebarTopBarModel model;
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

  char* json_str =
      maho_core_get_favorite_tabs(core, active_space_id_json.c_str());
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
    // Preserve the raw authored name separately: the edit dialog prefills
    // with this, never with the resolved projection in |title|.
    favorite_item.custom_title = custom_title_u16;

    const std::string* id = dict->FindString("id");
    if (id) {
      favorite_item.tab_id = *id;
    }
    const std::string* favicon_b64 = dict->FindString("favicon");
    if (favicon_b64 && !favicon_b64->empty()) {
      std::string decoded_bytes;
      if (base::Base64Decode(*favicon_b64, &decoded_bytes)) {
        favorite_item.favicon_png_data =
            std::vector<uint8_t>(decoded_bytes.begin(), decoded_bytes.end());
      }
    }
    // Optional user overrides: a curated glyph replacing the favicon, and a
    // pinned destination URL replacing the live tab URL. Both keys are omitted
    // by maho-core when unset, which maps to the empty field default.
    const std::string* custom_icon = dict->FindString("customIcon");
    if (custom_icon && !custom_icon->empty()) {
      favorite_item.custom_icon = base::UTF8ToUTF16(*custom_icon);
    }
    const std::string* pinned_url = dict->FindString("pinnedUrl");
    if (pinned_url && !pinned_url->empty()) {
      favorite_item.pinned_url = *pinned_url;
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

// static
MahoSidebarFavoritesModel
MahoSidebarStateAdapter::RefreshFavoritesModelForSpaceIdJson(
    std::string active_space_id_json) {
  ::MahoCore* core = maho::GetCore();
  const std::string space_id = ParseSpaceIdJson(active_space_id_json);
  if (!core || space_id.empty() || IsSidebarSafeMode()) {
    return MahoSidebarFavoritesModel();
  }
  return RefreshSidebarFavoritesForTesting(core,
                                           std::move(active_space_id_json));
}

MahoSidebarFavoritesModel MahoSidebarStateAdapter::BuildFavoritesModel(
    Browser* browser) const {
  MahoSidebarFavoritesModel model = favorites_model_;
  if (!browser || !browser->GetTabStripModel()) {
    return model;
  }
  content::WebContents* active =
      browser->GetTabStripModel()->GetActiveWebContents();
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
  TabStripModel* tab_strip_model =
      browser ? browser->GetTabStripModel() : nullptr;

  model.active_space_id = GetRawActiveSpaceIdForBrowser(browser);
  std::string space_id_json = GetActiveSpaceIdJsonForBrowser(browser);

  std::string active_stable_id;
  content::WebContents* active_contents = nullptr;
  int active_index = TabStripModel::kNoTab;

  if (tab_strip_model) {
    active_index = tab_strip_model->active_index();
    if (tab_strip_model->ContainsIndex(active_index)) {
      active_contents = tab_strip_model->GetWebContentsAt(active_index);
      if (active_contents) {
        model.active_tab.index = active_index;
        model.active_tab.title = GetTabTitle(active_contents);
        model.active_tab.host = GetTabHost(active_contents);
        if (auto* helper = MahoTabIdHelper::FromWebContents(active_contents)) {
          active_stable_id = helper->stable_tab_id();
          // Without this the fp_match fast path sees active_tab.tab_id=="" and
          // clears the highlight a row click just applied (RC-H1).
          model.active_tab.tab_id = active_stable_id;
        }
      }
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
    auto parsed_tree = std::move(v2_tree_);
    base::flat_map<std::string, std::vector<uint8_t>> favicon_map =
        std::move(v2_favicon_map_);
    model.tree_unavailable = !(parsed_tree && parsed_tree->is_list());

    DVLOG(1) << "BuildTabListModel: tree_requested=true, parsed_tree exists="
             << (parsed_tree.has_value())
             << " is_list=" << (parsed_tree ? parsed_tree->is_list() : false)
             << " size="
             << (parsed_tree && parsed_tree->is_list()
                     ? parsed_tree->GetList().size()
                     : 0);
    if (parsed_tree && parsed_tree->is_list()) {
      for (const auto& root_val : parsed_tree->GetList()) {
        auto parsed_node =
            ParseTreeNode(root_val, tab_strip_model, stable_id_map,
                          active_stable_id, 0, favicon_map);
        DVLOG(1) << "BuildTabListModel: parsed_node has_value="
                 << parsed_node.has_value();
        if (parsed_node.has_value()) {
          SidebarTreeNode node = std::move(parsed_node.value());
          DVLOG(1) << "BuildTabListModel: parsed node kind=" << (int)node.kind
                   << " tab_id=" << node.tab_id
                   << " is_pinned=" << node.is_pinned
                   << " folder_is_pinned=" << node.folder_is_pinned;
          if ((node.kind == SidebarNodeKind::kTab && node.is_pinned) ||
              (node.kind == SidebarNodeKind::kFolder &&
               node.folder_is_pinned)) {
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
    if (tab_strip_model) {
      GroupSplitTabs(model.pinned_tree, tab_strip_model);
      GroupSplitTabs(model.normal_tree, tab_strip_model);
    }
  }

  // Active-tab live overlay: refresh live fields (URL/title/loading/audio) on
  // the active tab's row so that stale persisted state (e.g. customTitle from
  // a previous navigation) never wins over the real page state. The row is
  // guaranteed to exist in the tree because BuildTreeSection exempts the
  // active entry from URL/ghost filters via active_stable_id.
  if (tab_strip_model && active_index != TabStripModel::kNoTab) {
    if (!FindAndOverlayActiveTabInTree(model.pinned_tree, active_index,
                                       active_stable_id, tab_strip_model,
                                       active_contents)) {
      FindAndOverlayActiveTabInTree(model.normal_tree, active_index,
                                    active_stable_id, tab_strip_model,
                                    active_contents);
    }
  }

  FooterSpaceState space_state;
  if (cycle_space_cache_) {
    space_state = cycle_space_cache_->state;
  } else if (!IsSidebarSafeMode()) {
    space_state = GetFooterSpaceState();
  }
  const int idx = space_state.active_space_index;
  if (idx >= 0 && idx < static_cast<int>(space_state.space_names.size()) &&
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
    core_state = GetFooterSpaceState();
  }
  const auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  const size_t bridge_space_count =
      bridge ? bridge->GetRegisteredSpaceCount() : 0u;

  const size_t space_count =
      core_state.has_core_state ? core_state.space_count : bridge_space_count;
  model.status_text = space_count > 0 ? u"Spaces" : u"No spaces";
  model.space_count = static_cast<int>(space_count);
  int active_space_index =
      core_state.has_core_state
          ? core_state.active_space_index
          : (bridge ? bridge->GetActiveSpaceIndex(browser) : -1);
  std::string active_space_color =
      core_state.has_core_state ? core_state.active_space_color : std::string();
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
    ::MahoCore* core,
    const std::string& space_id_json) const {
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
    cycle_space_cache_->state = GetFooterSpaceState();
    DVLOG(1) << "[MAHO_PERF]     GetCoreFooterSpaceState="
             << footer_t.Elapsed().InMillisecondsF() << "ms";
  }

  std::string space_id_json = GetActiveSpaceIdJsonForBrowser(browser);
  base::flat_map<std::string, std::vector<uint8_t>> favicon_map;

  ::MahoCore* core = maho::GetCore();
  if (core && !space_id_json.empty() && !IsSidebarSafeMode()) {
    base::ElapsedTimer v2_t;
    std::string v2_json = FetchSidebarStateV2(core, space_id_json);
    DVLOG(1) << "[MAHO_PERF]     FetchSidebarStateV2="
             << v2_t.Elapsed().InMillisecondsF()
             << "ms bytes=" << v2_json.size() << " json=" << v2_json;
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
  model.favorites = BuildFavoritesModel(browser);
  model.tab_list = BuildTabListModel(browser);
  model.footer = BuildFooterModel(browser);
  DVLOG(1) << "[MAHO_PERF]     BuildXXXModel(top+fav+tabList+footer)="
           << build_t.Elapsed().InMillisecondsF() << "ms";
  v2_favicon_map_.clear();
  v2_tree_.reset();
  cycle_space_cache_.reset();
  DVLOG(1) << "[MAHO_PERF]   BuildViewStateModel TOTAL="
           << maho_perf_t.Elapsed().InMillisecondsF() << "ms";
  return model;
}

// static
SidebarStateBackgroundResult MahoSidebarStateAdapter::BuildStateOnBackground(
    std::string space_id_json,
    bool safe_mode) {
  DVLOG(1) << "BuildStateOnBackground start";
  ::MahoCore* core = maho::GetCore();
  SidebarStateBackgroundResult result =
      GetSidebarCoreStateCache().GetOrFetch(core, space_id_json, safe_mode);
  result.core_generation = GetCoreGeneration();
  result.tab_facts = ReadCoreTabFactsSnapshot(core);
  const SidebarCoreCacheCounters counters =
      GetSidebarCoreStateCache().counters();
  DVLOG(1) << "[MAHO_SIDEBAR_CACHE] footer=" << counters.footer_hits << "/"
           << counters.footer_misses << " tree=" << counters.tree_bundle_hits
           << "/" << counters.tree_bundle_misses
           << " favorites=" << counters.favorites_hits << "/"
           << counters.favorites_misses
           << " stale=" << counters.stale_publications_discarded;
  DVLOG(1) << "BuildStateOnBackground end";
  return result;
}

MahoSidebarViewStateModel
MahoSidebarStateAdapter::BuildViewStateModelFromResult(
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
  model.favorites = BuildFavoritesModel(browser);
  model.tab_list = BuildTabListModel(browser);
  model.footer = BuildFooterModel(browser);
  v2_favicon_map_.clear();
  v2_tree_.reset();
  cycle_space_cache_.reset();
  return model;
}

void MahoSidebarStateAdapter::InvalidateFooterState() {
  footer_state_is_dirty_ = true;
}

FooterSpaceState MahoSidebarStateAdapter::GetFooterSpaceState() const {
  if (footer_state_is_dirty_) {
    cached_footer_state_ = GetCoreFooterSpaceStateRaw();
    footer_state_is_dirty_ = false;
  }
  return cached_footer_state_;
}

}  // namespace maho
