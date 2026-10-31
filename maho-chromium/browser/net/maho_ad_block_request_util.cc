// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_ad_block_request_util.h"

#include "services/network/public/cpp/resource_request.h"

namespace maho {

const char* MapRequestDestinationToFilterType(
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
    case network::mojom::RequestDestination::kFrame:
    case network::mojom::RequestDestination::kFencedframe:
      return "subdocument";
    case network::mojom::RequestDestination::kObject:
    case network::mojom::RequestDestination::kEmbed:
      return "object";
    case network::mojom::RequestDestination::kAudio:
    case network::mojom::RequestDestination::kVideo:
      return "media";
    case network::mojom::RequestDestination::kWorker:
    case network::mojom::RequestDestination::kSharedWorker:
    case network::mojom::RequestDestination::kServiceWorker:
    case network::mojom::RequestDestination::kAudioWorklet:
    case network::mojom::RequestDestination::kPaintWorklet:
      return "script";
    default:
      return "other";
  }
}

const char* MapRequestToFilterType(const network::ResourceRequest& request) {
  if (request.is_fetch_like_api) {
    return "xmlhttprequest";
  }
  if (request.destination == network::mojom::RequestDestination::kEmpty) {
    return request.keepalive ? "ping" : "other";
  }
  return MapRequestDestinationToFilterType(request.destination);
}

}  // namespace maho
