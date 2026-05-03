// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_ad_block_throttle.h"

#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "net/base/net_errors.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"

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

MahoAdBlockThrottle::MahoAdBlockThrottle() = default;
MahoAdBlockThrottle::~MahoAdBlockThrottle() = default;

void MahoAdBlockThrottle::WillStartRequest(network::ResourceRequest* request,
                                           bool* defer) {
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

  if (maho::core::ShouldBlockRequest(core, url.c_str(), source.c_str(),
                                     type)) {
    delegate_->CancelWithError(net::ERR_BLOCKED_BY_CLIENT);
  }
}
