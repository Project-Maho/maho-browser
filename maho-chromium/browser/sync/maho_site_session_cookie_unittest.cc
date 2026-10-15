// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/sync/maho_site_session_cookie.h"

#include "base/time/time.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_constants.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

TEST(MahoSiteSessionCookieTest, RoundTripsWireFormat) {
  SiteSessionCookie input{"sid", "secret", "/", "lax", std::nullopt, true,
                          true};
  auto json = EncodeSiteSessionCookie(input);
  ASSERT_TRUE(json);
  auto decoded = DecodeSiteSessionCookie(*json);
  ASSERT_TRUE(decoded);
  EXPECT_EQ(decoded->name, input.name);
  EXPECT_EQ(decoded->value, input.value);
  EXPECT_EQ(decoded->path, input.path);
  EXPECT_EQ(decoded->same_site, input.same_site);
  EXPECT_EQ(decoded->expires_utc_ms, input.expires_utc_ms);
  EXPECT_EQ(decoded->secure, input.secure);
  EXPECT_EQ(decoded->http_only, input.http_only);
}

TEST(MahoSiteSessionCookieTest, CollectFilterExcludesUnsafeCookies) {
  const GURL origin("https://example.com");
  const base::Time now = base::Time::Now();
  auto make = [&](const GURL& url, const std::string& line) {
    return net::CanonicalCookie::CreateForTesting(
        url, line, now, net::CookieSourceType::kHTTP);
  };
  auto host = make(origin, "sid=x; Secure; Path=/");
  ASSERT_TRUE(host);
  EXPECT_TRUE(ShouldCollectSiteSessionCookie(*host, origin, now));
  auto domain = make(origin, "sid=x; Domain=example.com; Secure; Path=/");
  ASSERT_TRUE(domain);
  EXPECT_FALSE(ShouldCollectSiteSessionCookie(*domain, origin, now));
  auto other = make(GURL("https://other.example"), "sid=x; Secure; Path=/");
  ASSERT_TRUE(other);
  EXPECT_FALSE(ShouldCollectSiteSessionCookie(*other, origin, now));
  auto insecure = make(origin, "sid=x; Path=/");
  ASSERT_TRUE(insecure);
  EXPECT_FALSE(ShouldCollectSiteSessionCookie(*insecure, origin, now));
  auto partitioned = make(origin, "sid=x; Secure; Partitioned; SameSite=None; Path=/");
  ASSERT_TRUE(partitioned);
  EXPECT_TRUE(partitioned->IsPartitioned());
  EXPECT_FALSE(ShouldCollectSiteSessionCookie(*partitioned, origin, now));
  auto expired = make(origin, "sid=x; Secure; Expires=Wed, 01 Jan 2020 00:00:00 GMT; Path=/");
  ASSERT_TRUE(expired);
  EXPECT_FALSE(ShouldCollectSiteSessionCookie(*expired, origin, now));
}

TEST(MahoSiteSessionCookieTest, EncodeRejectsUnknownSameSiteAndDecodeRejectsMalformedFields) {
  SiteSessionCookie cookie{"sid", "x", "/", "invalid", std::nullopt, true, false};
  EXPECT_FALSE(EncodeSiteSessionCookie(cookie));
  EXPECT_FALSE(DecodeSiteSessionCookie(
      R"({"name":"sid","value":"x","path":"/","secure":true,"httpOnly":false,"sameSite":"lax"})"));
  EXPECT_FALSE(DecodeSiteSessionCookie(
      R"({"name":"sid","value":"x","path":"/","secure":true,"httpOnly":false,"sameSite":"other","expiresUtcMs":null})"));
}

TEST(MahoSiteSessionCookieTest, ApplyRequiresOptInAndUnexpiredSecureCookie) {
  const GURL origin("https://example.com");
  const base::Time now = base::Time::Now();
  SiteSessionCookie cookie{"sid", "value", "/", "lax", std::nullopt, true,
                           true};
  EXPECT_TRUE(ShouldApplySiteSessionCookie(cookie, origin, true, now));
  EXPECT_FALSE(ShouldApplySiteSessionCookie(cookie, origin, false, now));
  cookie.expires_utc_ms = (now - base::Hours(1) - base::Time::UnixEpoch())
                              .InMilliseconds();
  EXPECT_FALSE(ShouldApplySiteSessionCookie(cookie, origin, true, now));
  cookie.expires_utc_ms.reset();
  EXPECT_FALSE(ShouldApplySiteSessionCookie(
      cookie, GURL("https://example.com/path"), true, now));
  EXPECT_FALSE(ShouldApplySiteSessionCookie(
      cookie, GURL("http://example.com"), true, now));
  cookie.secure = false;
  EXPECT_FALSE(ShouldApplySiteSessionCookie(cookie, origin, true, now));
}

TEST(MahoSiteSessionCookieTest, RebuildsUnexpiredCookieAsHostOnly) {
  const GURL origin("https://example.com");
  const base::Time now = base::Time::Now();
  SiteSessionCookie cookie{"sid", "v", "/account", "strict", std::nullopt,
                           true, true};
  auto rebuilt = BuildSiteSessionCanonicalCookie(cookie, origin, now);
  ASSERT_TRUE(rebuilt);
  EXPECT_TRUE(rebuilt->IsHostCookie());
  EXPECT_EQ(rebuilt->Domain(), "example.com");
  EXPECT_TRUE(rebuilt->IsSecure());
  EXPECT_TRUE(rebuilt->IsHttpOnly());
  EXPECT_EQ(rebuilt->Path(), "/account");
}

}  // namespace
}  // namespace maho
