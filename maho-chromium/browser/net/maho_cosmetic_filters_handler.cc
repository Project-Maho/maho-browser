// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_cosmetic_filters_handler.h"

#include <string>

#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "url/gurl.h"

WEB_CONTENTS_USER_DATA_KEY_IMPL(MahoCosmeticFiltersHandler);

namespace {

constexpr int kIsolatedWorldIdForCosmeticFilters =
    content::ISOLATED_WORLD_ID_CONTENT_END + 3;

std::string BuildCssInjectionScript(const std::string& selectors_css) {
  std::string escaped;
  base::ReplaceChars(selectors_css, "\\", "\\\\", &escaped);
  base::ReplaceChars(escaped, "`", "\\`", &escaped);
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "${", "\\${");

  return base::StrCat({
      "(function() {"
      "  if (document.getElementById('maho-cosmetic-style')) return;"
      "  var s = document.createElement('style');"
      "  s.id = 'maho-cosmetic-style';"
      "  s.textContent = `",
      escaped,
      "`;"
      "  (document.head || document.documentElement).appendChild(s);"
      "})();"});
}

}  // namespace

MahoCosmeticFiltersHandler::MahoCosmeticFiltersHandler(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<MahoCosmeticFiltersHandler>(*web_contents) {}

MahoCosmeticFiltersHandler::~MahoCosmeticFiltersHandler() = default;

void MahoCosmeticFiltersHandler::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->HasCommitted() ||
      navigation_handle->IsErrorPage() ||
      !navigation_handle->GetURL().SchemeIsHTTPOrHTTPS()) {
    return;
  }

  if (navigation_handle->IsSameDocument()) {
    return;
  }

  InjectCosmeticFilters(navigation_handle->GetRenderFrameHost(),
                        navigation_handle->GetURL().spec());
}

void MahoCosmeticFiltersHandler::InjectCosmeticFilters(
    content::RenderFrameHost* render_frame_host,
    const std::string& url) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  MahoCore* core = maho::GetCore();
  if (!core || !render_frame_host) {
    return;
  }

  std::string json = maho::core::GetCosmeticResources(core, url.c_str());
  auto parsed = base::JSONReader::Read(json,
      base::JSON_PARSE_RFC | base::JSON_ALLOW_TRAILING_COMMAS);
  if (!parsed || !parsed->is_dict()) {
    return;
  }

  const base::DictValue& dict = parsed->GetDict();

  const base::ListValue* hide_selectors = dict.FindList("hideSelectors");
  const std::string* injected_script = dict.FindString("injectedScript");

  if (hide_selectors && !hide_selectors->empty()) {
    std::vector<std::string> selectors;
    selectors.reserve(hide_selectors->size());
    for (const auto& val : *hide_selectors) {
      if (val.is_string()) {
        selectors.push_back(val.GetString());
      }
    }

    if (!selectors.empty()) {
      std::string combined = base::JoinString(selectors, ", ");
      std::string css = combined + " { display: none !important; }";

      std::string script = BuildCssInjectionScript(css);
      render_frame_host->ExecuteJavaScriptInIsolatedWorld(
          base::UTF8ToUTF16(script),
          base::DoNothing(),
          kIsolatedWorldIdForCosmeticFilters);
    }
  }

  if (injected_script && !injected_script->empty()) {
    render_frame_host->ExecuteJavaScriptInIsolatedWorld(
        base::UTF8ToUTF16(*injected_script),
        base::DoNothing(),
        kIsolatedWorldIdForCosmeticFilters);
  }
}
