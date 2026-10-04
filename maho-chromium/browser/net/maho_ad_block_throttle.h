// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_NET_MAHO_AD_BLOCK_THROTTLE_H_
#define MAHO_BROWSER_NET_MAHO_AD_BLOCK_THROTTLE_H_

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "url/gurl.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"

namespace network {
struct ResourceRequest;
}  // namespace network

using MahoAdBlockBlockedCallback =
    base::RepeatingCallback<void(const network::ResourceRequest&)>;

class MahoAdBlockThrottle : public blink::URLLoaderThrottle {
 public:
  explicit MahoAdBlockThrottle(MahoAdBlockBlockedCallback on_blocked);
  ~MahoAdBlockThrottle() override;

  MahoAdBlockThrottle(const MahoAdBlockThrottle&) = delete;
  MahoAdBlockThrottle& operator=(const MahoAdBlockThrottle&) = delete;

  void WillStartRequest(network::ResourceRequest* request,
                        bool* defer) override;
  const GURL* TakeDeferredStartRedirectUrl() override;

 private:
  MahoAdBlockBlockedCallback on_blocked_;
  GURL redirect_url_;
  bool redirect_pending_ = false;
  base::WeakPtrFactory<MahoAdBlockThrottle> weak_factory_{this};
};

#endif  // MAHO_BROWSER_NET_MAHO_AD_BLOCK_THROTTLE_H_
