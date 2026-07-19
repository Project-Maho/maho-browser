// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_webstore_rebrand_handler.h"

#include <string>
#include <string_view>

#include "base/functional/callback_helpers.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "url/gurl.h"

WEB_CONTENTS_USER_DATA_KEY_IMPL(MahoWebStoreRebrandHandler);

namespace {

// Distinct from MahoBoostInjectionHandler's isolated world (CONTENT_END + 4).
constexpr int kIsolatedWorldIdForWebStoreRebrand =
    content::ISOLATED_WORLD_ID_CONTENT_END + 5;

bool IsChromeWebStore(const GURL& url) {
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  const std::string_view host = url.host();
  if (host == "chromewebstore.google.com") {
    return true;
  }
  // Legacy store still redirects some entry points through this host.
  if (host == "chrome.google.com" &&
      url.path().rfind("/webstore", 0) == 0) {
    return true;
  }
  return false;
}

// Runs in an isolated world that shares the page DOM. It renames the install
// button to "Add to Maho" (locale-agnostic: any non-tab <button> whose label
// contains the "Chrome" token) and removes the server-rendered
// "Switch to Chrome" nag popup/banner. A MutationObserver re-applies the fix
// because the store is a single-page app that renders these nodes
// asynchronously and re-renders them on in-app navigation.
constexpr char kRebrandScript[] = R"JS(
(function() {
  if (window.__mahoWebstoreRebrand) { return; }
  window.__mahoWebstoreRebrand = true;

  var BRAND = 'Maho';
  var CHROME_TOKEN = /(Google\s+)?Chrome/g;

  function renameInstallButtons() {
    var buttons = document.querySelectorAll('button');
    for (var i = 0; i < buttons.length; i++) {
      var btn = buttons[i];
      if (btn.getAttribute('role') === 'tab') { continue; }
      if ((btn.textContent || '').indexOf('Chrome') === -1) { continue; }
      var walker = document.createTreeWalker(btn, NodeFilter.SHOW_TEXT, null);
      var node;
      while ((node = walker.nextNode())) {
        if (node.nodeValue && node.nodeValue.indexOf('Chrome') !== -1) {
          node.nodeValue = node.nodeValue.replace(CHROME_TOKEN, BRAND);
        }
      }
    }
  }

  function removeNags() {
    // "Switch to Chrome?" popup — anchored by the locale-independent id that
    // the popup container references via aria-labelledby.
    var popups = document.querySelectorAll('[aria-labelledby="promo-header"]');
    for (var i = 0; i < popups.length; i++) { popups[i].remove(); }
    var promoHeader = document.getElementById('promo-header');
    if (promoHeader) {
      var card = promoHeader.closest('header > *') || promoHeader.parentElement;
      if (card) { card.remove(); }
    }
    // "Switch to Chrome to install extensions" banner. It is a short bar that
    // contains an action link and lives inside <main>, but its enclosing
    // <section> also wraps unrelated page content, so match the smallest
    // element whose own text is the short nag ("Switch to Chrome" EN /
    // "\uc804\ud658" KO) and that has a link/button, then remove just that bar.
    var candidates = document.querySelectorAll(
        'main div, main section, [role="alert"], [role="region"]');
    for (var j = 0; j < candidates.length; j++) {
      var el = candidates[j];
      var t = (el.textContent || '').trim();
      if (t.length > 0 && t.length < 120 &&
          /(switch to chrome|\uc804\ud658)/i.test(t) &&
          el.querySelector('a, button')) {
        el.remove();
      }
    }
  }

  function apply() {
    try {
      renameInstallButtons();
      removeNags();
    } catch (e) {}
  }

  apply();

  var scheduled = false;
  var observer = new MutationObserver(function() {
    if (scheduled) { return; }
    scheduled = true;
    setTimeout(function() { scheduled = false; apply(); }, 0);
  });
  observer.observe(document.documentElement,
                   { childList: true, subtree: true, characterData: true });
})();
)JS";

}  // namespace

MahoWebStoreRebrandHandler::MahoWebStoreRebrandHandler(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<MahoWebStoreRebrandHandler>(*web_contents) {}

MahoWebStoreRebrandHandler::~MahoWebStoreRebrandHandler() = default;

void MahoWebStoreRebrandHandler::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->HasCommitted() ||
      navigation_handle->IsErrorPage() ||
      !navigation_handle->IsInPrimaryMainFrame()) {
    return;
  }
  if (!IsChromeWebStore(navigation_handle->GetURL())) {
    return;
  }
  InjectRebrandScript(navigation_handle->GetRenderFrameHost());
}

void MahoWebStoreRebrandHandler::InjectRebrandScript(
    content::RenderFrameHost* render_frame_host) {
  if (!render_frame_host || !render_frame_host->IsRenderFrameLive() ||
      !render_frame_host->IsActive()) {
    return;
  }
  render_frame_host->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(std::string(kRebrandScript)), base::DoNothing(),
      kIsolatedWorldIdForWebStoreRebrand);
}
