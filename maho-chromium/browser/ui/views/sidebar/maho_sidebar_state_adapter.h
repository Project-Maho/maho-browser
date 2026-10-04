// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_STATE_ADAPTER_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_STATE_ADAPTER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/functional/callback.h"
#include "base/values.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "maho/browser/maho_core_holder.h"
#include "ui/gfx/image/image.h"

class Browser;
class TabStripModel;

// Forward declaration of the opaque FFI type (defined in maho_ffi.h).
struct MahoCore;

namespace maho {

// Holds the result of Phase 0 (footer space state FFI) so it can be returned
// from a background task without touching UI objects.
struct FooterSpaceState {
  FooterSpaceState();
  FooterSpaceState(const FooterSpaceState&);
  FooterSpaceState& operator=(const FooterSpaceState&);
  FooterSpaceState(FooterSpaceState&&);
  FooterSpaceState& operator=(FooterSpaceState&&);
  ~FooterSpaceState();

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

// Result of background Phase 0 + Phase 2 work (FFI + JSON parse + base64
// decode). Returned by value from ThreadPool, consumed on UI thread.
struct SidebarStateBackgroundResult {
  uint64_t core_generation = 0;
  std::vector<CoreTabFacts> tab_facts;
  SidebarStateBackgroundResult();
  SidebarStateBackgroundResult(SidebarStateBackgroundResult&&);
  SidebarStateBackgroundResult& operator=(SidebarStateBackgroundResult&&);
  ~SidebarStateBackgroundResult();

  FooterSpaceState footer_state;
  std::optional<base::Value> parsed_tree;
  base::flat_map<std::string, std::vector<uint8_t>> favicon_map;
  MahoSidebarFavoritesModel favorites_model;
};

enum class SidebarCoreFragment : uint8_t {
  kNone = 0,
  kFooter = 1 << 0,
  kTreeBundle = 1 << 1,
  kFavorites = 1 << 2,
  kAll = (1 << 0) | (1 << 1) | (1 << 2),
};

SidebarCoreFragment operator|(SidebarCoreFragment lhs, SidebarCoreFragment rhs);
SidebarCoreFragment operator&(SidebarCoreFragment lhs, SidebarCoreFragment rhs);

struct SidebarCacheInvalidation {
  SidebarCacheInvalidation();
  SidebarCacheInvalidation(const SidebarCacheInvalidation&);
  SidebarCacheInvalidation& operator=(const SidebarCacheInvalidation&);
  SidebarCacheInvalidation(SidebarCacheInvalidation&&);
  SidebarCacheInvalidation& operator=(SidebarCacheInvalidation&&);
  ~SidebarCacheInvalidation();

  SidebarCoreFragment fragments = SidebarCoreFragment::kNone;
  // Empty means every space for per-space fragments.
  std::vector<std::string> space_ids;
};

struct SidebarCoreCacheCounters {
  uint64_t footer_hits = 0;
  uint64_t footer_misses = 0;
  uint64_t tree_bundle_hits = 0;
  uint64_t tree_bundle_misses = 0;
  uint64_t favorites_hits = 0;
  uint64_t favorites_misses = 0;
  uint64_t stale_publications_discarded = 0;
};

struct SidebarCoreCacheFetchersForTesting {
  SidebarCoreCacheFetchersForTesting();
  SidebarCoreCacheFetchersForTesting(const SidebarCoreCacheFetchersForTesting&);
  SidebarCoreCacheFetchersForTesting& operator=(
      const SidebarCoreCacheFetchersForTesting&);
  SidebarCoreCacheFetchersForTesting(SidebarCoreCacheFetchersForTesting&&);
  SidebarCoreCacheFetchersForTesting& operator=(
      SidebarCoreCacheFetchersForTesting&&);
  ~SidebarCoreCacheFetchersForTesting();

  base::RepeatingCallback<FooterSpaceState(::MahoCore*)> footer;
  base::RepeatingCallback<SidebarStateBackgroundResult(::MahoCore*,
                                                       const std::string&)>
      tree_bundle;
  base::RepeatingCallback<MahoSidebarFavoritesModel(::MahoCore*,
                                                    const std::string&)>
      favorites;
};

// Invalidates explicitly classified fragments for typed FFI mutations that do
// not return CoreUpdates.
void InvalidateSidebarCoreCache(const SidebarCacheInvalidation& invalidation);

// Parses authoritative CoreUpdate JSON returned by HandleEvent and invalidates
// only the affected cached core fragments.
void InvalidateSidebarCoreCacheForUpdatesJson(std::string_view updates_json);

// Test-only cache controls and instrumentation.
SidebarCacheInvalidation GetSidebarCacheInvalidationForUpdateJsonForTesting(
    std::string_view update_json);
SidebarCoreCacheCounters GetSidebarCoreCacheCountersForTesting();
void ResetSidebarCoreCacheForTesting();
void SetSidebarCoreCacheFetchersForTesting(
    SidebarCoreCacheFetchersForTesting fetchers);
SidebarStateBackgroundResult BuildSidebarStateOnBackgroundForTesting(
    ::MahoCore* core,
    std::string space_id_json,
    bool bypass_cache);
MahoSidebarFavoritesModel RefreshSidebarFavoritesForTesting(
    ::MahoCore* core,
    std::string space_id_json);
std::string DispatchSpaceContextMenuCoreEventForTesting(
    ::MahoCore* core,
    const std::string& event_json);
void RecolorSpaceContextMenuForTesting(::MahoCore* core,
                                       const std::string& space_id,
                                       const std::string& color);

class ShellEventObserver {
 public:
  virtual ~ShellEventObserver() = default;
  virtual void OnShellEventDispatched(const std::string& kind,
                                      const std::string& event_json) = 0;
};

// Test-only hook for observing native shell event dispatch without changing
// production behavior.
void SetShellEventObserverForTesting(ShellEventObserver* observer);

// Centralized ShellEvent dispatch: builds JSON and sends to maho-core.
void DispatchShellEvent(
    const std::string& kind,
    const std::vector<std::pair<std::string, std::string>>& fields);

// Creates a folder and returns the generated folder id (empty on failure).
std::string DispatchCreateFolder(const std::string& space_id,
                                 const std::string& name);

// Variant for events with mixed string and integer fields.
struct ShellEventField {
  enum class Type { kString, kInt, kBool };

