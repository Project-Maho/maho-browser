// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_MAHO_SETTINGS_NAVIGATION_H_
#define MAHO_BROWSER_UI_MAHO_SETTINGS_NAVIGATION_H_

#include <string>

#include "ui/base/page_transition_types.h"

class Browser;
class GURL;

namespace maho {

// Opens the maho settings page. |pane_key| targets a specific pane
// (e.g. "content-blocker"); empty means generic open.
//
// If a settings tab is already open: activates it. For a pane-targeted
// open, also navigates only when the pane query parameter of the visible URL
// differs from |pane_key| (GetVisibleURL reflects the SPA's
// history.replaceState pane updates). Otherwise opens a new foreground tab.
void OpenMahoSettingsPane(Browser* browser, const std::string& pane_key);

// Opens |url| in a new foreground tab in |browser| with the given
// page-transition. Authoritative new-tab helper used by the command palette
// and contract tests.
void NavigateToNewForegroundTab(Browser* browser,
                                const GURL& url,
                                ui::PageTransition transition);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_MAHO_SETTINGS_NAVIGATION_H_
