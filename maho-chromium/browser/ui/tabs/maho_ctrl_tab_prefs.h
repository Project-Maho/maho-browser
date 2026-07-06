// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_PREFS_H_
#define MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_PREFS_H_

class PrefRegistrySimple;

namespace maho::ctrl_tab_prefs {

// If false, Ctrl+Tab falls back to Chromium's positional
// SelectNextTab/SelectPreviousTab.
inline constexpr char kMruOrder[] = "maho.ctrl_tab.mru_order";

// One of "current_space" (default) or "global".  Controls whether the MRU
// switcher cycles only within the currently active Maho space or across all
// tabs in the window.
inline constexpr char kScope[] = "maho.ctrl_tab.scope";

// Maximum number of preview cards shown in the overlay.  Tabs beyond this
// count are represented by a "+N more" affordance card.
inline constexpr char kMaxVisible[] = "maho.ctrl_tab.max_visible";

inline constexpr char kScopeCurrentSpace[] = "current_space";
inline constexpr char kScopeGlobal[] = "global";

void RegisterProfilePrefs(PrefRegistrySimple* registry);

}  // namespace maho::ctrl_tab_prefs

#endif  // MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_PREFS_H_
