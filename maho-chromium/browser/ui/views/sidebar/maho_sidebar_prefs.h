// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_PREFS_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_PREFS_H_

class PrefRegistrySimple;
class PrefService;

namespace maho::sidebar_prefs {

// Legacy pref retained for migration only.
extern const char kSidebarVisible[];

// 2-state model prefs:
// Whether the Arc sidebar layout is enabled (controls tab strip suppression).
extern const char kSidebarLayoutEnabled[];
// Whether the sidebar panel is expanded (full width) vs collapsed (thin rail).
extern const char kSidebarPanelExpanded[];
// Persisted expanded width.
extern const char kSidebarWidth[];
// Whether the user has dismissed the "Drag to add Favorites" hint card shown
// in the empty favorites slot. Persistent across sessions per profile.
extern const char kFavoritesDragHintDismissed[];
extern const char kLiveFolders[];
extern const char kSpacesFullViewport[];
extern const char kMahoMiniGlobalShortcutEnabled[];
extern const char kMahoMiniClickOverrideEnabled[];
extern const char kOpenExternalLinksInMahoMini[];
extern const char kPeekEnabled[];
extern const char kPeekLinkRoutingEnabled[];
extern const char kPeekPopupRoutingEnabled[];

// One-time migration marker for kOpenExternalLinksInMahoMini. maho-core is the
// canonical store for that setting (ATCManager::decide_link_destination reads
// only core), so the legacy pref above is seeded into core exactly once and
// this marker records that it happened. The core FFI getter returns a plain
// bool and cannot distinguish "unset" from "false", so without this marker a
// later seed would clobber the user's choice on every startup.
//
// LOCAL STATE, NOT PROFILE PREFS. The value it guards lives in maho-core, which
// is a single process-global store shared by every profile. A profile-scoped
// marker would let profile B re-seed from its own legacy pref after profile A
// already migrated, overwriting the process-global core value that A (or the
// user) had set. Registered by RegisterLocalStatePrefs below.
extern const char kOpenExternalLinksInMahoMiniMigrated[];

extern const char kMahoMiniWindowWidth[];
extern const char kMahoMiniWindowHeight[];

// Maho Mail opt-in Beta gate. New profiles default to false and users enable it
// in Settings. It gates every Mail entry point together (sidebar button,
// chrome://maho-mail, the helper subprocess, and the settings panes); flipping
// only one of them leaves the surfaces inconsistent.
extern const char kMahoMailEnabled[];

int ClampSidebarWidthForLayout(int width);
int GetSidebarWidthForLayout(int pref_width);

// Returns true when Arc layout should be active (layout-enabled).
bool IsSidebarLayoutEnabled(const PrefService* prefs);
// Returns true when the panel is in expanded state.
bool IsSidebarPanelExpanded(const PrefService* prefs);
// Toggle panel expanded/collapsed state.
void ToggleSidebarPanelExpanded(PrefService* prefs);

// Returns true when Maho Mail is enabled for this profile. New profiles default
// to false until users enable the opt-in Beta in Settings.
bool IsMahoMailEnabled(const PrefService* prefs);

// Legacy compat: equivalent to IsSidebarLayoutEnabled for callers that
// previously used ShouldShowSidebarInShell.
bool ShouldShowSidebarInShell(const PrefService* prefs);

void RegisterProfilePrefs(PrefRegistrySimple* registry);

// Registers sidebar prefs that are process-global rather than per-profile.
// Currently only kOpenExternalLinksInMahoMiniMigrated, which must be shared by
// every profile because the maho-core value it guards is process-global.
void RegisterLocalStatePrefs(PrefRegistrySimple* registry);
// Migrate legacy kSidebarVisible to 2-state prefs. Call once at profile load.
void MigrateFromLegacyVisiblePref(PrefService* prefs);

}  // namespace maho::sidebar_prefs

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_PREFS_H_
