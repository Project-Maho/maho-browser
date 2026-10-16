// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_core_recovery_policy.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace maho {

TEST(MahoCoreRecoveryBannerRetry, RetriesOnceAfterTwoSeconds) {
  const auto retry = DecideCoreStartupRetry(0);
  EXPECT_TRUE(retry.retry);
  EXPECT_EQ(retry.attempt, 1);
  EXPECT_EQ(retry.delay, base::Seconds(2));
  EXPECT_FALSE(DecideCoreStartupRetry(1).retry);
  EXPECT_EQ(DecideCoreStartupRetry(1).attempt, 1);
  EXPECT_FALSE(DecideCoreStartupRetry(2).retry);
}

TEST(MahoCoreRecoveryBannerPolicy, SelectsBannerState) {
  EXPECT_EQ(DecideCoreRecoveryBannerState(true, false, false),
            CoreRecoveryBannerState::kHidden);
  EXPECT_EQ(DecideCoreRecoveryBannerState(true, true, false),
            CoreRecoveryBannerState::kDegraded);
  EXPECT_EQ(DecideCoreRecoveryBannerState(false, false, true),
            CoreRecoveryBannerState::kFailedWithRetry);
}

TEST(MahoCoreRecoveryBannerSecondFailure, ShowsRestartAction) {
  EXPECT_EQ(DecideCoreStartupRetry(1).retry, false);
  const auto state = DecideCoreRecoveryBannerState(false, false, true);
  EXPECT_EQ(state,
            CoreRecoveryBannerState::kFailedWithRetry);
  EXPECT_TRUE(CoreRecoveryBannerHasRestartAction(state));
}

TEST(MahoCoreRecoveryBannerSecondFailure, RestartRemainsUserInitiated) {
  const auto state = DecideCoreRecoveryBannerState(false, false, true);
  EXPECT_TRUE(CoreRecoveryBannerHasRestartAction(state));
  EXPECT_EQ(ApplyCoreRecoveryBannerDismissal(state, true),
            CoreRecoveryBannerState::kHidden);
}

TEST(MahoCoreRecoveryBannerRetrySuccess, HidesBanner) {
  EXPECT_EQ(DecideCoreRecoveryBannerState(true, false, false),
            CoreRecoveryBannerState::kHidden);
}

TEST(MahoCoreRecoveryBannerDegraded, HasNoRestartAction) {
  const auto state = DecideCoreRecoveryBannerState(true, true, false);
  EXPECT_EQ(state,
            CoreRecoveryBannerState::kDegraded);
  EXPECT_FALSE(CoreRecoveryBannerHasRestartAction(state));
}

TEST(MahoCoreRecoveryBannerDismissed, HidesCurrentState) {
  EXPECT_EQ(ApplyCoreRecoveryBannerDismissal(
                CoreRecoveryBannerState::kFailedWithRetry, true),
            CoreRecoveryBannerState::kHidden);
}

TEST(MahoCoreRecoveryBannerHealthy, HidesAfterDegradation) {
  EXPECT_EQ(DecideCoreRecoveryBannerState(true, false, false),
            CoreRecoveryBannerState::kHidden);
}

TEST(MahoCoreRecoveryBannerHealthy, DegradedAfterDismissalCanRecover) {
  EXPECT_EQ(ApplyCoreRecoveryBannerDismissal(
                CoreRecoveryBannerState::kDegraded, true),
            CoreRecoveryBannerState::kHidden);
  EXPECT_EQ(ApplyCoreRecoveryBannerDismissal(
                CoreRecoveryBannerState::kHidden, false),
            CoreRecoveryBannerState::kHidden);
}

TEST(MahoCoreRecoveryBannerSecondFailure, FailedStateIsDismissible) {
  const auto failed = DecideCoreRecoveryBannerState(false, false, true);
  EXPECT_EQ(ApplyCoreRecoveryBannerDismissal(failed, true),
            CoreRecoveryBannerState::kHidden);
  EXPECT_TRUE(CoreRecoveryBannerHasRestartAction(failed));
}

}  // namespace maho
