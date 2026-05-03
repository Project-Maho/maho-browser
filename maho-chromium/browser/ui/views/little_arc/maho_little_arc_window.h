// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_LITTLE_ARC_MAHO_LITTLE_ARC_WINDOW_H_
#define MAHO_BROWSER_UI_VIEWS_LITTLE_ARC_MAHO_LITTLE_ARC_WINDOW_H_

class Browser;
class GURL;

// MahoLittleArcWindow creates a small floating popup browser window
// (400x500), similar to Arc's "Little Arc" feature. The popup uses
// Browser::TYPE_POPUP with always-on-top z-order for quick URL viewing
// without leaving the current tab.
class MahoLittleArcWindow {
 public:
  // Opens a new LittleArc floating popup window. If |url| is empty,
  // opens about:blank.
  static void Open(Browser* parent_browser, const GURL& url);

  // Promotes the current page in |popup_browser| to a new tab in its
  // opener (parent) browser, then closes the popup.
  static void PromoteToTab(Browser* popup_browser);

 private:
  MahoLittleArcWindow() = delete;
};

#endif  // MAHO_BROWSER_UI_VIEWS_LITTLE_ARC_MAHO_LITTLE_ARC_WINDOW_H_
