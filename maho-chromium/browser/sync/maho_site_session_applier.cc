// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/sync/maho_site_session_applier.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/storage_partition.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/sync/maho_site_session_cookie.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "net/cookies/cookie_options.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace maho {

MahoSiteSessionApplier::MahoSiteSessionApplier(Profile* profile)
    : profile_(profile) {}

MahoSiteSessionApplier::~MahoSiteSessionApplier() {
  Cancel();
}

void MahoSiteSessionApplier::Cancel() {
  weak_factory_.InvalidateWeakPtrs();
  cookie_manager_.reset();
  pending_apply_ = false;
}

void MahoSiteSessionApplier::Apply(const std::string& record_json,
                                   CompletionCallback callback) {
  if (pending_apply_) {
    SiteSessionApplyResult result;
    result.failures.push_back("apply_already_in_progress");
    std::move(callback).Run(std::move(result));
    return;
  }
  pending_apply_ = true;
  ApplyRecord(record_json, std::move(callback));
}

bool MahoSiteSessionApplier::IsOriginOptedIn(const std::string& origin) const {
  MahoCore* core = GetCore();
  auto opted = base::JSONReader::Read(
      core ? maho::core::GetSiteSessionOptedOriginsJson(core) : std::string(),
      base::JSON_PARSE_RFC);
  if (!opted || !opted->is_list()) return false;
  for (const auto& value : opted->GetList())
    if (value.is_string() && value.GetString() == origin) return true;
  return false;
}

void MahoSiteSessionApplier::ApplyRecord(const std::string& record_json,
                                        CompletionCallback callback) {
  last_record_json_ = record_json;
  SiteSessionApplyResult result;
  if (!profile_ || profile_->IsOffTheRecord()) {
    result.failures.push_back("private_profile");
    FinishApply(std::move(result), std::move(callback));
    return;
  }
  auto parsed = base::JSONReader::Read(record_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    result.failures.push_back("invalid_record");
    FinishApply(std::move(result), std::move(callback));
    return;
  }
  const auto& record = parsed->GetDict();
  const std::string* origin = record.FindString("origin");
  const base::ListValue* cookies = record.FindList("cookies");
  if (!origin || !cookies) {
    result.failures.push_back("invalid_record");
    FinishApply(std::move(result), std::move(callback));
    return;
  }
  result.origin = *origin;
  const GURL origin_url(*origin);
  if (!origin_url.is_valid() || !origin_url.SchemeIs("https") ||
      url::Origin::Create(origin_url).Serialize() != *origin ||
      !IsOriginOptedIn(*origin)) {
    result.failures.push_back("origin_not_opted_in");
    FinishApply(std::move(result), std::move(callback));
    return;
  }
  std::vector<std::string> encoded;
  for (const auto& item : *cookies) {
    if (!item.is_string()) {
      result.failures.push_back("cookie_not_string");
      continue;
    }
    encoded.push_back(item.GetString());
  }
  auto* partition = profile_->GetDefaultStoragePartition();
  if (!partition) {
    result.failures.push_back("storage_partition_unavailable");
    std::move(callback).Run(std::move(result));
    return;
  }
  partition->GetCookieManagerForBrowserProcess()->CloneInterface(
      cookie_manager_.BindNewPipeAndPassReceiver());
  result.origin = *origin;
  std::vector<std::string> cookie_strings;
  if (const auto* cookie_list = record.FindList("cookies")) {
    for (const auto& item : *cookie_list)
      if (item.is_string()) cookie_strings.push_back(item.GetString());
  }
  SetNextCookie(*origin, std::move(cookie_strings), 0, std::move(result),
                std::move(callback));
}

void MahoSiteSessionApplier::SetNextCookie(
    std::string origin,
    std::vector<std::string> cookies,
    size_t index,
    SiteSessionApplyResult result,
    CompletionCallback callback) {
  if (index >= cookies.size()) {
    auto parsed = base::JSONReader::Read(last_record_json_, base::JSON_PARSE_RFC);
    if (parsed && parsed->is_dict() &&
        parsed->GetDict().FindDict("localStorage") &&
        !parsed->GetDict().FindDict("localStorage")->empty())
      result.failures.push_back("local_storage_apply_not_implemented");
    FinishApply(std::move(result), std::move(callback));
    return;
  }
  auto decoded = DecodeSiteSessionCookie(cookies[index]);
  const GURL origin_url(origin);
  if (!decoded || !ShouldApplySiteSessionCookie(
                      *decoded, origin_url, true, base::Time::Now())) {
    result.failures.push_back("cookie_validation_failed:" +
                              base::NumberToString(index));
    SetNextCookie(std::move(origin), std::move(cookies), index + 1,
                  std::move(result), std::move(callback));
    return;
  }
  auto cookie = BuildSiteSessionCanonicalCookie(*decoded, origin_url,
                                                base::Time::Now());
  if (!cookie) {
    result.failures.push_back("cookie_rebuild_failed:" +
                              base::NumberToString(index));
    SetNextCookie(std::move(origin), std::move(cookies), index + 1,
                  std::move(result), std::move(callback));
    return;
  }
  net::CookieOptions options = net::CookieOptions::MakeAllInclusive();
  options.set_include_httponly();
  GURL::Replacements with_path;
  with_path.SetPathStr(decoded->path);
  cookie_manager_->SetCanonicalCookie(
      *cookie, origin_url.ReplaceComponents(with_path), options,
      base::BindOnce(
          [](base::WeakPtr<MahoSiteSessionApplier> self, std::string origin,
             std::vector<std::string> cookies, size_t index,
             SiteSessionApplyResult result, CompletionCallback callback,
             net::CookieAccessResult access_result) mutable {
            if (!self) return;
            if (!self->IsOriginOptedIn(origin)) {
              result.failures.push_back("origin_not_opted_in");
              self->FinishApply(std::move(result), std::move(callback));
              return;
            }
            if (access_result.status.IsInclude())
              ++result.applied;
            else
              result.failures.push_back("cookie_store_rejected:" +
                                        base::NumberToString(index));
            self->SetNextCookie(std::move(origin), std::move(cookies), index + 1,
                                std::move(result), std::move(callback));
          }, weak_factory_.GetWeakPtr(), std::move(origin), std::move(cookies),
          index, std::move(result), std::move(callback)));
}

void MahoSiteSessionApplier::FinishApply(SiteSessionApplyResult result,
                                         CompletionCallback callback) {
  pending_apply_ = false;
  std::move(callback).Run(std::move(result));
}

}  // namespace maho
