// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/little_arc/maho_little_arc_window.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "ui/base/ui_base_types.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

constexpr int kLittleArcWidth = 400;
constexpr int kLittleArcHeight = 500;

}  // namespace

// static
void MahoLittleArcWindow::Open(Browser* parent_browser, const GURL& url) {
  if (!parent_browser) {
    return;
  }

  gfx::Rect parent_bounds = parent_browser->window()->GetBounds();
  int x = parent_bounds.x() +
           (parent_bounds.width() - kLittleArcWidth) / 2;
  int y = parent_bounds.y() +
           (parent_bounds.height() - kLittleArcHeight) / 2;

  Browser::CreateParams params(Browser::TYPE_POPUP,
                               parent_browser->profile(),
                               /*user_gesture=*/true);
  params.initial_bounds = gfx::Rect(x, y, kLittleArcWidth, kLittleArcHeight);
  params.omit_from_session_restore = true;

  Browser* popup = Browser::Create(params);
  GURL target = url.is_empty() ? GURL("about:blank") : url;
  chrome::AddTabAt(popup, target, /*index=*/-1, /*foreground=*/true);

  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(popup);
  if (browser_view) {
    views::Widget* widget = browser_view->GetWidget();
    if (widget) {
      widget->SetZOrderLevel(ui::ZOrderLevel::kFloatingWindow);
    }
  }

  popup->window()->Show();
}

// static
void MahoLittleArcWindow::PromoteToTab(Browser* popup_browser) {
  if (!popup_browser) {
    return;
  }

  TabStripModel* popup_tab_strip = popup_browser->tab_strip_model();
  if (!popup_tab_strip || popup_tab_strip->empty()) {
    return;
  }

  // Find a tabbed (TYPE_NORMAL) browser with the same profile, skipping
  // popups. This ensures the promoted tab lands in a real browser window.
  Browser* target_browser =
      chrome::FindTabbedBrowser(popup_browser->profile(),
                                /*match_original_profiles=*/false);
  if (!target_browser || target_browser == popup_browser) {
    return;
  }

  int active_index = popup_tab_strip->active_index();
  std::unique_ptr<content::WebContents> contents =
      popup_tab_strip->DetachWebContentsAtForInsertion(active_index);
  if (contents) {
    target_browser->tab_strip_model()->InsertWebContentsAt(
        /*index=*/-1, std::move(contents),
        AddTabTypes::ADD_ACTIVE | AddTabTypes::ADD_INHERIT_OPENER);
    target_browser->window()->Show();
    target_browser->window()->Activate();
  }

  popup_browser->window()->Close();
}
