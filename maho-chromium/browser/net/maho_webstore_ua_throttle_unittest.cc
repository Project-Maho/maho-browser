// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_webstore_ua_throttle.h"

#include <string>
#include <vector>

#include "net/http/http_request_headers.h"
#include "services/network/public/cpp/resource_request.h"
#include "url/gurl.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

TEST(MahoWebStoreUAThrottleTest, NeedsChromeLikeUA) {
  EXPECT_TRUE(MahoWebStoreUAThrottle::NeedsChromeLikeUA(
      GURL("https://chrome.google.com/webstore/category/extensions")));
  EXPECT_TRUE(MahoWebStoreUAThrottle::NeedsChromeLikeUA(
      GURL("https://chromewebstore.google.com/")));
  EXPECT_TRUE(MahoWebStoreUAThrottle::NeedsChromeLikeUA(
      GURL("https://clients2.google.com/service/update2/crx")));
  EXPECT_TRUE(MahoWebStoreUAThrottle::NeedsChromeLikeUA(
      GURL("https://update.googleapis.com/")));

  EXPECT_FALSE(MahoWebStoreUAThrottle::NeedsChromeLikeUA(
      GURL("https://www.google.com")));
  EXPECT_FALSE(MahoWebStoreUAThrottle::NeedsChromeLikeUA(
      GURL("https://example.com")));
  EXPECT_FALSE(MahoWebStoreUAThrottle::NeedsChromeLikeUA(GURL()));
}

TEST(MahoWebStoreUAThrottleTest, StripMahoToken) {
  std::string original_ua =
      "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/120.0.0.0 Maho/1.0 Safari/537.36";
  std::string stripped_ua =
      "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

  EXPECT_EQ(MahoWebStoreUAThrottle::StripMahoToken(original_ua), stripped_ua);
  EXPECT_EQ(MahoWebStoreUAThrottle::StripMahoToken(stripped_ua), stripped_ua);
}

TEST(MahoWebStoreUAThrottleTest, WillStartRequestModifiesUAForWebstore) {
  MahoWebStoreUAThrottle throttle;
  network::ResourceRequest request;
  request.url = GURL("https://chromewebstore.google.com/");
  std::string raw_ua = "Chrome/120.0.0.0 Maho/1.0";
  request.headers.SetHeader(net::HttpRequestHeaders::kUserAgent, raw_ua);

  bool defer = false;
  throttle.WillStartRequest(&request, &defer);

  std::optional<std::string> modified_ua =
      request.headers.GetHeader(net::HttpRequestHeaders::kUserAgent);
  EXPECT_TRUE(modified_ua.has_value());
  EXPECT_EQ(*modified_ua, "Chrome/120.0.0.0");
}

TEST(MahoWebStoreUAThrottleTest, WillStartRequestDoesNotModifyUAForOtherSites) {
  MahoWebStoreUAThrottle throttle;
  network::ResourceRequest request;
  request.url = GURL("https://example.com/");
  std::string raw_ua = "Chrome/120.0.0.0 Maho/1.0";
  request.headers.SetHeader(net::HttpRequestHeaders::kUserAgent, raw_ua);

  bool defer = false;
  throttle.WillStartRequest(&request, &defer);

  std::optional<std::string> modified_ua =
      request.headers.GetHeader(net::HttpRequestHeaders::kUserAgent);
  EXPECT_TRUE(modified_ua.has_value());
  EXPECT_EQ(*modified_ua, raw_ua);
}

}  // namespace
}  // namespace maho
