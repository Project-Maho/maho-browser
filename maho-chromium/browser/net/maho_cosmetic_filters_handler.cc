// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_cosmetic_filters_handler.h"

#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "content/public/common/isolated_world_ids.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/page.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/net/maho_cosmetic_runtime_string.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "url/gurl.h"

WEB_CONTENTS_USER_DATA_KEY_IMPL(MahoCosmeticFiltersHandler);

namespace {

std::string JsonArray(const base::ListValue* list) {
  if (!list) return "[]";
  base::ListValue values;
  for (const auto& item : *list) {
    if (item.is_string()) values.Append(item.GetString());
  }
  return base::WriteJson(base::Value(std::move(values))).value_or("[]");
}

std::string JsonBool(const base::DictValue& dict, const char* key) {
  return dict.FindBool(key).value_or(false) ? "true" : "false";
}

std::string BuildCosmeticInitScript(const base::DictValue& dict) {
  const auto hide = JsonArray(dict.FindList("hideSelectors"));
  const auto procedural = JsonArray(dict.FindList("proceduralActions"));
  const auto exceptions = JsonArray(dict.FindList("exceptions"));
  return base::StrCat({"window.__mahoCosmetic.init({hideSelectors:", hide,
      ",proceduralActions:", procedural, ",exceptions:", exceptions,
      ",generichide:", JsonBool(dict, "generichide"), "});"});
}

bool CosmeticGenerichide(const base::DictValue& dict) {
  return dict.FindBool("generichide").value_or(false);
}

std::vector<std::string> CosmeticExceptions(const base::DictValue& dict) {
  std::vector<std::string> exceptions;
  if (const auto* list = dict.FindList("exceptions")) {
    for (const auto& value : *list) {
      if (value.is_string()) exceptions.push_back(value.GetString());
    }
  }
  return exceptions;
}

struct CosmeticInjection {
  std::vector<std::u16string> scripts;
  std::vector<std::string> exceptions;
  bool generichide = false;
};

}  // namespace

MahoCosmeticFiltersHandler::MahoCosmeticFiltersHandler(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<MahoCosmeticFiltersHandler>(*web_contents) {}

MahoCosmeticFiltersHandler::~MahoCosmeticFiltersHandler() = default;

void MahoCosmeticFiltersHandler::RenderFrameDeleted(
    content::RenderFrameHost* render_frame_host) {
  StopPoller(render_frame_host->GetGlobalId());
}

void MahoCosmeticFiltersHandler::StopPoller(
    content::GlobalRenderFrameHostId frame_id) {
  const auto it = pollers_.find(frame_id);
  if (it == pollers_.end()) return;
  auto poller = std::move(it->second);
  pollers_.erase(it);
  poller->Stop();
}

void MahoCosmeticFiltersHandler::PrimaryPageChanged(content::Page& page) {
  std::vector<content::GlobalRenderFrameHostId> ids;
  ids.reserve(pollers_.size());
  for (const auto& [id, poller] : pollers_) ids.push_back(id);
  for (const auto& id : ids) StopPoller(id);
}

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
  maho::PostCoreTask<CosmeticInjection>(
      FROM_HERE,
      base::BindOnce([](uint64_t generation, std::string url) {
        CosmeticInjection result;
        if (generation != maho::GetCoreGeneration() || !maho::GetCore()) {
          return result;
        }
        auto parsed = base::JSONReader::Read(
            maho::core::GetCosmeticResources(maho::GetCore(), url.c_str()),
            base::JSON_PARSE_RFC | base::JSON_ALLOW_TRAILING_COMMAS);
        if (!parsed || !parsed->is_dict()) {
          return result;
        }
        const auto& dict = parsed->GetDict();
        result.scripts.push_back(base::UTF8ToUTF16(kMahoCosmeticRuntime));
        result.scripts.push_back(base::UTF8ToUTF16(kMahoCosmeticProcedural));
        if (const auto* script = dict.FindString("injectedScript");
            script && !script->empty()) {
          result.scripts.push_back(base::UTF8ToUTF16(*script));
        }
        result.scripts.push_back(base::UTF8ToUTF16(BuildCosmeticInitScript(dict)));
        result.exceptions = CosmeticExceptions(dict);
        result.generichide = CosmeticGenerichide(dict);
        return result;
      }, generation, url),
      base::BindOnce([weak_this = weak_factory_.GetWeakPtr()](
                         content::WeakDocumentPtr document, uint64_t generation,
                        CosmeticInjection injection) {
        if (!weak_this) return;
        auto* frame = document.AsRenderFrameHostIfValid();
        if (!frame || !frame->IsRenderFrameLive() || !frame->IsActive() ||
            generation != maho::GetCoreGeneration()) {
          return;
        }
        const content::GlobalRenderFrameHostId frame_id = frame->GetGlobalId();
        weak_this->StopPoller(frame_id);
        for (const auto& script : injection.scripts) {
          frame->ExecuteJavaScriptInIsolatedWorld(
              script, base::DoNothing(),
              maho::kIsolatedWorldIdForCosmeticFilters);
        }
        if (!injection.generichide) {
          auto poller = std::make_unique<maho::CosmeticClassIdPoller>(
              maho::CosmeticClassIdPoller::CreateFrameDelegate(frame),
              std::move(injection.exceptions), generation);
          poller->Start();
          weak_this->pollers_.emplace(frame_id, std::move(poller));
        }
      }, render_frame_host->GetWeakDocumentPtr(),
         generation));
}
