// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/net/maho_proxying_url_loader_factory.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "content/public/browser/browser_thread.h"
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

namespace {

// Content-blocking mode codes as returned by GetContentBlockingMode
// (maho_core_get_content_blocking_mode): 0 Native, 1 Extension, 2 Disabled,
// 3 Unknown, -1 unavailable. Only Native performs network blocking.
constexpr int kContentBlockingModeNative = 0;

}  // namespace

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
  content::GlobalRenderFrameHostId page_id;
  if (frame) {
    page_id = frame->GetGlobalId();
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
                                    std::move(tab_helper), page_id);
}

MahoProxyingURLLoaderFactory::MahoProxyingURLLoaderFactory(
    mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver,
    mojo::PendingRemote<network::mojom::URLLoaderFactory> target,
    base::WeakPtr<MahoAdBlockTabHelper> tab_helper,
    content::GlobalRenderFrameHostId page_id)
    : tab_helper_(std::move(tab_helper)), page_id_(page_id) {
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
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!maho::GetCore()) {
    target_factory_->CreateLoaderAndStart(std::move(loader), request_id, options,
                                         request, std::move(client),
                                         traffic_annotation);
    return;
  }

  const uint64_t generation = maho::GetCoreGeneration();
  const std::string source = request.request_initiator.has_value()
                                 ? request.request_initiator->GetURL().spec()
                                 : std::string();
  const std::string type = maho::MapRequestToFilterType(request);
  maho::PostCoreTask<std::optional<maho::core::BlockResult>>(
      FROM_HERE,
      base::BindOnce(
          [](uint64_t generation, std::string url, std::string source,
             std::string type) -> std::optional<maho::core::BlockResult> {
            if (generation != maho::GetCoreGeneration()) {
              return std::nullopt;
            }
            MahoCore* core = maho::GetCore();
            if (!core) {
              return std::nullopt;
            }
            if (maho::core::GetContentBlockingMode(core) !=
                kContentBlockingModeNative) {
              return maho::core::BlockResult{};
            }
            return maho::core::CheckRequest(
                core, url.c_str(), source.c_str(), type.c_str());
          },
          generation, request.url.spec(), source, type),
      base::BindOnce(
          [](base::WeakPtr<MahoProxyingURLLoaderFactory> factory,
             uint64_t generation,
             mojo::PendingReceiver<network::mojom::URLLoader> loader,
             int32_t request_id, uint32_t options,
             network::ResourceRequest request,
             mojo::PendingRemote<network::mojom::URLLoaderClient> client,
             net::MutableNetworkTrafficAnnotationTag traffic_annotation,
             std::string type,
             std::optional<maho::core::BlockResult> result) {
            DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
            if (!factory || !result ||
                generation != maho::GetCoreGeneration()) {
              mojo::Remote<network::mojom::URLLoaderClient> client_remote(
                  std::move(client));
              client_remote->OnComplete(
                  network::URLLoaderCompletionStatus(net::ERR_ABORTED));
              return;
            }
            const std::string& replacement = !result->redirect.empty()
                                                 ? result->redirect
                                                 : result->rewritten_url;
            if (!replacement.empty()) {
              GURL new_url(replacement);
              if (new_url.is_valid() && new_url.SchemeIsHTTPOrHTTPS()) {
                factory->RecordBlockedRequest(request, type.c_str());
                request.url = std::move(new_url);
                factory->target_factory_->CreateLoaderAndStart(
                    std::move(loader), request_id, options, request,
                    std::move(client), traffic_annotation);
                return;
              }
              result->blocked = true;
            }
            if (result->blocked) {
              factory->RecordBlockedRequest(request, type.c_str());
              mojo::Remote<network::mojom::URLLoaderClient> client_remote(
                  std::move(client));
              client_remote->OnComplete(network::URLLoaderCompletionStatus(
                  net::ERR_BLOCKED_BY_CLIENT));
              return;
            }
            factory->target_factory_->CreateLoaderAndStart(
                std::move(loader), request_id, options, request,
                std::move(client), traffic_annotation);
          },
          weak_factory_.GetWeakPtr(), generation, std::move(loader), request_id,
          options, request, std::move(client), traffic_annotation, type));
}

void MahoProxyingURLLoaderFactory::RecordBlockedRequest(
    const network::ResourceRequest& request,
    const char* request_type) {
  if (!tab_helper_) {
    return;
  }

  MahoBlockedRequestInfo info;
  info.page_id = page_id_;
  info.destination = request.destination;
  info.is_outermost_main_frame = request.is_outermost_main_frame;
  info.request_type = request_type;
  tab_helper_->RecordBlockedRequest(info);
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
