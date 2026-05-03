// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_webstore_ua_throttle.h"

#include "base/strings/string_util.h"
#include "net/http/http_request_headers.h"
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
      path.find("/service/update2/crx") != std::string::npos) {
    return true;
  }
  if (host == "update.googleapis.com") {
    return true;
  }
  return false;
}

// static
std::string MahoWebStoreUAThrottle::StripMahoToken(const std::string& ua) {
  const std::string kToken = " Maho/1.0";
  std::string result = ua;
  std::string::size_type pos = result.find(kToken);
  if (pos != std::string::npos) {
    result.erase(pos, kToken.size());
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
    }
  }
}

void MahoWebStoreUAThrottle::WillRedirectRequest(
    net::RedirectInfo* redirect_info,
    const network::mojom::URLResponseHead& /* response_head */,
    bool* defer,
    std::vector<std::string>* /* to_be_removed_request_headers */,
    net::HttpRequestHeaders* modified_request_headers,
    net::HttpRequestHeaders* /* modified_cors_exempt_request_headers */) {
  const GURL new_url(redirect_info->new_url);
  if (NeedsChromeLikeUA(new_url) && !cached_clean_ua_.empty() &&
      modified_request_headers) {
    modified_request_headers->SetHeader(net::HttpRequestHeaders::kUserAgent,
                                       cached_clean_ua_);
  }
}
