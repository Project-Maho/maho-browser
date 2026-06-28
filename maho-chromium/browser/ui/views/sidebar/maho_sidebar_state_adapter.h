// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_STATE_ADAPTER_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_STATE_ADAPTER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/values.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "ui/gfx/image/image.h"

class Browser;

// Forward declaration of the opaque FFI type (defined in maho_ffi.h).
struct MahoCore;

namespace maho {

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
void DispatchShellEvent(const std::string& kind,
                        const std::vector<std::pair<std::string, std::string>>& fields);

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
void DispatchShellEventDict(const std::string& kind,
                            base::DictValue dict);

class MahoSidebarStateAdapter {
 public:
  MahoSidebarStateAdapter();
  MahoSidebarStateAdapter(const MahoSidebarStateAdapter&) = delete;
  MahoSidebarStateAdapter& operator=(const MahoSidebarStateAdapter&) = delete;
  ~MahoSidebarStateAdapter();

  MahoSidebarTopBarModel BuildTopBarModel(Browser* browser) const;
  MahoSidebarSearchModel BuildSearchModel(Browser* browser) const;
  static MahoSidebarFavoritesModel BuildFavoritesModelForSpaceIdJson(
      std::string active_space_id_json);
  void SetFavoritesModel(MahoSidebarFavoritesModel favorites_model);
  MahoSidebarFavoritesModel BuildFavoritesModel(Browser* browser) const;
  MahoSidebarTabListModel BuildTabListModel(Browser* browser) const;
  MahoSidebarFooterModel BuildFooterModel(Browser* browser) const;
  MahoSidebarViewStateModel BuildViewStateModel(Browser* browser) const;

 private:
  struct CycleSpaceCache;
  mutable std::unique_ptr<CycleSpaceCache> cycle_space_cache_;
  MahoSidebarFavoritesModel favorites_model_;
  std::u16string GetActiveHostText(Browser* browser) const;

  // V2 FFI: single call replacing 3 separate v1 FFI hops.
  std::string FetchSidebarStateV2(
      ::MahoCore* core, const std::string& space_id_json) const;

  // Prebuilt favicon map from v2 FFI (base64 decoded PNG bytes keyed by tab ID).
  // Set during BuildViewStateModel, consumed by ParseTreeNode, cleared after.
  mutable base::flat_map<std::string, std::vector<uint8_t>> v2_favicon_map_;

  // Parsed tree from v2 FFI. When non-null, BuildTabListModel uses this
  // instead of calling maho_core_get_sidebar_tree (v1).
  mutable std::optional<base::Value> v2_tree_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_STATE_ADAPTER_H_
