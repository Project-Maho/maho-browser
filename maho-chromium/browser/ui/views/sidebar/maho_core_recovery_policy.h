// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_CORE_RECOVERY_POLICY_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_CORE_RECOVERY_POLICY_H_

#include "base/time/time.h"
#include "base/functional/callback_list.h"

namespace maho {

enum class CoreRecoveryBannerState { kHidden, kDegraded, kFailedWithRetry };

struct CoreStartupRetryDecision {
  int attempt = 0;
  base::TimeDelta delay;
  bool retry = false;
};

CoreStartupRetryDecision DecideCoreStartupRetry(int completed_attempts);
CoreRecoveryBannerState DecideCoreRecoveryBannerState(bool core_available,
                                                       bool degraded,
                                                       bool startup_failed);
CoreRecoveryBannerState ApplyCoreRecoveryBannerDismissal(
    CoreRecoveryBannerState state,
    bool dismissed);
bool CoreRecoveryBannerHasRestartAction(CoreRecoveryBannerState state);

using CoreRecoveryStateCallbackList =
    base::RepeatingCallbackList<void(CoreRecoveryBannerState state)>;
base::CallbackListSubscription AddCoreRecoveryStateCallback(
    base::RepeatingCallback<void(CoreRecoveryBannerState state)> callback);
void PublishCoreRecoveryState(CoreRecoveryBannerState state);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_CORE_RECOVERY_POLICY_H_
