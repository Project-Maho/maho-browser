// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_NET_MAHO_PROXYING_URL_LOADER_FACTORY_H_
#define MAHO_BROWSER_NET_MAHO_PROXYING_URL_LOADER_FACTORY_H_

#include "base/memory/weak_ptr.h"
#include "content/public/browser/global_routing_id.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"

namespace content {
class RenderFrameHost;
}  // namespace content

namespace network {
struct ResourceRequest;
class URLLoaderFactoryBuilder;
}  // namespace network

class MahoAdBlockTabHelper;

// Browser-side proxying URLLoaderFactory that applies Maho's native
// content-blocking engine (maho::core::CheckRequest) to EVERY request that
// flows through the intercepted factory — including renderer-initiated
// document subresources (script/image/iframe/xhr/font/...), which the
// navigation-only browser-side throttle never sees.
//
// Start-only enforcement (Phase 1): the decision is made in
// CreateLoaderAndStart and mirrors MahoAdBlockThrottle exactly —
//   * redirect / rewritten_url  -> mutate ResourceRequest.url, forward
//   * blocked                   -> complete client with ERR_BLOCKED_BY_CLIENT
//   * otherwise / no core / import active (fail-open) -> forward unchanged
// No response-side proxying is performed.
class MahoProxyingURLLoaderFactory : public network::mojom::URLLoaderFactory {
 public:
  // Installs the proxy into `factory_builder` when the core is available.
  // `frame` may be null (worker/service-worker contexts): blocked-count
  // attribution is then skipped. Runs on the UI thread.
  static void MaybeProxyRequest(content::RenderFrameHost* frame,
                                network::URLLoaderFactoryBuilder& factory_builder);

  MahoProxyingURLLoaderFactory(
      mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver,
      mojo::PendingRemote<network::mojom::URLLoaderFactory> target,
      base::WeakPtr<MahoAdBlockTabHelper> tab_helper,
      content::GlobalRenderFrameHostId page_id);
  ~MahoProxyingURLLoaderFactory() override;

  MahoProxyingURLLoaderFactory(const MahoProxyingURLLoaderFactory&) = delete;
  MahoProxyingURLLoaderFactory& operator=(const MahoProxyingURLLoaderFactory&) =
      delete;

  // network::mojom::URLLoaderFactory:
  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> loader,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation)
      override;
  void Clone(mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver)
      override;

 private:
  void RecordBlockedRequest(const network::ResourceRequest& request,
                            const char* request_type);
  void OnDisconnect();
  void MaybeDeleteSelf();

  mojo::ReceiverSet<network::mojom::URLLoaderFactory> proxy_receivers_;
  mojo::Remote<network::mojom::URLLoaderFactory> target_factory_;
  base::WeakPtr<MahoAdBlockTabHelper> tab_helper_;
  content::GlobalRenderFrameHostId page_id_;
  base::WeakPtrFactory<MahoProxyingURLLoaderFactory> weak_factory_{this};
};

#endif  // MAHO_BROWSER_NET_MAHO_PROXYING_URL_LOADER_FACTORY_H_
