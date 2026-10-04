// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_SITE_CONTROL_MAHO_PAGE_INFO_UI_H_
#define MAHO_BROWSER_UI_SITE_CONTROL_MAHO_PAGE_INFO_UI_H_

#include <cstdint>
#include <string>

#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"

class Browser;

namespace content {
class WebContents;
}

namespace views {
class View;
class Widget;
}

class MahoPageInfoUI {
 public:
  MahoPageInfoUI() = delete;

  static void ShowShieldBubble(Browser* browser);
  static void ShowShieldBubble(views::View* anchor_view,
                               Browser* browser,
                               const std::string& origin);
  static void ShowShieldBubble(views::Widget* anchor_widget,
                               const gfx::Rect& anchor_rect,
                               Browser* browser,
                               const std::string& origin,
                               uint32_t blocked_count,
                               bool is_excepted,
                               content::WebContents* target_web_contents);
  static void ShowBoostEditor(Browser* browser);
  static void ShowSiteSettings(Browser* browser, const GURL& url);
  static void ShowCookiesSettings(Browser* browser);
};

#endif  // MAHO_BROWSER_UI_SITE_CONTROL_MAHO_PAGE_INFO_UI_H_
