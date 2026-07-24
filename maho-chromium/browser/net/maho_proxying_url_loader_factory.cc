// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_proxying_url_loader_factory.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "net/base/net_errors.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/cpp/url_loader_factory_builder.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "url/gurl.h"

#include "maho/browser/maho_core_holder.h"
#include "maho/browser/net/maho_ad_block_request_util.h"
#include "maho/browser/net/maho_ad_block_tab_helper.h"
#include "maho/third_party/maho/maho_bridge.h"

// static
void MahoProxyingURLLoaderFactory::MaybeProxyRequest(
    content::RenderFrameHost* frame,
    network::URLLoaderFactoryBuilder& factory_builder) {
  // Fail-open: install no interceptor when the core is unavailable, matching
  // the throttle's early return so page loads never break.
  if (!maho::GetCore()) {
    return;
  }

  base::WeakPtr<MahoAdBlockTabHelper> tab_helper;
  if (frame) {
    if (content::WebContents* wc =
            content::WebContents::FromRenderFrameHost(frame)) {
      MahoAdBlockTabHelper::CreateForWebContents(wc);
      if (MahoAdBlockTabHelper* helper =
              MahoAdBlockTabHelper::FromWebContents(wc)) {
        tab_helper = helper->GetWeakPtr();
      }
    }
  }

  auto [receiver, target] = factory_builder.Append();
  // Self-owned: deletes itself once all proxy receivers and the target
  // factory disconnect (see MaybeDeleteThis).
  new MahoProxyingURLLoaderFactory(std::move(receiver), std::move(target),
                                   std::move(tab_helper));
}

MahoProxyingURLLoaderFactory::MahoProxyingURLLoaderFactory(
    mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver,
    mojo::PendingRemote<network::mojom::URLLoaderFactory> target,
    base::WeakPtr<MahoAdBlockTabHelper> tab_helper)
    : tab_helper_(std::move(tab_helper)) {
  proxy_receivers_.Add(this, std::move(receiver));
  target_factory_.Bind(std::move(target));
  proxy_receivers_.set_disconnect_handler(base::BindRepeating(
      &MahoProxyingURLLoaderFactory::OnDisconnect, base::Unretained(this)));
  target_factory_.set_disconnect_handler(base::BindOnce(
      &MahoProxyingURLLoaderFactory::OnDisconnect, base::Unretained(this)));
}

MahoProxyingURLLoaderFactory::~MahoProxyingURLLoaderFactory() = default;

void MahoProxyingURLLoaderFactory::CreateLoaderAndStart(
    mojo::PendingReceiver<network::mojom::URLLoader> loader,
    int32_t request_id,
    uint32_t options,
    const network::ResourceRequest& request,
    mojo::PendingRemote<network::mojom::URLLoaderClient> client,
    const net::MutableNetworkTrafficAnnotationTag& traffic_annotation) {
  MahoCore* core = maho::GetCore();
  if (core) {
    const std::string url = request.url.spec();
    const std::string source =
        request.request_initiator.has_value()
            ? request.request_initiator->GetURL().spec()
            : std::string();
    const char* type = maho::MapRequestToFilterType(request);
    // CheckRequest applies mode (native/extension/disabled) and site
    // exceptions internally; non-native modes return a no-block result.
    maho::core::BlockResult result =
        maho::core::CheckRequest(core, url.c_str(), source.c_str(), type);

    const std::string& replacement =
        !result.redirect.empty() ? result.redirect : result.rewritten_url;
    if (!replacement.empty()) {
      GURL new_url(replacement);
      if (new_url.is_valid() && new_url.SchemeIsHTTPOrHTTPS()) {
        network::ResourceRequest new_request = request;
        new_request.url = new_url;
        if (tab_helper_) {
          tab_helper_->IncrementBlockedCount();
        }
        target_factory_->CreateLoaderAndStart(std::move(loader), request_id,
                                              options, new_request,
                                              std::move(client),
                                              traffic_annotation);
        return;
      }
      // A non-network (e.g. data:) redirect/rewrite target cannot be served by
      // the terminal network factory, so block the original request instead of
      // forwarding an unsupported URL.
      result.blocked = true;
    }
    if (result.blocked) {
      if (tab_helper_) {
        tab_helper_->IncrementBlockedCount();
      }
      // Complete the client with ERR_BLOCKED_BY_CLIENT and drop the loader
      // receiver so the request is cancelled before hitting the network. The
      // OnComplete message is written to the pipe synchronously, so it is
      // delivered even though `client_remote` is destroyed on return.
      mojo::Remote<network::mojom::URLLoaderClient> client_remote(
          std::move(client));
      client_remote->OnComplete(
          network::URLLoaderCompletionStatus(net::ERR_BLOCKED_BY_CLIENT));
      return;
    }
  }

  // Allowed (or fail-open when core is unavailable): forward unchanged.
  target_factory_->CreateLoaderAndStart(std::move(loader), request_id, options,
                                        request, std::move(client),
                                        traffic_annotation);
}

void MahoProxyingURLLoaderFactory::Clone(
    mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver) {
  proxy_receivers_.Add(this, std::move(receiver));
}

void MahoProxyingURLLoaderFactory::OnDisconnect() {
  MaybeDeleteSelf();
}

void MahoProxyingURLLoaderFactory::MaybeDeleteSelf() {
  // Once no inbound receivers remain or the terminal factory is gone, this
  // proxy can no longer serve requests; tear it down.
  if (!proxy_receivers_.empty() && target_factory_.is_connected()) {
    return;
  }
  delete this;
}
