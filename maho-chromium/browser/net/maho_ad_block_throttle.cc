// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_ad_block_throttle.h"

#include <utility>

#include "content/public/browser/browser_thread.h"
#include "net/base/net_errors.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "url/gurl.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/net/maho_ad_block_request_util.h"
#include "maho/third_party/maho/maho_bridge.h"

MahoAdBlockThrottle::MahoAdBlockThrottle(
    MahoAdBlockBlockedCallback on_blocked)
    : on_blocked_(std::move(on_blocked)) {}
MahoAdBlockThrottle::~MahoAdBlockThrottle() = default;

void MahoAdBlockThrottle::WillStartRequest(network::ResourceRequest* request,
                                           bool* defer) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }

  const std::string url = request->url.spec();
  const std::string source =
      request->request_initiator.has_value()
          ? request->request_initiator->GetURL().spec()
          : "";
  const char* type = maho::MapRequestToFilterType(*request);

  auto result =
      maho::core::CheckRequest(core, url.c_str(), source.c_str(), type);

  if (!result.redirect.empty()) {
    request->url = GURL(result.redirect);
    if (on_blocked_) {
      on_blocked_.Run(*request);
    }
    return;
  }

  if (!result.rewritten_url.empty()) {
    request->url = GURL(result.rewritten_url);
    if (on_blocked_) {
      on_blocked_.Run(*request);
    }
    return;
  }

  if (result.blocked) {
    if (delegate_) {
      delegate_->CancelWithError(net::ERR_BLOCKED_BY_CLIENT);
    }
    if (on_blocked_) {
      on_blocked_.Run(*request);
    }
  }
}
