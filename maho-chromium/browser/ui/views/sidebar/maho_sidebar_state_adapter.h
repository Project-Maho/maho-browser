// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_STATE_ADAPTER_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_STATE_ADAPTER_H_

#include <string>

#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"

class Browser;

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
  MahoSidebarFavoritesModel favorites_model_;
  std::u16string GetActiveHostText(Browser* browser) const;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_STATE_ADAPTER_H_
