// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/sync/maho_site_session_cookie.h"

#include <cstdint>
#include <limits>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/time/time.h"
#include "base/values.h"
#include "net/cookies/cookie_constants.h"
#include "net/cookies/cookie_inclusion_status.h"

namespace maho {
namespace {

std::optional<net::CookieSameSite> ParseSameSite(const std::string& value) {
  if (value == "lax") return net::CookieSameSite::LAX_MODE;
  if (value == "strict") return net::CookieSameSite::STRICT_MODE;
  if (value == "none") return net::CookieSameSite::NO_RESTRICTION;
  if (value == "unspecified") return net::CookieSameSite::UNSPECIFIED;
  return std::nullopt;
}

// Site sessions are scoped to one exact https origin: no path, query or ref.
bool IsExactHttpsOrigin(const GURL& origin) {
  return origin.is_valid() && origin.SchemeIs("https") &&
         origin == origin.GetWithEmptyPath() && !origin.has_query() &&
         !origin.has_ref();
}

}  // namespace

std::optional<std::string> EncodeSiteSessionCookie(
    const SiteSessionCookie& cookie) {
  if (!ParseSameSite(cookie.same_site)) return std::nullopt;
  base::DictValue value;
  value.Set("name", cookie.name);
  value.Set("value", cookie.value);
  value.Set("path", cookie.path);
  value.Set("secure", cookie.secure);
  value.Set("httpOnly", cookie.http_only);
  value.Set("sameSite", cookie.same_site);
  // JSON numbers are doubles; millisecond timestamps stay exact well past
  // year 275000, so a double carries them without loss.
  if (cookie.expires_utc_ms)
    value.Set("expiresUtcMs", static_cast<double>(*cookie.expires_utc_ms));
  else
    value.Set("expiresUtcMs", base::Value());
  std::string json;
  if (!base::JSONWriter::Write(base::Value(std::move(value)), &json))
    return std::nullopt;
  return json;
}

std::optional<SiteSessionCookie> DecodeSiteSessionCookie(
    const std::string& json) {
  auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) return std::nullopt;
  const auto& dict = parsed->GetDict();
  const std::string* name = dict.FindString("name");
  const std::string* value = dict.FindString("value");
  const std::string* path = dict.FindString("path");
  const std::string* same_site = dict.FindString("sameSite");
  const std::optional<bool> secure = dict.FindBool("secure");
  const std::optional<bool> http_only = dict.FindBool("httpOnly");
  const base::Value* expires = dict.Find("expiresUtcMs");
  if (!name || !value || !path || !same_site || !secure || !http_only ||
      !ParseSameSite(*same_site) || !expires)
    return std::nullopt;
  SiteSessionCookie cookie{*name, *value, *path, *same_site, std::nullopt,
                           *secure, *http_only};
  if (expires->is_int() || expires->is_double()) {
    const double milliseconds = expires->GetDouble();
    if (milliseconds < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
        milliseconds > static_cast<double>(std::numeric_limits<int64_t>::max()))
      return std::nullopt;
    cookie.expires_utc_ms = static_cast<int64_t>(milliseconds);
  } else if (!expires->is_none()) {
    return std::nullopt;
  }
  return cookie;
}

bool ShouldCollectSiteSessionCookie(const net::CanonicalCookie& cookie,
                                   const GURL& origin,
                                   base::Time now) {
  return IsExactHttpsOrigin(origin) && cookie.IsHostCookie() && cookie.Domain() == origin.host() &&
         cookie.IsSecure() && !cookie.IsPartitioned() &&
         !cookie.IsExpired(now);
}

bool ShouldApplySiteSessionCookie(const SiteSessionCookie& cookie,
                                  const GURL& origin,
                                  bool origin_is_opted_in,
                                  base::Time now) {
  if (!origin_is_opted_in || !IsExactHttpsOrigin(origin) || !cookie.secure || cookie.name.empty() || cookie.path.empty() ||
      cookie.path.front() != '/' || !ParseSameSite(cookie.same_site))
    return false;
  if (cookie.expires_utc_ms &&
      now >= base::Time::UnixEpoch() + base::Milliseconds(*cookie.expires_utc_ms))
    return false;
  return true;
}

std::optional<net::CanonicalCookie> BuildSiteSessionCanonicalCookie(
    const SiteSessionCookie& cookie,
    const GURL& origin,
    base::Time now) {
  if (!ShouldApplySiteSessionCookie(cookie, origin, true, now))
    return std::nullopt;
  const auto same_site = ParseSameSite(cookie.same_site);
  if (!same_site) return std::nullopt;
  const base::Time expires = cookie.expires_utc_ms
      ? base::Time::UnixEpoch() + base::Milliseconds(*cookie.expires_utc_ms)
      : base::Time();
  GURL::Replacements with_path;
  with_path.SetPathStr(cookie.path);
  net::CookieInclusionStatus status;
  auto result = net::CanonicalCookie::CreateSanitizedCookie(
      origin.ReplaceComponents(with_path), cookie.name, cookie.value, "",
      cookie.path, now, expires, now, true, cookie.http_only, *same_site,
      net::CookiePriority::COOKIE_PRIORITY_MEDIUM, std::nullopt, &status);
  if (!result || !result->IsHostCookie() || result->Domain() != origin.host())
    return std::nullopt;
  return *result;
}

}  // namespace maho
