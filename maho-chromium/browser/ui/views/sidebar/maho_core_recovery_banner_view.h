// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_CORE_RECOVERY_BANNER_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_CORE_RECOVERY_BANNER_VIEW_H_

#include "ui/views/view.h"
#include "maho/browser/ui/views/sidebar/maho_core_recovery_policy.h"
#include "base/memory/callback_list.h"

namespace maho {

class MahoCoreRecoveryBannerView : public views::View {
  METADATA_HEADER(MahoCoreRecoveryBannerView, views::View)

 public:
  MahoCoreRecoveryBannerView();
  MahoCoreRecoveryBannerView(const MahoCoreRecoveryBannerView&) = delete;
  MahoCoreRecoveryBannerView& operator=(const MahoCoreRecoveryBannerView&) =
      delete;
  ~MahoCoreRecoveryBannerView() override;

  void SetState(CoreRecoveryBannerState state);
  void SetDismissed(bool dismissed);

 private:
  CoreRecoveryBannerState state_ = CoreRecoveryBannerState::kHidden;
  bool dismissed_ = false;
  base::CallbackListSubscription state_subscription_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_CORE_RECOVERY_BANNER_VIEW_H_
