// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/maho_settings_navigation.h"

#include <string>

#include "base/check.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_navigator.h"  // nogncheck
#include "chrome/browser/ui/browser_navigator_params.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"  // nogncheck
#include "content/public/browser/web_contents.h"
#include "maho/components/constants/webui_url_constants.h"
#include "net/base/url_util.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace maho {

namespace {

std::string PaneQueryFromUrl(const GURL& url) {
  std::string pane_value;
  net::GetValueForKeyInQuery(url, "pane", &pane_value);
  return pane_value;
}

GURL BuildSettingsUrl(const std::string& pane_key) {
  GURL url(maho::kMahoSettingsURL);
  if (pane_key.empty()) {
    return url;
  }
  return net::AppendQueryParameter(url, "pane", pane_key);
}

int FindSettingsTabIndex(Browser* browser) {
  TabStripModel* tab_strip = browser->tab_strip_model();
  for (int i = 0; i < tab_strip->count(); ++i) {
    auto* contents = tab_strip->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    const GURL& visible = contents->GetVisibleURL();
    if (visible.SchemeIs("chrome") &&
        visible.host() == maho::kMahoSettingsHost) {
      return i;
    }
  }
  return TabStripModel::kNoTab;
}

}  // namespace

void OpenMahoSettingsPane(Browser* browser, const std::string& pane_key) {
  DCHECK(browser);

  const bool pane_targeted = !pane_key.empty();
  const GURL target_url = BuildSettingsUrl(pane_key);

  const int existing_index = FindSettingsTabIndex(browser);
  if (existing_index != TabStripModel::kNoTab) {
    browser->tab_strip_model()->ActivateTabAt(existing_index);
    if (pane_targeted) {
      auto* contents = browser->tab_strip_model()->GetActiveWebContents();
      if (contents &&
          PaneQueryFromUrl(contents->GetVisibleURL()) != pane_key) {
        NavigateParams nav(browser, target_url, ui::PAGE_TRANSITION_GENERATED);
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
