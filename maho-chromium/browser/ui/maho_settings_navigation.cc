// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/maho_settings_navigation.h"

#include <string>

#include "base/check.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_navigator.h"  // nogncheck
#include "chrome/browser/ui/browser_navigator_params.h"  // nogncheck
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"  // nogncheck
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"  // nogncheck
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "maho/components/constants/webui_url_constants.h"
#include "net/base/url_util.h"
#include "ui/base/base_window.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

class Profile;

namespace maho {

namespace {

std::string PaneQueryFromUrl(const GURL& url) {
  std::string pane_value;
  net::GetValueForKeyInQuery(url, "pane", &pane_value);
  return pane_value;
}

GURL BuildSettingsUrl(const std::string& pane_key) {
  GURL url(maho::kMahoSettingsPublicURL);
  if (pane_key.empty()) {
    return url;
  }
  return net::AppendQueryParameter(url, "pane", pane_key);
}

bool IsMahoSettingsUrl(const GURL& url) {
  return (url.SchemeIs("chrome") && url.host() == maho::kMahoSettingsHost) ||
         (url.SchemeIs("maho") &&
          url.host() == GURL(maho::kMahoSettingsPublicURL).host());
}

GURL SettingsPaneUrl(content::WebContents* contents) {
  if (!contents) {
    return GURL();
  }
  content::NavigationEntry* entry =
      contents->GetController().GetLastCommittedEntry();
  return entry ? entry->GetVirtualURL() : GURL();
}

struct SettingsTabLocation {
  raw_ptr<BrowserWindowInterface> browser_window = nullptr;
  int index = TabStripModel::kNoTab;
};

SettingsTabLocation FindSettingsTabAcrossWindows(Profile* profile) {
  SettingsTabLocation result;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [profile, &result](BrowserWindowInterface* bwi) {
        if (bwi->GetProfile() != profile) {
          return true;
        }
        TabStripModel* tab_strip = bwi->GetTabStripModel();
        if (!tab_strip) {
          return true;
        }
        for (int i = 0; i < tab_strip->count(); ++i) {
          auto* contents = tab_strip->GetWebContentsAt(i);
          if (!contents) {
            continue;
          }
          content::NavigationEntry* entry =
              contents->GetController().GetLastCommittedEntry();
          if (!entry) {
            continue;
          }
          if (IsMahoSettingsUrl(entry->GetURL())) {
            result.browser_window = bwi;
            result.index = i;
            return false;
          }
        }
        return true;
      });
  return result;
}

}  // namespace

void OpenMahoSettingsPane(Browser* browser, const std::string& pane_key) {
  DCHECK(browser);

  const bool pane_targeted = !pane_key.empty();
  const GURL target_url = BuildSettingsUrl(pane_key);

  const SettingsTabLocation existing =
      FindSettingsTabAcrossWindows(browser->GetProfile());
  if (existing.browser_window) {
    existing.browser_window->GetWindow()->Activate();
    TabStripModel* existing_tab_strip =
        existing.browser_window->GetTabStripModel();
    existing_tab_strip->ActivateTabAt(existing.index);
    if (pane_targeted) {
      auto* contents = existing_tab_strip->GetActiveWebContents();
      if (contents && PaneQueryFromUrl(SettingsPaneUrl(contents)) != pane_key) {
        NavigateParams nav(existing.browser_window, target_url,
                           ui::PAGE_TRANSITION_GENERATED);
        nav.disposition = WindowOpenDisposition::CURRENT_TAB;
        Navigate(&nav);
      }
    }
    return;
  }

  NavigateParams params(browser, target_url, ui::PAGE_TRANSITION_GENERATED);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

void NavigateToNewForegroundTab(Browser* browser,
                                const GURL& url,
                                ui::PageTransition transition) {
  if (!browser) {
    return;
  }
  NavigateParams params(browser, url, transition);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

}  // namespace maho
