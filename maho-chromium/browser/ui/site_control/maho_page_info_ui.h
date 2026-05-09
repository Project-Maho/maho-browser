// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_SITE_CONTROL_MAHO_PAGE_INFO_UI_H_
#define MAHO_BROWSER_UI_SITE_CONTROL_MAHO_PAGE_INFO_UI_H_

#include <string>

class Browser;

namespace views {
class View;
}

class MahoPageInfoUI {
 public:
  MahoPageInfoUI() = delete;

  static void CustomizePageInfo();
  static void ShowShieldBubble(views::View* anchor_view,
                               Browser* browser,
                               const std::string& origin);
};

#endif  // MAHO_BROWSER_UI_SITE_CONTROL_MAHO_PAGE_INFO_UI_H_
