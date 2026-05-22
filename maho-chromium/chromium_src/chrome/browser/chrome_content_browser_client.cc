// Copyright 2026 Maho Browser. All rights reserved.
// chromium_src overlay reference for chrome_content_browser_client.cc
//
// Documents the upstream patches applied to chrome_content_browser_client.cc
// by build/scripts/apply_chromium_src_overrides.py.
//
// ── Patch 1: URL-loader throttles ────────────────────────────────────────────
//
// Includes added (file-scope, before anonymous namespace):
//   #include "maho/browser/maho_core_holder.h"
//   #include "maho/browser/net/maho_ad_block_tab_helper.h"
//   #include "maho/browser/net/maho_ad_block_throttle.h"
//   #include "maho/browser/net/maho_webstore_ua_throttle.h"
//
// In ChromeContentBrowserClient::CreateURLLoaderThrottles(),
// before `return result;`, insert:
//   if (maho::GetCore()) {
//     base::RepeatingClosure on_blocked;
//     content::WebContents* wc = wc_getter ? wc_getter.Run() : nullptr;
//     if (wc) {
//       MahoAdBlockTabHelper::CreateForWebContents(wc);
//       MahoAdBlockTabHelper* helper =
//           MahoAdBlockTabHelper::FromWebContents(wc);
//       if (helper) {
//         on_blocked = base::BindRepeating(
//             &MahoAdBlockTabHelper::IncrementBlockedCount,
//             base::Unretained(helper));
//       }
//     }
//     result.push_back(
//         std::make_unique<MahoAdBlockThrottle>(std::move(on_blocked)));
//   }
//   result.push_back(std::make_unique<MahoWebStoreUAThrottle>());
//
// MahoAdBlockThrottle intercepts every network request via
// WillStartRequest() and calls maho::core::CheckRequest(), which
// delegates to the Rust adblock::Engine (brave/adblock-rust).
// Based on the BlockResult:
//   - redirect: replaces request URL with data: URI ($redirect rules)
//   - rewritten_url: rewrites URL to strip tracking params ($removeparam)
//   - blocked: cancels with net::ERR_BLOCKED_BY_CLIENT
// Per-site exceptions are checked in the Rust layer via host matching.
//
// MahoAdBlockTabHelper tracks blocked-request counts per tab via an
// on_blocked callback passed to MahoAdBlockThrottle. The counter resets
// on navigation (PrimaryPageChanged). The shield bubble reads this count.
//
// MahoWebStoreUAThrottle strips the "Maho/1.0" User-Agent token on
// Chrome Web Store and CRX-download domains so Google does not block
// extension installs.
//
// The MahoAdBlockThrottle is guarded by a null check on maho::GetCore()
// so that the throttle is only created after MahoCore initialisation
// (PostEarlyInitialization, before any network requests).
//
// GN dep added to chrome/browser/BUILD.gn:
//   "//maho/browser/net"
//
// ── Patch 2: BrowserURLHandler NTP rewrite ───────────────────────────────────
//
// Includes added (file-scope, after chrome/browser/search/search.h):
//   #include "content/public/browser/browser_url_handler.h"
//
// A helper inserted into the file's existing anonymous namespace, after:
//   bool g_disable_advanced_protection_caching_for_tests = false;
//
//   bool MahoRewriteNewTabToBlank(GURL* url,
//                                 content::BrowserContext* /*context*/) {
//     if (url->SchemeIs("chrome") && url->host() == "newtab") {
//       *url = GURL("about:blank");
//       return true;
//     }
//     return false;
//   }
//
// In ChromeContentBrowserClient::BrowserURLHandlerCreated(), immediately
// before the existing HandleChromeAboutAndChromeSyncRewrite AddHandlerPair:
//   handler->AddHandlerPair(&MahoRewriteNewTabToBlank,
//                           BrowserURLHandler::null_handler());
//
// This rewrite fires after extra_parts_ BrowserURLHandlerCreated() calls and
// before the upstream NTP/about-rewriter, so chrome://newtab navigations that
// survive policy and extra_parts handling are redirected to about:blank.
// The handler only inspects GURL* and BrowserContext*; it never touches UI.
// The reverse handler is null_handler() (no reverse mapping needed).
