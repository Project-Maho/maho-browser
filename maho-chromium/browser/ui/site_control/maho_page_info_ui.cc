// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/site_control/maho_page_info_ui.h"

#include "base/json/json_reader.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/location_bar/location_bar.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/views/boost/maho_boost_bubble_coordinator.h"
#include "maho/browser/net/maho_ad_block_tab_helper.h"
#include "maho/browser/ui/views/shields/maho_shield_bubble_coordinator.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "ui/base/interaction/element_tracker.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/widget/widget.h"
#include "url/origin.h"

namespace {

views::View* GetLocationBarAnchorView(Browser* browser) {
  if (!browser) {
    return nullptr;
  }

  BrowserWindow* window = browser->window();
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

  views::Widget* widget = views::Widget::GetWidgetForNativeWindow(
      window->GetNativeWindow());
  return widget ? widget->GetContentsView() : nullptr;
}

}  // namespace

// static
void MahoPageInfoUI::ShowBoostBubble(Browser* browser) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (!browser) {
    return;
  }

  views::View* anchor_view = GetLocationBarAnchorView(browser);
  content::WebContents* web_contents =
      browser->tab_strip_model()->GetActiveWebContents();
  if (!anchor_view || !web_contents) {
    return;
  }

  if (BrowserWindow* window = browser->window()) {
    window->Activate();
  }

  maho::MahoBoostBubbleCoordinator::GetForBrowser(browser).Show(
      anchor_view, browser, web_contents);
}

// static
void MahoPageInfoUI::ShowShieldBubble(views::View* anchor_view,
                                      Browser* browser,
                                      const std::string& origin) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  MahoCore* core = maho::GetCore();
  if (!core || !anchor_view || !browser) {
    return;
  }

  uint32_t blocked_count = 0;
  content::WebContents* wc =
      browser->tab_strip_model()->GetActiveWebContents();
  if (wc) {
    auto* helper = MahoAdBlockTabHelper::FromWebContents(wc);
    if (helper) {
      blocked_count = helper->blocked_count();
    }
  }

  bool is_excepted = false;
  std::string exceptions_json = maho::core::GetSiteExceptions(core);
  auto parsed = base::JSONReader::Read(exceptions_json, base::JSON_PARSE_RFC);
  if (parsed && parsed->is_list()) {
    for (const auto& val : parsed->GetList()) {
      if (val.is_string() && val.GetString() == origin) {
        is_excepted = true;
        break;
      }
    }
  }

  maho::MahoShieldBubbleCoordinator::GetForBrowser(browser).Show(
      anchor_view, origin, blocked_count, is_excepted, wc);
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
void MahoPageInfoUI::ShowShieldBubble(Browser* browser) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (!browser) {
    return;
  }

  views::View* anchor_view = GetLocationBarAnchorView(browser);
  content::WebContents* web_contents =
      browser->tab_strip_model()->GetActiveWebContents();
  if (!anchor_view || !web_contents) {
    return;
  }

  if (BrowserWindow* window = browser->window()) {
    window->Activate();
  }

  ShowShieldBubble(
      anchor_view, browser,
      url::Origin::Create(web_contents->GetLastCommittedURL()).Serialize());
}
