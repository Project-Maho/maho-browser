// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_NET_MAHO_COSMETIC_FILTERS_HANDLER_H_
#define MAHO_BROWSER_NET_MAHO_COSMETIC_FILTERS_HANDLER_H_

#include <string>

#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

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

 private:
  friend class content::WebContentsUserData<MahoCosmeticFiltersHandler>;
  explicit MahoCosmeticFiltersHandler(content::WebContents* web_contents);

  void InjectCosmeticFilters(content::RenderFrameHost* render_frame_host,
                             const std::string& url);

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif  // MAHO_BROWSER_NET_MAHO_COSMETIC_FILTERS_HANDLER_H_
