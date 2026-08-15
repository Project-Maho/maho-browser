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
const char kSpacesFullViewport[] = "maho.spaces.full_viewport";
const char kMahoMiniGlobalShortcutEnabled[] =
    "maho.maho_mini.global_shortcut_enabled";
const char kMahoMiniClickOverrideEnabled[] =
    "maho.maho_mini.click_override_enabled";
const char kOpenExternalLinksInMahoMini[] =
    "maho.maho_mini.open_external_links_in_maho_mini";
const char kOpenExternalLinksInMahoMiniMigrated[] =
    "maho.maho_mini.open_external_links_in_maho_mini_migrated";
const char kPeekEnabled[] = "maho.peek.enabled";
const char kPeekLinkRoutingEnabled[] = "maho.peek.link_routing_enabled";
const char kPeekPopupRoutingEnabled[] = "maho.peek.popup_routing_enabled";
const char kMahoMiniWindowWidth[] = "maho.maho_mini.window_width";
const char kMahoMiniWindowHeight[] = "maho.maho_mini.window_height";
const char kMahoMailEnabled[] = "maho.mail.enabled";

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

bool IsMahoMailEnabled(const PrefService* prefs) {
  return prefs && prefs->GetBoolean(kMahoMailEnabled);
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
  registry->RegisterBooleanPref(kSpacesFullViewport, true);
  registry->RegisterBooleanPref(kMahoMiniGlobalShortcutEnabled, true);
  registry->RegisterBooleanPref(kMahoMiniClickOverrideEnabled, true);
  registry->RegisterBooleanPref(kOpenExternalLinksInMahoMini, false);
  registry->RegisterBooleanPref(kPeekEnabled, true);
  registry->RegisterBooleanPref(kPeekLinkRoutingEnabled, true);
  registry->RegisterBooleanPref(kPeekPopupRoutingEnabled, true);
  registry->RegisterIntegerPref(kMahoMiniWindowWidth, 0);
  registry->RegisterIntegerPref(kMahoMiniWindowHeight, 0);
  registry->RegisterBooleanPref(kMahoMailEnabled, false);
}

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  // Process-global on purpose: see the header comment on this pref. The
  // maho-core value it guards is shared by every profile, so the record that
  // the one-time seed already ran must be shared too.
  registry->RegisterBooleanPref(kOpenExternalLinksInMahoMiniMigrated, false);
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
