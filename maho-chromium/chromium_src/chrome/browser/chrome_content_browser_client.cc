// Copyright 2026 Maho Browser. All rights reserved.
// chromium_src overlay reference for chrome_content_browser_client.cc
//
// Documents the upstream patches applied to chrome_content_browser_client.cc:
//
// 1. #include "maho/browser/maho_core_holder.h"
//    #include "maho/browser/net/maho_ad_block_tab_helper.h"
//    #include "maho/browser/net/maho_ad_block_throttle.h"
//    #include "maho/browser/net/maho_webstore_ua_throttle.h"
//
// 2. In ChromeContentBrowserClient::CreateURLLoaderThrottles(),
//    before `return result;`, insert:
//      if (maho::GetCore()) {
//        base::RepeatingClosure on_blocked;
//        content::WebContents* wc = wc_getter ? wc_getter.Run() : nullptr;
//        if (wc) {
//          MahoAdBlockTabHelper::CreateForWebContents(wc);
//          MahoAdBlockTabHelper* helper =
//              MahoAdBlockTabHelper::FromWebContents(wc);
//          if (helper) {
//            on_blocked = base::BindRepeating(
//                &MahoAdBlockTabHelper::IncrementBlockedCount,
//                base::Unretained(helper));
//          }
//        }
//        result.push_back(
//            std::make_unique<MahoAdBlockThrottle>(std::move(on_blocked)));
//      }
//      result.push_back(std::make_unique<MahoWebStoreUAThrottle>());
//
//    MahoAdBlockThrottle intercepts every network request via
//    WillStartRequest() and calls maho::core::CheckRequest(), which
//    delegates to the Rust adblock::Engine (brave/adblock-rust).
//    Based on the BlockResult:
//      - redirect: replaces request URL with data: URI ($redirect rules)
//      - rewritten_url: rewrites URL to strip tracking params ($removeparam)
//      - blocked: cancels with net::ERR_BLOCKED_BY_CLIENT
//    Per-site exceptions are checked in the Rust layer via host matching.
//
//    MahoAdBlockTabHelper tracks blocked-request counts per tab via an
//    on_blocked callback passed to MahoAdBlockThrottle. The counter resets
//    on navigation (PrimaryPageChanged). The shield bubble reads this count.
//
//    MahoWebStoreUAThrottle strips the "Maho/1.0" User-Agent token on
//    Chrome Web Store and CRX-download domains so Google does not block
//    extension installs.
//
//    The MahoAdBlockThrottle is guarded by a null check on maho::GetCore()
//    so that the throttle is only created after MahoCore initialisation
//    (PostEarlyInitialization, before any network requests).
//
// GN dep added to chrome/browser/BUILD.gn:
//      "//maho/browser/net",
