// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_NET_MAHO_WEBSTORE_UA_THROTTLE_H_
#define MAHO_BROWSER_NET_MAHO_WEBSTORE_UA_THROTTLE_H_

#include <string>

#include "third_party/blink/public/common/loader/url_loader_throttle.h"

namespace network {
struct HttpRequestHeadersUpdateParams;
}

// Strips the "Maho/1.0" token from User-Agent on Chrome Web Store and
// CRX-download domains so that Google does not block extension installs.
class MahoWebStoreUAThrottle : public blink::URLLoaderThrottle {
 public:
  MahoWebStoreUAThrottle();
  ~MahoWebStoreUAThrottle() override;

  MahoWebStoreUAThrottle(const MahoWebStoreUAThrottle&) = delete;
  MahoWebStoreUAThrottle& operator=(const MahoWebStoreUAThrottle&) = delete;

  void WillStartRequest(network::ResourceRequest* request,
                        bool* defer) override;

  void WillRedirectRequest(
      net::RedirectInfo* redirect_info,
      const network::mojom::URLResponseHead& response_head,
      bool* defer,
      network::HttpRequestHeadersUpdateParams* headers_update_params) override;

  void WillProcessResponse(const GURL& response_url,
                           network::mojom::URLResponseHead* response_head,
                           bool* defer) override;

  static bool NeedsChromeLikeUA(const GURL& url);
  static std::string StripMahoToken(const std::string& ua);

 private:
  std::string cached_clean_ua_;
};

#endif  // MAHO_BROWSER_NET_MAHO_WEBSTORE_UA_THROTTLE_H_
