// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_url_scheme.h"

#include <string>
#include <string_view>

#include "base/strings/string_util.h"
#include "maho/components/constants/url_constants.h"
#include "maho/components/constants/webui_url_constants.h"
#include "url/gurl.h"

namespace maho {

namespace {

constexpr char kActualScheme[] = "chrome";

const MahoUrlAliasRecord* FindRecordByAliasHost(std::string_view host) {
  std::string lower_host = base::ToLowerASCII(host);
  for (const auto& record : kMahoUrlAliases) {
    if (record.alias_host == lower_host) {
      return &record;
    }
  }
  return nullptr;
}

const MahoUrlAliasRecord* FindRecordByActualHost(std::string_view host) {
  std::string lower_host = base::ToLowerASCII(host);
  for (const auto& record : kMahoUrlAliases) {
    if (record.actual_host == lower_host) {
      return &record;
    }
  }
  return nullptr;
}

}  // namespace

bool IsValidMahoUrlAlias(const GURL& url) {
  if (!url.is_valid()) {
    return false;
  }
  if (!url.SchemeIs(kMahoUIScheme)) {
    return false;
  }
  if (url.has_username() || url.has_password()) {
    return false;
  }
  if (url.has_port()) {
    return false;
  }

  std::string_view host = url.host();
  if (host.empty()) {
    return false;
  }

  return FindRecordByAliasHost(host) != nullptr;
}

bool MapMahoUrlAliasToActualUrl(const GURL& url, GURL* out_actual_url) {
  if (!IsValidMahoUrlAlias(url)) {
    return false;
  }

  const MahoUrlAliasRecord* record = FindRecordByAliasHost(url.host());
  if (!record) {
    return false;
  }

  if (out_actual_url) {
    GURL::Replacements replacements;
    replacements.SetSchemeStr(kActualScheme);
    replacements.SetHostStr(record->actual_host);
    *out_actual_url = url.ReplaceComponents(replacements);
  }

  return true;
}

bool ResolveActualUrlToMahoAlias(const GURL& url, GURL* out_alias_url) {
  if (!url.is_valid()) {
    return false;
  }
  if (!url.SchemeIs(kActualScheme)) {
    return false;
  }
  if (url.has_username() || url.has_password()) {
    return false;
  }
  if (url.has_port()) {
    return false;
  }

  std::string_view host = url.host();
  if (host.empty()) {
    return false;
  }

  const MahoUrlAliasRecord* record = FindRecordByActualHost(host);
  if (!record) {
    return false;
  }

  if (out_alias_url) {
    GURL::Replacements replacements;
    replacements.SetSchemeStr(kMahoUIScheme);
    replacements.SetHostStr(record->alias_host);
    *out_alias_url = url.ReplaceComponents(replacements);
  }

  return true;
}

}  // namespace maho
