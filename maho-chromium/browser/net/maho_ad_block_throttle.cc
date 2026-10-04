// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_ad_block_throttle.h"

#include <utility>
#include <optional>
#include "base/functional/bind.h"

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

const GURL* MahoAdBlockThrottle::TakeDeferredStartRedirectUrl() {
  return std::exchange(redirect_pending_, false) ? &redirect_url_ : nullptr;
}

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

  const uint64_t generation = maho::GetCoreGeneration();
  *defer = true;
  maho::PostCoreTask<std::optional<maho::core::BlockResult>>(
      FROM_HERE,
      base::BindOnce([](uint64_t generation, std::string url,
                        std::string source, std::string type)
                        -> std::optional<maho::core::BlockResult> {
        if (generation != maho::GetCoreGeneration()) {
          return std::nullopt;
        }
        MahoCore* core = maho::GetCore();
        if (!core) {
          return std::nullopt;
        }
        return maho::core::CheckRequest(
            core, url.c_str(), source.c_str(), type.c_str());
      }, generation, url, source, std::string(type)),
      base::BindOnce([](base::WeakPtr<MahoAdBlockThrottle> throttle,
                        uint64_t generation, network::ResourceRequest request,
                        std::optional<maho::core::BlockResult> result) {
        if (!throttle) {
          return;
        }
        if (!result || generation != maho::GetCoreGeneration()) {
          throttle->delegate_->CancelWithError(net::ERR_ABORTED);
          return;
        }
        if (!result->redirect.empty() || !result->rewritten_url.empty()) {
          throttle->redirect_url_ = GURL(!result->redirect.empty()
                                            ? result->redirect
                                            : result->rewritten_url);
          if (!throttle->redirect_url_.is_valid() ||
              (request.url.SchemeIsHTTPOrHTTPS() &&
               !throttle->redirect_url_.SchemeIsHTTPOrHTTPS())) {
            throttle->delegate_->CancelWithError(net::ERR_BLOCKED_BY_CLIENT);
            return;
          }
          throttle->redirect_pending_ = true;
          request.url = throttle->redirect_url_;
          if (!result->redirect.empty() && throttle->on_blocked_) {
            throttle->on_blocked_.Run(request);
          }
        } else if (result->blocked) {
          if (throttle->on_blocked_) {
            throttle->on_blocked_.Run(request);
          }
          if (throttle) {
            throttle->delegate_->CancelWithError(net::ERR_BLOCKED_BY_CLIENT);
          }
          return;
        }
        if (throttle) {
          throttle->delegate_->Resume();
        }
      }, weak_factory_.GetWeakPtr(), generation, *request));
}
