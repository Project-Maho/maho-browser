// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_SITE_CONTROL_MAHO_PAGE_INFO_UI_H_
#define MAHO_BROWSER_UI_SITE_CONTROL_MAHO_PAGE_INFO_UI_H_

#include <string>

#include "url/gurl.h"

class Browser;

namespace views {
class View;
}

class MahoPageInfoUI {
 public:
  MahoPageInfoUI() = delete;

  static void ShowBoostBubble(Browser* browser);
  static void ShowShieldBubble(Browser* browser);
  static void ShowShieldBubble(views::View* anchor_view,
                               Browser* browser,
                               const std::string& origin);
  static void ShowSiteSettings(Browser* browser, const GURL& url);
  static void ShowCookiesSettings(Browser* browser);
};

#endif  // MAHO_BROWSER_UI_SITE_CONTROL_MAHO_PAGE_INFO_UI_H_