  std::string key;
  std::string string_value;
  int int_value = 0;
  bool bool_value = false;
  Type type = Type::kString;

  ShellEventField(std::string k, std::string v)
      : key(std::move(k)), string_value(std::move(v)), type(Type::kString) {}
  ShellEventField(std::string k, int v)
      : key(std::move(k)), int_value(v), type(Type::kInt) {}
  ShellEventField(std::string k, bool v)
      : key(std::move(k)), bool_value(v), type(Type::kBool) {}
};
void DispatchShellEventEx(const std::string& kind,
                          const std::vector<ShellEventField>& fields);

// Variant for events with nested JSON structure (e.g. ReorderRootItem).
void DispatchShellEventDict(const std::string& kind, base::DictValue dict);

bool IsSidebarSafeMode();

// Regroups paired split tabs into a single kSplitGroup node with two ordered
// children, marking both is_in_split and both is_active when either member is
// the model's active tab. Needs a live TabStripModel; shared by the core-tree
// and both fallback builders so every path renders splits identically.
void GroupSplitTabs(std::vector<SidebarTreeNode>& nodes, TabStripModel* model);

class MahoSidebarStateAdapter {
 public:
  MahoSidebarStateAdapter();
  MahoSidebarStateAdapter(const MahoSidebarStateAdapter&) = delete;
  MahoSidebarStateAdapter& operator=(const MahoSidebarStateAdapter&) = delete;
  ~MahoSidebarStateAdapter();

  MahoSidebarTopBarModel BuildTopBarModel(Browser* browser) const;
  static MahoSidebarFavoritesModel BuildFavoritesModelForSpaceIdJson(
      std::string active_space_id_json);
  // Invalidates, fetches, and republishes the favorites fragment atomically
  // with respect to the shared cache. Use for synchronous post-drop refreshes
  // so the following full rebuild cannot clobber fresh UI with an older entry.
  static MahoSidebarFavoritesModel RefreshFavoritesModelForSpaceIdJson(
      std::string active_space_id_json);
  void SetFavoritesModel(MahoSidebarFavoritesModel favorites_model);
  MahoSidebarFavoritesModel BuildFavoritesModel(Browser* browser) const;
  MahoSidebarTabListModel BuildTabListModel(Browser* browser) const;
  MahoSidebarFooterModel BuildFooterModel(Browser* browser) const;
  MahoSidebarViewStateModel BuildViewStateModel(Browser* browser) const;

  // Thread-safe: no mutable member access, no Browser* dependency.
  static SidebarStateBackgroundResult BuildStateOnBackground(
      std::string space_id_json,
      bool safe_mode);

  // UI-thread only: installs background result into mutable caches,
  // then builds UI-dependent models.
  MahoSidebarViewStateModel BuildViewStateModelFromResult(
      Browser* browser,
      SidebarStateBackgroundResult result) const;

  void InvalidateFooterState();
  FooterSpaceState GetFooterSpaceState() const;

 private:
  struct CycleSpaceCache;
  mutable std::unique_ptr<CycleSpaceCache> cycle_space_cache_;
  mutable MahoSidebarFavoritesModel favorites_model_;

  // V2 FFI: single call replacing 3 separate v1 FFI hops.
  std::string FetchSidebarStateV2(::MahoCore* core,
                                  const std::string& space_id_json) const;

  // Prebuilt favicon map from v2 FFI (base64 decoded PNG bytes keyed by tab
  // ID). Set during BuildViewStateModel, consumed by ParseTreeNode, cleared
  // after.
  mutable base::flat_map<std::string, std::vector<uint8_t>> v2_favicon_map_;

  // Parsed tree from v2 FFI. When non-null, BuildTabListModel uses this.
  mutable std::optional<base::Value> v2_tree_;

  mutable FooterSpaceState cached_footer_state_;
  mutable bool footer_state_is_dirty_ = true;
};

FooterSpaceState GetCoreFooterSpaceStateRaw();

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_STATE_ADAPTER_H_
