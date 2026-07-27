// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_ad_block_tab_helper.h"

#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"

WEB_CONTENTS_USER_DATA_KEY_IMPL(MahoAdBlockTabHelper);

MahoAdBlockTabHelper::MahoAdBlockTabHelper(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<MahoAdBlockTabHelper>(*web_contents) {}

MahoAdBlockTabHelper::~MahoAdBlockTabHelper() = default;

void MahoAdBlockTabHelper::RecordBlockedRequest(
    const MahoBlockedRequestInfo& info) {
  if (!current_primary_page_id_ || !info.page_id ||
      info.page_id != current_primary_page_id_ ||
      !info.is_outermost_main_frame ||
      info.destination != network::mojom::RequestDestination::kDocument) {
    return;
  }

  ++blocked_count_;
}

void MahoAdBlockTabHelper::PrimaryPageChanged(content::Page& page) {
  current_primary_page_id_ = page.GetMainDocument().GetGlobalId();
  blocked_count_ = 0;
}
