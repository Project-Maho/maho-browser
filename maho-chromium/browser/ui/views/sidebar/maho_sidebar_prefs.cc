// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"

#include <algorithm>

#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"

namespace maho::sidebar_prefs {

const char kSidebarVisible[] = "maho.sidebar.visible";
const char kSidebarWidth[] = "maho.sidebar.width";

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

bool ShouldShowSidebarInShell(const PrefService* prefs) {
  return prefs && prefs->GetBoolean(kSidebarVisible);
}

void ToggleSidebarVisibilityInShell(PrefService* prefs) {
  if (!prefs) {
    return;
  }

  prefs->SetBoolean(kSidebarVisible, !ShouldShowSidebarInShell(prefs));
}

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kSidebarVisible, true);
  registry->RegisterIntegerPref(kSidebarWidth,
                                sidebar_layout::kDefaultRailWidthDp);
}

}  // namespace maho::sidebar_prefs
