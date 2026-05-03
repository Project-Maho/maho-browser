// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_action_handler.h"

#include <string>

#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "maho/components/constants/webui_url_constants.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace maho {

void ExecuteCommandAction(Browser* browser, const std::string& action_id) {
  if (action_id == "new_tab") {
    chrome::NewTab(browser);
  } else if (action_id == "close_tab") {
    chrome::CloseTab(browser);
  } else if (action_id == "reload_tab") {
    chrome::Reload(browser, WindowOpenDisposition::CURRENT_TAB);
  } else if (action_id == "hard_reload") {
    chrome::ReloadBypassingCache(browser, WindowOpenDisposition::CURRENT_TAB);
  } else if (action_id == "duplicate_tab") {
    chrome::DuplicateTab(browser);
  } else if (action_id == "restore_tab") {
    chrome::RestoreTab(browser);
  } else if (action_id == "pin_tab") {
    chrome::PinTab(browser);
  } else if (action_id == "find_in_page") {
    chrome::Find(browser);
  } else if (action_id == "zoom_in") {
    chrome::Zoom(browser, content::PAGE_ZOOM_IN);
  } else if (action_id == "zoom_out") {
    chrome::Zoom(browser, content::PAGE_ZOOM_OUT);
  } else if (action_id == "reset_zoom") {
    chrome::Zoom(browser, content::PAGE_ZOOM_RESET);
  } else if (action_id == "toggle_dev_tools") {
    chrome::ToggleDevToolsWindow(browser, DevToolsToggleAction::Toggle(),
                                 DevToolsOpenedByAction::kUnknown);
  } else if (action_id == "print_page") {
    chrome::Print(browser);
  } else if (action_id == "view_source") {
    if (auto* web_contents =
            browser->tab_strip_model()->GetActiveWebContents()) {
      web_contents->GetPrimaryMainFrame()->ViewSource();
    }
  } else if (action_id == "settings") {
    GURL settings_url(maho::kMahoSettingsURL);
    NavigateParams params(browser, settings_url,
                          ui::PAGE_TRANSITION_GENERATED);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&params);
  } else if (action_id == "open_history") {
    chrome::ShowHistory(browser);
  } else if (action_id == "open_bookmarks") {
    chrome::ShowBookmarkManager(browser);
  } else if (action_id == "open_downloads") {
    chrome::ShowDownloads(browser);
  } else if (action_id == "new_incognito") {
    chrome::NewIncognitoWindow(browser->profile());
  } else if (action_id == "focus_url_bar") {
    chrome::FocusLocationBar(browser);
  } else {
    MahoCore* core = maho::GetCore();
    if (core) {
      std::string event_json =
          "{\"type\":\"CommandBarAction\",\"action\":\"" + action_id + "\"}";
      char* result = maho_core_handle_event(core, event_json.c_str());
      if (result)
        maho_string_free(result);
    }
  }
}

void OpenCommandSuggestion(Browser* browser,
                           const CommandSuggestion& suggestion) {
  if (!suggestion.action_id.empty()) {
    ExecuteCommandAction(browser, suggestion.action_id);
    return;
  }

  if (suggestion.tab_index >= 0) {
    TabStripModel* model = browser->tab_strip_model();
    if (suggestion.tab_index < model->count()) {
      model->ActivateTabAt(suggestion.tab_index);
      return;
    }
  }

  GURL gurl(suggestion.execution_payload);
  if (!gurl.SchemeIsHTTPOrHTTPS() && !gurl.SchemeIs("chrome"))
    return;

  if (gurl.spec() == maho::kMahoAIURL) {
    browser->GetFeatures().side_panel_ui()->Show(
        SidePanelEntryId::kMahoAiPanel);
    return;
  }

  if (gurl.spec() == maho::kMahoSettingsURL) {
    if (!chrome::ActivateMahoSettingsTabIfOpen(browser)) {
      content::OpenURLParams params(gurl, content::Referrer(),
                                    WindowOpenDisposition::NEW_WINDOW,
                                    ui::PAGE_TRANSITION_AUTO_BOOKMARK, false);
      browser->OpenURL(params, /*navigation_handle_callback=*/{});
    }
    return;
  }

  NavigateParams params(browser, gurl, ui::PAGE_TRANSITION_GENERATED);
  params.disposition = WindowOpenDisposition::CURRENT_TAB;
  Navigate(&params);
}

}  // namespace maho
