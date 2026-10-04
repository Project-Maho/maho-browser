// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_webstore_ua_throttle.h"

#include <string>
#include <vector>

#include "net/http/http_request_headers.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/http_request_headers_update_params.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
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

TEST(MahoWebStoreUAThrottleTest, RedirectUpdatesOnlyOrdinaryUAForWebstore) {
  MahoWebStoreUAThrottle throttle;
  network::ResourceRequest request;
  request.url = GURL("https://example.com/");
  request.headers.SetHeader(net::HttpRequestHeaders::kUserAgent,
                            "Chrome/154.0 Maho/1.0");
  bool defer = false;
  throttle.WillStartRequest(&request, &defer);

  net::RedirectInfo redirect;
  redirect.new_url = GURL("https://chromewebstore.google.com/");
  network::HttpRequestHeadersUpdateParams updates;
  updates.removed_headers.push_back("X-Remove");
  updates.modified_headers.SetHeader("X-Keep", "ordinary");
  updates.modified_cors_exempt_headers.SetHeader(
      net::HttpRequestHeaders::kUserAgent, "cors-sentinel");
  auto response = network::mojom::URLResponseHead::New();
  throttle.WillRedirectRequest(&redirect, *response, &defer, &updates);

  EXPECT_EQ(updates.modified_headers.GetHeader(net::HttpRequestHeaders::kUserAgent),
            "Chrome/154.0");
  EXPECT_EQ(updates.modified_headers.GetHeader("X-Keep"), "ordinary");
  EXPECT_EQ(updates.modified_cors_exempt_headers.GetHeader(
                net::HttpRequestHeaders::kUserAgent), "cors-sentinel");
  EXPECT_EQ(updates.removed_headers, std::vector<std::string>({"X-Remove"}));
  EXPECT_FALSE(defer);

  redirect.new_url = GURL("https://example.org/");
  updates.modified_headers.SetHeader(net::HttpRequestHeaders::kUserAgent,
                                    "leave-unchanged");
  throttle.WillRedirectRequest(&redirect, *response, &defer, &updates);
  EXPECT_EQ(updates.modified_headers.GetHeader(net::HttpRequestHeaders::kUserAgent),
            "leave-unchanged");
}

}  // namespace
}  // namespace maho
