// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_NET_MAHO_AD_BLOCK_THROTTLE_H_
#define MAHO_BROWSER_NET_MAHO_AD_BLOCK_THROTTLE_H_

#include "third_party/blink/public/common/loader/url_loader_throttle.h"

class MahoAdBlockThrottle : public blink::URLLoaderThrottle {
 public:
  MahoAdBlockThrottle();
  ~MahoAdBlockThrottle() override;

  MahoAdBlockThrottle(const MahoAdBlockThrottle&) = delete;
  MahoAdBlockThrottle& operator=(const MahoAdBlockThrottle&) = delete;

  void WillStartRequest(network::ResourceRequest* request,
                        bool* defer) override;
};

#endif  // MAHO_BROWSER_NET_MAHO_AD_BLOCK_THROTTLE_H_
