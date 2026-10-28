// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/sync/maho_site_session_collector.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/services/storage/public/mojom/local_storage_control.mojom.h"
#include "content/public/browser/storage_partition.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/sync/maho_site_session_cookie.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "third_party/blink/public/mojom/dom_storage/storage_area.mojom.h"
#include "url/origin.h"

namespace maho {

MahoSiteSessionCollector::MahoSiteSessionCollector(Profile* profile)
    : profile_(profile) {}

MahoSiteSessionCollector::~MahoSiteSessionCollector() {
  Cancel();
}

void MahoSiteSessionCollector::Cancel() {
  weak_factory_.InvalidateWeakPtrs();
  cookie_manager_.reset();
  storage_area_.reset();
}

void MahoSiteSessionCollector::Collect(CompletionCallback callback) {
  Cancel();
  if (!profile_ || profile_->IsOffTheRecord()) {
    std::move(callback).Run({});
    return;
  }
  MahoCore* core = GetCore();
  if (!core) {
    std::move(callback).Run({});
    return;
  }
  auto parsed = base::JSONReader::Read(
      maho::core::GetSiteSessionExportOriginsJson(core), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    std::move(callback).Run({});
    return;
  }
  std::set<std::string> origins;
  for (const auto& item : parsed->GetList()) {
    const std::string* origin = item.GetIfString();
    if (!origin) continue;
    const GURL url(*origin);
    if (url.is_valid() && url.SchemeIs("https") &&
        url::Origin::Create(url).Serialize() == *origin)
      origins.insert(*origin);
  }
  if (origins.empty()) {
    std::move(callback).Run({});
    return;
  }
  content::StoragePartition* partition = profile_->GetDefaultStoragePartition();
  if (!partition) {
    std::move(callback).Run({});
    return;
  }
  partition->GetCookieManagerForBrowserProcess()->CloneInterface(
      cookie_manager_.BindNewPipeAndPassReceiver());
  cookie_manager_->GetAllCookies(base::BindOnce(
      &MahoSiteSessionCollector::OnCookies, weak_factory_.GetWeakPtr(),
      std::move(origins), std::move(callback)));
}

void MahoSiteSessionCollector::OnCookies(
    std::set<std::string> opted_origins,
    CompletionCallback callback,
    const std::vector<net::CanonicalCookie>& cookies) {
  if (!profile_ || profile_->IsOffTheRecord()) {
    std::move(callback).Run({});
    return;
  }
  std::vector<std::string> records;
  for (const auto& origin : opted_origins) {
    const GURL origin_url(origin);
    base::ListValue encoded_cookies;
    for (const auto& cookie : cookies) {
      if (!ShouldCollectSiteSessionCookie(cookie, origin_url, base::Time::Now()))
        continue;
      const char* same_site = cookie.SameSite() == net::CookieSameSite::LAX_MODE
                           ? "lax"
                           : cookie.SameSite() == net::CookieSameSite::STRICT_MODE
                                 ? "strict"
                                 : cookie.SameSite() == net::CookieSameSite::NO_RESTRICTION
                                       ? "none"
                                       : "unspecified";
      SiteSessionCookie fields{
          cookie.Name(), cookie.Value(), cookie.Path(), same_site,
          cookie.ExpiryDate().is_null()
              ? std::nullopt
              : std::optional<int64_t>(cookie.ExpiryDate().InMillisecondsSinceUnixEpoch()),
          cookie.IsSecure(), cookie.IsHttpOnly()};
      auto json = EncodeSiteSessionCookie(fields);
      if (json) encoded_cookies.Append(*json);
    }
    base::DictValue record;
    record.Set("origin", origin);
    record.Set("cookies", std::move(encoded_cookies));
    record.Set("localStorage", base::DictValue());
    std::string json;
    if (base::JSONWriter::Write(base::Value(std::move(record)), &json))
      records.push_back(std::move(json));
  }
  if (opted_origins.empty()) {
    std::move(callback).Run(std::move(records));
    return;
  }
  CollectStorageOrigin(0, {opted_origins.begin(), opted_origins.end()},
                       std::move(records), std::move(callback));
}

void MahoSiteSessionCollector::CollectStorageOrigin(
    size_t index,
    std::vector<std::string> origins,
    std::vector<std::string> records,
    CompletionCallback callback) {
  if (!profile_ || profile_->IsOffTheRecord()) {
    std::move(callback).Run({});
    return;
  }
  if (index >= origins.size()) {
    std::move(callback).Run(std::move(records));
    return;
  }
  auto* partition = profile_->GetDefaultStoragePartition();
  auto* control = partition ? partition->GetLocalStorageControl() : nullptr;
  if (!control) {
    std::move(callback).Run(std::move(records));
    return;
  }
  const GURL origin_url(origins[index]);
  const auto key = blink::StorageKey::CreateFirstParty(
      url::Origin::Create(origin_url));
  storage_area_.reset();
  control->BindStorageArea(key, storage_area_.BindNewPipeAndPassReceiver());
  storage_area_->GetAll(mojo::NullRemote(), base::BindOnce(
      &MahoSiteSessionCollector::OnStorageCollected,
      weak_factory_.GetWeakPtr(), index, std::move(origins),
      std::move(records), std::move(callback)));
}

void MahoSiteSessionCollector::OnStorageCollected(
    size_t index,
    std::vector<std::string> origins,
    std::vector<std::string> records,
    CompletionCallback callback,
    std::vector<blink::mojom::KeyValuePtr> entries) {
  if (!profile_ || profile_->IsOffTheRecord()) {
    std::move(callback).Run({});
    return;
  }
  const std::string& origin = origins[index];
  auto record_it = std::find_if(records.begin(), records.end(),
      [&origin](const std::string& json) {
        auto value = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
        return value && value->is_dict() &&
               value->GetDict().FindString("origin") &&
               *value->GetDict().FindString("origin") == origin;
      });
  if (record_it != records.end()) {
    auto parsed = base::JSONReader::Read(*record_it, base::JSON_PARSE_RFC);
    if (parsed && parsed->is_dict()) {
      base::DictValue storage;
      for (const auto& entry : entries) {
        std::string key(entry->key.begin(), entry->key.end());
        std::string value(entry->value.begin(), entry->value.end());
        storage.Set(std::move(key), std::move(value));
      }
      parsed->GetDict().Set("localStorage", std::move(storage));
      record_it->clear();
      if (!base::JSONWriter::Write(*parsed, &*record_it))
        record_it->clear();
    }
  }
  CollectStorageOrigin(index + 1, std::move(origins), std::move(records),
                       std::move(callback));
}

}  // namespace maho
