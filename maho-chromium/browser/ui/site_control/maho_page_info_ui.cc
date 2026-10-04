// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/site_control/maho_page_info_ui.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/location_bar/location_bar.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/net/maho_ad_block_tab_helper.h"
#include "maho/browser/net/maho_shield_site_state.h"
#include "maho/browser/net/maho_shield_site_state_factory.h"
#include "maho/browser/ui/views/boost/maho_boost_window_controller.h"
#include "maho/browser/ui/views/shields/maho_shield_bubble_coordinator.h"
#include "ui/base/interaction/element_tracker.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/widget/widget.h"
#include "url/origin.h"

namespace {

views::View* GetLocationBarAnchorView(Browser* browser) {
  if (!browser) {
    return nullptr;
  }

  BrowserView* window = BrowserView::GetBrowserViewForBrowser(browser);
  if (!window) {
    return nullptr;
  }

  LocationBar* location_bar = window->GetLocationBar();
  if (!location_bar) {
    return nullptr;
  }

  ui::TrackedElement* tracked_element = location_bar->GetAnchorOrNull();
  if (tracked_element) {
    auto* tracked_views = tracked_element->AsA<views::TrackedElementViews>();
    if (tracked_views && tracked_views->view()) {
      return tracked_views->view();
    }
  }

  views::Widget* widget =
      views::Widget::GetWidgetForNativeWindow(window->GetNativeWindow());
  return widget ? widget->GetContentsView() : nullptr;
}

maho::MahoShieldSiteState* GetShieldSiteState(Browser* browser,
                                              content::WebContents* contents) {
  if (contents) {
    return maho::MahoShieldSiteStateFactory::GetForProfile(
        Profile::FromBrowserContext(contents->GetBrowserContext()));
  }
  return maho::MahoShieldSiteStateFactory::GetForProfile(
      browser ? browser->GetProfile() : nullptr);
}

}  // namespace

// static
void MahoPageInfoUI::ShowShieldBubble(views::View* anchor_view,
                                       Browser* browser,
                                       const std::string& origin) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (!anchor_view || !browser || origin.empty()) {
    return;
  }

  uint32_t blocked_count = 0;
  TabStripModel* tab_strip = browser->GetTabStripModel();
  content::WebContents* wc = tab_strip ? tab_strip->GetActiveWebContents()
                                       : nullptr;
  maho::MahoShieldSiteState* state = GetShieldSiteState(browser, wc);
  if (!state) {
    return;
  }

  if (wc) {
    auto* helper = MahoAdBlockTabHelper::FromWebContents(wc);
    if (helper) {
      blocked_count = helper->blocked_count();
    }
  }

  maho::MahoShieldBubbleCoordinator::GetForBrowser(browser).Show(
      anchor_view, origin, blocked_count,
      state->IsSiteExceptedForOrigin(origin), wc);
}

void MahoPageInfoUI::ShowShieldBubble(
    views::Widget* anchor_widget,
    const gfx::Rect& anchor_rect,
    Browser* browser,
    const std::string& origin,
    uint32_t blocked_count,
    bool is_excepted,
    content::WebContents* target_web_contents) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (!anchor_widget || anchor_rect.IsEmpty() || !browser || origin.empty() ||
      !target_web_contents) {
    return;
  }

  maho::MahoShieldBubbleCoordinator::GetForBrowser(browser).Show(
      anchor_widget, anchor_rect, origin, blocked_count, is_excepted,
      target_web_contents);
}

// static
void MahoPageInfoUI::ShowSiteSettings(Browser* browser, const GURL& url) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!browser || !url.is_valid() || url.is_empty()) {
    return;
  }
  chrome::ShowSiteSettings(browser, url);
}

// static
void MahoPageInfoUI::ShowCookiesSettings(Browser* browser) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!browser) {
    return;
  }
  chrome::ShowContentSettings(browser, ContentSettingsType::COOKIES);
}

// static
void MahoPageInfoUI::ShowBoostEditor(Browser* browser) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (!browser) {
    return;
  }

  TabStripModel* tab_strip = browser->GetTabStripModel();
  if (!tab_strip) {
    return;
  }

  content::WebContents* active_tab = tab_strip->GetActiveWebContents();
  if (!active_tab) {
    return;
  }

  const GURL& url = active_tab->GetLastCommittedURL();
  if (!url.is_valid() || url.is_empty()) {
    return;
  }

  maho::MahoBoostWindowController::GetForBrowser(browser, browser->GetProfile())
      .ShowForActiveDomain(active_tab);
}

// static
void MahoPageInfoUI::ShowShieldBubble(Browser* browser) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (!browser) {
    return;
  }

  views::View* anchor_view = GetLocationBarAnchorView(browser);
  content::WebContents* web_contents =
      browser->GetTabStripModel()->GetActiveWebContents();
  if (!anchor_view || !web_contents) {
    return;
  }

  if (ui::BaseWindow* window = browser->GetWindow()) {
    window->Activate();
  }

  ShowShieldBubble(
      anchor_view, browser,
      url::Origin::Create(web_contents->GetLastCommittedURL()).Serialize());
}
