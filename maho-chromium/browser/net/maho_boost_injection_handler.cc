// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_boost_injection_handler.h"

#include <string>

#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "content/public/common/isolated_world_ids.h"
#include "url/gurl.h"

WEB_CONTENTS_USER_DATA_KEY_IMPL(MahoBoostInjectionHandler);

namespace {

constexpr int kIsolatedWorldIdForBoost = content::ISOLATED_WORLD_ID_CONTENT_END + 4;

std::string BuildBoostCssScript(const std::string& css) {
  if (css.empty()) {
    return "(function() {"
           "  var existing = document.getElementById('maho-boost-style');"
           "  if (existing) { existing.remove(); }"
           "})();";
  }

  std::string escaped;
  base::ReplaceChars(css, "\\", "\\\\", &escaped);
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "${", "\\${");
  base::ReplaceChars(escaped, "`", "\\`", &escaped);

  return base::StrCat({
      "(function() {"
      "  var existing = document.getElementById('maho-boost-style');"
      "  if (existing) {"
      "    existing.textContent = `",
      escaped,
      "`;"
      "    return;"
      "  }"
      "  var s = document.createElement('style');"
      "  s.id = 'maho-boost-style';"
      "  s.textContent = `",
      escaped,
      "`;"
      "  (document.head || document.documentElement).appendChild(s);"
      "})();"});
}

}  // namespace

MahoBoostInjectionHandler::MahoBoostInjectionHandler(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<MahoBoostInjectionHandler>(*web_contents) {}

MahoBoostInjectionHandler::~MahoBoostInjectionHandler() = default;

void MahoBoostInjectionHandler::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->HasCommitted() ||
      navigation_handle->IsErrorPage() ||
      !navigation_handle->GetURL().SchemeIsHTTPOrHTTPS()) {
    return;
  }

  if (!navigation_handle->IsInPrimaryMainFrame()) {
    return;
  }

  if (navigation_handle->IsSameDocument()) {
    // SPA/pushState/hash navigations: reinject CSS only (skip JS re-run).
    InjectBoostCss(navigation_handle->GetRenderFrameHost(),
                   navigation_handle->GetURL().spec());
    return;
  }

  InjectBoostCss(navigation_handle->GetRenderFrameHost(),
                 navigation_handle->GetURL().spec());
  InjectBoostJs(navigation_handle->GetRenderFrameHost(),
                navigation_handle->GetURL().spec());
}

void MahoBoostInjectionHandler::InjectBoostCssIntoTab(
    content::RenderFrameHost* render_frame_host,
    const std::string& url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  InjectBoostCss(render_frame_host, url);
}

void MahoBoostInjectionHandler::InjectBoostJsIntoTab(
    content::RenderFrameHost* render_frame_host,
    const std::string& url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  InjectBoostJs(render_frame_host, url);
}

void MahoBoostInjectionHandler::InjectBoostCss(
    content::RenderFrameHost* render_frame_host,
    const std::string& url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !render_frame_host || !render_frame_host->IsRenderFrameLive() ||
      !render_frame_host->IsActive()) {
    return;
  }

  std::string css = maho::core::GetBoostInjectionCss(core, url.c_str());
  std::string script = BuildBoostCssScript(css);
  render_frame_host->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(script),
      base::DoNothing(),
      kIsolatedWorldIdForBoost);
}

void MahoBoostInjectionHandler::InjectBoostJs(
    content::RenderFrameHost* render_frame_host,
    const std::string& url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core || !render_frame_host || !render_frame_host->IsRenderFrameLive() ||
      !render_frame_host->IsActive()) {
    return;
  }

  std::string js = maho::core::GetBoostInjectionJs(core, url.c_str());
  if (js.empty()) {
    return;
  }

  std::string script = base::StrCat({"(function() {", js, "})();"});
  render_frame_host->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(script),
      base::DoNothing(),
      kIsolatedWorldIdForBoost);
}
