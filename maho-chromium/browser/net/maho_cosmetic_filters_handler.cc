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
#include "content/public/browser/weak_document_ptr.h"
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

  const uint64_t generation = maho::GetCoreGeneration();
  maho::PostCoreTask<std::vector<std::u16string>>(
      FROM_HERE,
      base::BindOnce([](uint64_t generation, std::string url) {
        std::vector<std::u16string> scripts;
        if (generation != maho::GetCoreGeneration() || !maho::GetCore()) {
          return scripts;
        }
        auto parsed = base::JSONReader::Read(
            maho::core::GetCosmeticResources(maho::GetCore(), url.c_str()),
            base::JSON_PARSE_RFC | base::JSON_ALLOW_TRAILING_COMMAS);
        if (!parsed || !parsed->is_dict()) {
          return scripts;
        }
        const auto& dict = parsed->GetDict();
        if (const auto* hide_selectors = dict.FindList("hideSelectors")) {
          std::vector<std::string> selectors;
          for (const auto& value : *hide_selectors) {
            if (value.is_string()) {
              selectors.push_back(value.GetString());
            }
          }
          if (!selectors.empty()) {
            scripts.push_back(base::UTF8ToUTF16(BuildCssInjectionScript(
                base::JoinString(selectors, ", ") + " { display: none !important; }")));
          }
        }
        if (const auto* script = dict.FindString("injectedScript");
            script && !script->empty()) {
          scripts.push_back(base::UTF8ToUTF16(*script));
        }
        return scripts;
      }, generation, url),
      base::BindOnce([](content::WeakDocumentPtr document, uint64_t generation,
                        std::vector<std::u16string> scripts) {
        auto* frame = document.AsRenderFrameHostIfValid();
        if (!frame || !frame->IsRenderFrameLive() || !frame->IsActive() ||
            generation != maho::GetCoreGeneration()) {
          return;
        }
        for (const auto& script : scripts) {
          frame->ExecuteJavaScriptInIsolatedWorld(
              script, base::DoNothing(), kIsolatedWorldIdForCosmeticFilters);
        }
      }, render_frame_host->GetWeakDocumentPtr(), generation));
}
