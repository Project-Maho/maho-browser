// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_ad_block_tab_helper.h"

#include "content/public/browser/navigation_handle.h"

WEB_CONTENTS_USER_DATA_KEY_IMPL(MahoAdBlockTabHelper);

MahoAdBlockTabHelper::MahoAdBlockTabHelper(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<MahoAdBlockTabHelper>(*web_contents) {}

MahoAdBlockTabHelper::~MahoAdBlockTabHelper() = default;

void MahoAdBlockTabHelper::IncrementBlockedCount() {
  ++blocked_count_;
}

void MahoAdBlockTabHelper::PrimaryPageChanged(content::Page& page) {
  blocked_count_ = 0;
}
