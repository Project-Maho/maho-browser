// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"

#include <algorithm>

#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"

namespace maho::sidebar_prefs {

const char kSidebarVisible[] = "maho.sidebar.visible";
const char kSidebarLayoutEnabled[] = "maho.sidebar.layout_enabled";
const char kSidebarPanelExpanded[] = "maho.sidebar.panel_expanded";
const char kSidebarWidth[] = "maho.sidebar.width";
const char kFavoritesDragHintDismissed[] =
    "maho.sidebar.favorites_drag_hint_dismissed";
const char kLiveFolders[] = "maho.live_folders";

int ClampSidebarWidthForLayout(int width) {
  if (width <= sidebar_layout::kLegacyRailWidthResetThresholdDp) {
    width = sidebar_layout::kDefaultRailWidthDp;
  }
  return std::clamp(width, sidebar_layout::kRuntimeRailWidthMinDp,
                    sidebar_layout::kRuntimeRailWidthMaxDp);
}

int GetSidebarWidthForLayout(int pref_width) {
  return ClampSidebarWidthForLayout(pref_width);
}

bool IsSidebarLayoutEnabled(const PrefService* prefs) {
  return prefs && prefs->GetBoolean(kSidebarLayoutEnabled);
}

bool IsSidebarPanelExpanded(const PrefService* prefs) {
  return prefs && prefs->GetBoolean(kSidebarPanelExpanded);
}

void ToggleSidebarPanelExpanded(PrefService* prefs) {
  if (!prefs) {
    return;
  }
  prefs->SetBoolean(kSidebarPanelExpanded, !IsSidebarPanelExpanded(prefs));
}

bool ShouldShowSidebarInShell(const PrefService* prefs) {
  return IsSidebarLayoutEnabled(prefs);
}

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kSidebarVisible, true);
  registry->RegisterBooleanPref(kSidebarLayoutEnabled, true);
  registry->RegisterBooleanPref(kSidebarPanelExpanded, true);
  registry->RegisterIntegerPref(kSidebarWidth,
                                sidebar_layout::kDefaultRailWidthDp);
  registry->RegisterBooleanPref(kFavoritesDragHintDismissed, false);
  registry->RegisterListPref(kLiveFolders);
}

void MigrateFromLegacyVisiblePref(PrefService* prefs) {
  if (!prefs) {
    return;
  }
  if (!prefs->FindPreference(kSidebarLayoutEnabled)->IsDefaultValue()) {
    return;
  }
  const bool legacy_visible = prefs->GetBoolean(kSidebarVisible);
  prefs->SetBoolean(kSidebarLayoutEnabled, true);
  prefs->SetBoolean(kSidebarPanelExpanded, legacy_visible);
}

}  // namespace maho::sidebar_prefs
