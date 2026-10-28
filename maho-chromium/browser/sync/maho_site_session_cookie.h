// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_SYNC_MAHO_SITE_SESSION_COOKIE_H_
#define MAHO_BROWSER_SYNC_MAHO_SITE_SESSION_COOKIE_H_

#include <optional>
#include <cstdint>
#include <string>

#include "base/time/time.h"
#include "net/cookies/canonical_cookie.h"
#include "url/gurl.h"

namespace maho {

struct SiteSessionCookie {
  std::string name;
  std::string value;
  std::string path;
  std::string same_site;
  std::optional<int64_t> expires_utc_ms;
  bool secure = false;
  bool http_only = false;
};

std::optional<std::string> EncodeSiteSessionCookie(
    const SiteSessionCookie& cookie);
std::optional<SiteSessionCookie> DecodeSiteSessionCookie(
    const std::string& json);
bool ShouldCollectSiteSessionCookie(const net::CanonicalCookie& cookie,
                                   const GURL& origin,
                                   base::Time now);
bool ShouldApplySiteSessionCookie(const SiteSessionCookie& cookie,
                                  const GURL& origin,
                                  bool origin_is_opted_in,
                                  base::Time now);
std::optional<net::CanonicalCookie> BuildSiteSessionCanonicalCookie(
    const SiteSessionCookie& cookie,
    const GURL& origin,
    base::Time now);

}  // namespace maho

#endif  // MAHO_BROWSER_SYNC_MAHO_SITE_SESSION_COOKIE_H_
