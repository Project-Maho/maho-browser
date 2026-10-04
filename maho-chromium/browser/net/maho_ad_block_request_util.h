// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_NET_MAHO_AD_BLOCK_REQUEST_UTIL_H_
#define MAHO_BROWSER_NET_MAHO_AD_BLOCK_REQUEST_UTIL_H_

#include "services/network/public/mojom/fetch_api.mojom-shared.h"

namespace network {
struct ResourceRequest;
}  // namespace network

namespace maho {

// Maps a Chromium request destination to the adblock-engine request-type string
// consumed by maho::core::CheckRequest (adblock-rust `Request` type). Shared by
// the browser-side navigation throttle and the subresource proxying factory so
// both classify requests identically. RequestDestination::kEmpty (fetch/XHR/
// beacon) maps to "other"; prefer MapRequestToFilterType when a request is
// available.
const char* MapRequestDestinationToFilterType(
    network::mojom::RequestDestination destination);

// Request-level classification, aligned with Chromium's WebRequest resource
// typing: fetch/XHR (is_fetch_like_api) -> "xmlhttprequest"; a kEmpty keepalive
// request -> "ping"; otherwise the destination mapping above. Preferred by all
// callers since it also distinguishes worker scripts and beacons.
const char* MapRequestToFilterType(const network::ResourceRequest& request);

}  // namespace maho

#endif  // MAHO_BROWSER_NET_MAHO_AD_BLOCK_REQUEST_UTIL_H_
