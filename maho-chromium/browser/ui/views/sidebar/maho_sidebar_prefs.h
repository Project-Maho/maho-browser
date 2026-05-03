// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_PREFS_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_PREFS_H_

class PrefRegistrySimple;
class PrefService;

namespace maho::sidebar_prefs {

extern const char kSidebarVisible[];
extern const char kSidebarWidth[];

int ClampSidebarWidthForLayout(int width);
int GetSidebarWidthForLayout(int pref_width);
// Truthful shell contract: this only answers whether the sidebar surface
// should be shown, not whether its child views are individually visible.
bool ShouldShowSidebarInShell(const PrefService* prefs);
void ToggleSidebarVisibilityInShell(PrefService* prefs);

void RegisterProfilePrefs(PrefRegistrySimple* registry);

}  // namespace maho::sidebar_prefs

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_PREFS_H_
