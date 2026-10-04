// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDE_PANEL_MAHO_AI_SIDE_PANEL_COORDINATOR_H_
#define MAHO_BROWSER_UI_VIEWS_SIDE_PANEL_MAHO_AI_SIDE_PANEL_COORDINATOR_H_

class BrowserWindowInterface;
class SidePanelRegistry;

namespace maho {

void RegisterMahoAiSidePanel(BrowserWindowInterface* browser,
                              SidePanelRegistry* global_registry);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDE_PANEL_MAHO_AI_SIDE_PANEL_COORDINATOR_H_
