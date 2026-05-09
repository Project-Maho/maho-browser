// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/site_control/maho_page_info_ui.h"

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/net/maho_ad_block_tab_helper.h"
#include "maho/browser/ui/views/shields/maho_shield_bubble_view.h"
#include "maho/third_party/maho/maho_bridge.h"

// static
void MahoPageInfoUI::CustomizePageInfo() {
  LOG(INFO) << "MahoPageInfoUI::CustomizePageInfo";
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

  maho::MahoShieldBubbleView::Show(anchor_view, browser, origin,
                                   blocked_count, is_excepted);
}
