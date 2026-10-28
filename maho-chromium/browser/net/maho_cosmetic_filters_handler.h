// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_NET_MAHO_COSMETIC_FILTERS_HANDLER_H_
#define MAHO_BROWSER_NET_MAHO_COSMETIC_FILTERS_HANDLER_H_

#include <map>
#include <memory>
#include <string>

#include "base/memory/weak_ptr.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "maho/browser/net/maho_cosmetic_class_id_poller.h"

class MahoCosmeticFiltersHandler
    : public content::WebContentsObserver,
      public content::WebContentsUserData<MahoCosmeticFiltersHandler> {
 public:
  ~MahoCosmeticFiltersHandler() override;

  MahoCosmeticFiltersHandler(const MahoCosmeticFiltersHandler&) = delete;
  MahoCosmeticFiltersHandler& operator=(const MahoCosmeticFiltersHandler&) =
      delete;

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void RenderFrameDeleted(content::RenderFrameHost* render_frame_host) override;
  void PrimaryPageChanged(content::Page& page) override;

 private:
  friend class content::WebContentsUserData<MahoCosmeticFiltersHandler>;
  explicit MahoCosmeticFiltersHandler(content::WebContents* web_contents);

  void InjectCosmeticFilters(content::RenderFrameHost* render_frame_host,
                             const std::string& url);
  void StopPoller(content::GlobalRenderFrameHostId frame_id);
  std::map<content::GlobalRenderFrameHostId,
           std::unique_ptr<maho::CosmeticClassIdPoller>> pollers_;
  base::WeakPtrFactory<MahoCosmeticFiltersHandler> weak_factory_{this};
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif  // MAHO_BROWSER_NET_MAHO_COSMETIC_FILTERS_HANDLER_H_
