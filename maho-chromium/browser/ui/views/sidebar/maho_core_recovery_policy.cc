// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_core_recovery_policy.h"

#include "base/no_destructor.h"

namespace maho {

namespace {
CoreRecoveryStateCallbackList& GetCoreRecoveryStateCallbacks() {
  static base::NoDestructor<CoreRecoveryStateCallbackList> callbacks;
  return *callbacks;
}
}  // namespace

base::CallbackListSubscription AddCoreRecoveryStateCallback(
    base::RepeatingCallback<void(CoreRecoveryBannerState state)> callback) {
  return GetCoreRecoveryStateCallbacks().Add(std::move(callback));
}

void PublishCoreRecoveryState(CoreRecoveryBannerState state) {
  GetCoreRecoveryStateCallbacks().Notify(state);
}

CoreStartupRetryDecision DecideCoreStartupRetry(int completed_attempts) {
  if (completed_attempts < 0 || completed_attempts >= 1) {
    return {completed_attempts, base::TimeDelta(), false};
  }
  return {completed_attempts + 1, base::Seconds(2), true};
}

CoreRecoveryBannerState DecideCoreRecoveryBannerState(bool core_available,
                                                       bool degraded,
                                                       bool startup_failed) {
  if (core_available && !degraded) {
    return CoreRecoveryBannerState::kHidden;
  }
  if (degraded) {
    return CoreRecoveryBannerState::kDegraded;
  }
  if (startup_failed) {
    return CoreRecoveryBannerState::kFailedWithRetry;
  }
  return CoreRecoveryBannerState::kHidden;
}

CoreRecoveryBannerState ApplyCoreRecoveryBannerDismissal(
    CoreRecoveryBannerState state,
    bool dismissed) {
  return dismissed ? CoreRecoveryBannerState::kHidden : state;
}

bool CoreRecoveryBannerHasRestartAction(CoreRecoveryBannerState state) {
  return state == CoreRecoveryBannerState::kFailedWithRetry;
}

}  // namespace maho
