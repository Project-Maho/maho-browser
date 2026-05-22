// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_sidebar_boosts_data.h"

#include <optional>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "url/gurl.h"

namespace maho {

BoostItem::BoostItem() = default;
BoostItem::BoostItem(const BoostItem&) = default;
BoostItem::BoostItem(BoostItem&&) = default;
BoostItem& BoostItem::operator=(const BoostItem&) = default;
BoostItem& BoostItem::operator=(BoostItem&&) = default;
BoostItem::~BoostItem() = default;

namespace {

std::string NormalizePatternForHostnameExtraction(std::string url_pattern) {
  if (base::StartsWith(url_pattern, "*://",
                       base::CompareCase::SENSITIVE)) {
    url_pattern.replace(0, 4, "https://");
  }

  const size_t wildcard_host_start = url_pattern.find("://*.");
  if (wildcard_host_start != std::string::npos) {
    url_pattern.erase(wildcard_host_start + 3, 2);
  }

  return url_pattern;
}

std::optional<std::string> HostnameFromPattern(const std::string& url_pattern) {
  const GURL url(NormalizePatternForHostnameExtraction(url_pattern));
  if (!url.is_valid() || !url.has_host() || url.host().empty()) {
    return std::nullopt;
  }
  return std::string(url.host());
}

}  // namespace

std::vector<BoostItem> ParseBoosts() {
  std::vector<BoostItem> boosts;
  MahoCore* core = maho::GetCore();
  if (!core) {
    return boosts;
  }

  char* json_str = maho_core_get_boost_view_models(core);
  if (!json_str) {
    return boosts;
  }

  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return boosts;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }

    const std::string* id = dict->FindString("id");
    const std::string* url_pattern = dict->FindString("urlPattern");
    if (!id || id->empty() || !url_pattern || url_pattern->empty()) {
      continue;
    }

    BoostItem boost;
    boost.id = *id;
    boost.url_pattern = *url_pattern;
    if (const std::string* custom_css = dict->FindString("customCss")) {
      boost.custom_css = *custom_css;
    }
    if (const std::string* custom_js = dict->FindString("customJs")) {
      boost.custom_js = *custom_js;
    }
    boost.enabled = dict->FindBool("enabled").value_or(false);
    boosts.push_back(std::move(boost));
  }

  return boosts;
}

std::u16string BoostTitleFromUrlPattern(const std::string& url_pattern) {
  if (std::optional<std::string> hostname = HostnameFromPattern(url_pattern)) {
    return base::UTF8ToUTF16(*hostname);
  }
  return base::UTF8ToUTF16(url_pattern);
}

std::u16string BoostSubtitleFromUrlPattern(const std::string& url_pattern) {
  return base::UTF8ToUTF16(url_pattern);
}

std::u16string BoostBadgeTextFromUrlPattern(const std::string& url_pattern) {
  const std::u16string title = BoostTitleFromUrlPattern(url_pattern);
  for (char16_t character : title) {
    if (base::IsAsciiAlpha(character)) {
      return std::u16string(1, base::ToUpperASCII(character));
    }
    if (base::IsAsciiDigit(character)) {
      return std::u16string(1, character);
    }
  }
  return u"B";
}

}  // namespace maho
