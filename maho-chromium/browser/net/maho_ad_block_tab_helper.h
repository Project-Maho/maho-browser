// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_NET_MAHO_AD_BLOCK_TAB_HELPER_H_
#define MAHO_BROWSER_NET_MAHO_AD_BLOCK_TAB_HELPER_H_

#include <string>

#include "base/memory/weak_ptr.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"

struct MahoBlockedRequestInfo {
  content::GlobalRenderFrameHostId page_id;
  network::mojom::RequestDestination destination =
      network::mojom::RequestDestination::kEmpty;
  bool is_outermost_main_frame = false;
  std::string request_type;
};

class MahoAdBlockTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<MahoAdBlockTabHelper> {
 public:
  ~MahoAdBlockTabHelper() override;

  MahoAdBlockTabHelper(const MahoAdBlockTabHelper&) = delete;
  MahoAdBlockTabHelper& operator=(const MahoAdBlockTabHelper&) = delete;

  uint32_t blocked_count() const { return blocked_count_; }
  void RecordBlockedRequest(const MahoBlockedRequestInfo& info);
  base::WeakPtr<MahoAdBlockTabHelper> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;

 private:
  friend class content::WebContentsUserData<MahoAdBlockTabHelper>;
  explicit MahoAdBlockTabHelper(content::WebContents* web_contents);

  content::GlobalRenderFrameHostId current_primary_page_id_;
  uint32_t blocked_count_ = 0;
  base::WeakPtrFactory<MahoAdBlockTabHelper> weak_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif  // MAHO_BROWSER_NET_MAHO_AD_BLOCK_TAB_HELPER_H_
