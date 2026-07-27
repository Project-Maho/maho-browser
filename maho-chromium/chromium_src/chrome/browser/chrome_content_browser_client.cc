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
//             [](base::WeakPtr<MahoAdBlockTabHelper> weak) {
//               if (weak) {
//                 weak->IncrementBlockedCount();
//               }
//             },
//             helper->GetWeakPtr());
//       }
//     }
//     result.push_back(
//         std::make_unique<MahoAdBlockThrottle>(std::move(on_blocked)));
//   }
//   result.push_back(std::make_unique<MahoWebStoreUAThrottle>());
//
// NOTE (audit H1 fix): WillStartRequest runs on the UI thread
// (DCHECK_CURRENTLY_ON(BrowserThread::UI)), so no PostTask hop is needed.
// GetWeakPtr() instead of base::Unretained() eliminates the UAF window if
// the tab closes between throttle construction and the first request.
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
// ── Patch 2: BrowserURLHandler NTP and public alias rewrites ─────────────────
//
// Includes added (file-scope, after maho/browser/ui/views/sidebar/maho_sidebar_prefs.h):
//   #include "content/public/browser/browser_url_handler.h"
//   #include "maho/browser/maho_url_scheme.h"
//
// Helpers inserted into the file's existing anonymous namespace, after:
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
//   bool MahoHandleUrlAlias(GURL* url,
//                           content::BrowserContext* /*context*/) {
//     return maho::MapMahoUrlAliasToActualUrl(*url, url);
//   }
//
//   bool MahoHandleUrlAliasReverse(GURL* url,
//                                  content::BrowserContext* /*context*/) {
//     GURL alias;
//     if (maho::ResolveActualUrlToMahoAlias(*url, &alias)) {
//       *url = alias;
//     }
//     return true;
//   }
//
// In ChromeContentBrowserClient::BrowserURLHandlerCreated(), immediately
// before the existing HandleChromeAboutAndChromeSyncRewrite AddHandlerPair:
//   handler->AddHandlerPair(&MahoRewriteNewTabToBlank,
//                           BrowserURLHandler::null_handler());
//   handler->AddHandlerPair(&MahoHandleUrlAlias,
//                           &MahoHandleUrlAliasReverse);
//
// This rewrite fires after extra_parts_ BrowserURLHandlerCreated() calls and
// before the upstream NTP/about-rewriter, so chrome://newtab navigations that
// survive policy and extra_parts handling are redirected to about:blank. The
// public alias pair follows it and precedes both Chrome about/sync and WebUI
// handlers. The forward callback returns true only for the strict allowlist of
// valid maho:// aliases and maps them to their existing chrome://maho-* WebUI
// origins. Direct legacy chrome://maho-* URLs do not match and remain visibly
// legacy. BrowserURLHandler invokes the reverse callback only when the original
// URL matched the forward callback; it always returns true. It maps an approved
// final actual URL to its alias, but deliberately leaves external or unlisted
// final URLs unchanged to clear a stale alias virtual URL.
