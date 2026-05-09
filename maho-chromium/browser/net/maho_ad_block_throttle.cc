// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_ad_block_throttle.h"

#include "content/public/browser/browser_thread.h"
#include "net/base/net_errors.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "url/gurl.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_bridge.h"

namespace {

const char* MapRequestDestination(
    network::mojom::RequestDestination destination) {
  switch (destination) {
    case network::mojom::RequestDestination::kScript:
      return "script";
    case network::mojom::RequestDestination::kImage:
      return "image";
    case network::mojom::RequestDestination::kStyle:
      return "stylesheet";
    case network::mojom::RequestDestination::kFont:
      return "font";
    case network::mojom::RequestDestination::kDocument:
      return "document";
    case network::mojom::RequestDestination::kIframe:
      return "subdocument";
    case network::mojom::RequestDestination::kObject:
    case network::mojom::RequestDestination::kEmbed:
      return "object";
    case network::mojom::RequestDestination::kAudio:
    case network::mojom::RequestDestination::kVideo:
      return "media";
    default:
      return "other";
  }
}

}  // namespace

MahoAdBlockThrottle::MahoAdBlockThrottle(base::RepeatingClosure on_blocked)
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
  const char* type = MapRequestDestination(request->destination);

  auto result =
      maho::core::CheckRequest(core, url.c_str(), source.c_str(), type);

  if (!result.redirect.empty()) {
    request->url = GURL(result.redirect);
    if (on_blocked_) {
      on_blocked_.Run();
    }
    return;
  }

  if (!result.rewritten_url.empty()) {
    request->url = GURL(result.rewritten_url);
    if (on_blocked_) {
      on_blocked_.Run();
    }
    return;
  }

  if (result.blocked) {
    if (delegate_) {
      delegate_->CancelWithError(net::ERR_BLOCKED_BY_CLIENT);
    }
    if (on_blocked_) {
      on_blocked_.Run();
    }
  }
}
