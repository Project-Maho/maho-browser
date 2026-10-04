// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_webstore_ua_throttle.h"
#include "base/metrics/histogram_functions.h"
#include "base/logging.h"
#include "net/http/http_response_headers.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "base/strings/string_util.h"
#include "net/http/http_request_headers.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/http_request_headers_update_params.h"
#include "services/network/public/cpp/resource_request.h"
#include "url/gurl.h"

MahoWebStoreUAThrottle::MahoWebStoreUAThrottle() = default;
MahoWebStoreUAThrottle::~MahoWebStoreUAThrottle() = default;

// static
bool MahoWebStoreUAThrottle::NeedsChromeLikeUA(const GURL& url) {
  if (!url.is_valid()) {
    return false;
  }
  const auto host = url.host();
  const auto path = url.path();

  if (host == "chrome.google.com" &&
      base::StartsWith(path, "/webstore", base::CompareCase::INSENSITIVE_ASCII)) {
    return true;
  }
  if (host == "chromewebstore.google.com") {
    return true;
  }
  if (host == "clients2.google.com" &&
      base::StartsWith(path, "/service/update2/crx",
                       base::CompareCase::INSENSITIVE_ASCII)) {
    return true;
  }
  if (host == "update.googleapis.com") {
    return true;
  }
  return false;
}

// static
std::string MahoWebStoreUAThrottle::StripMahoToken(const std::string& ua) {
  // Strip " Maho/<version>" for any version so a version bump in
  // user_agent_utils.cc does not silently break webstore extension installs.
  static constexpr char kPrefix[] = " Maho/";
  std::string result = ua;
  std::string::size_type pos = result.find(kPrefix);
  if (pos != std::string::npos) {
    std::string::size_type end = pos + (sizeof(kPrefix) - 1);
    while (end < result.size() &&
           (base::IsAsciiDigit(result[end]) || result[end] == '.')) {
      ++end;
    }
    result.erase(pos, end - pos);
  }
  return result;
}

void MahoWebStoreUAThrottle::WillStartRequest(
    network::ResourceRequest* request,
    bool* defer) {
  auto ua = request->headers.GetHeader(net::HttpRequestHeaders::kUserAgent);
  if (ua.has_value()) {
    cached_clean_ua_ = StripMahoToken(*ua);
    if (NeedsChromeLikeUA(request->url)) {
      request->headers.SetHeader(net::HttpRequestHeaders::kUserAgent,
                                 cached_clean_ua_);
      base::UmaHistogramBoolean("Maho.Extensions.UAThrottleApplied", true);
    }
  }
}

void MahoWebStoreUAThrottle::WillRedirectRequest(
    net::RedirectInfo* redirect_info,
    const network::mojom::URLResponseHead& /* response_head */,
    bool* defer,
    network::HttpRequestHeadersUpdateParams* headers_update_params) {
  const GURL new_url(redirect_info->new_url);
  if (NeedsChromeLikeUA(new_url) && !cached_clean_ua_.empty() &&
      headers_update_params) {
    headers_update_params->modified_headers.SetHeader(
        net::HttpRequestHeaders::kUserAgent, cached_clean_ua_);
    base::UmaHistogramBoolean("Maho.Extensions.UAThrottleApplied", true);
  }
}

void MahoWebStoreUAThrottle::WillProcessResponse(
    const GURL& response_url,
    network::mojom::URLResponseHead* response_head,
    bool* defer) {
  if (response_head && response_head->headers && NeedsChromeLikeUA(response_url)) {
    int response_code = response_head->headers->response_code();
    if (response_code == 401 || response_code == 403) {
      LOG(WARNING) << "MahoWebStoreUAThrottle: Web Store request to "
                   << response_url.spec() << " failed with HTTP " << response_code
                   << ". User-Agent used: " << cached_clean_ua_;
    }
  }
}
