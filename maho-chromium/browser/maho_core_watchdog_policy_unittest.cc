// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_core_watchdog_policy.h"

#include <vector>

#include "base/functional/bind.h"
#include "maho/browser/maho_core_holder.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

const base::TimeTicks kStart = base::TimeTicks() + base::Hours(1);

TEST(MahoCoreWatchdogPolicyTest, DeadlinesDependOnOperation) {
  EXPECT_EQ(CoreOperationDeadline(CoreOperationClass::kInteractive),
            base::Seconds(30));
  EXPECT_EQ(CoreOperationDeadline(CoreOperationClass::kStartup),
            base::Seconds(120));
  EXPECT_EQ(CoreOperationDeadline(CoreOperationClass::kLongIo), std::nullopt);
}

TEST(MahoCoreWatchdogPolicyTest, OnTimeTaskEmitsNothing) {
  CoreWatchdogPolicy policy;
  const uint64_t id =
      policy.OnTaskStarted(CoreOperationClass::kInteractive, kStart);
  EXPECT_EQ(policy.Evaluate(kStart + base::Seconds(30)), std::nullopt);
  policy.OnTaskFinished(id);
  EXPECT_EQ(policy.Evaluate(kStart + base::Minutes(10)), std::nullopt);
  EXPECT_EQ(policy.health(), CoreHealth::kHealthy);
}

TEST(MahoCoreWatchdogPolicyTest, OverdueTaskDegradesOnceThenRecovers) {
  CoreWatchdogPolicy policy;
  const uint64_t id =
      policy.OnTaskStarted(CoreOperationClass::kInteractive, kStart);
  EXPECT_EQ(policy.Evaluate(kStart + base::Seconds(31)), CoreHealth::kDegraded);
  EXPECT_EQ(policy.Evaluate(kStart + base::Seconds(60)), std::nullopt)
      << "one event per transition";
  EXPECT_EQ(policy.in_flight(), 1u) << "the policy never cancels the task";

  policy.OnTaskFinished(id);
  EXPECT_EQ(policy.Evaluate(kStart + base::Seconds(61)), CoreHealth::kHealthy);
}

TEST(MahoCoreWatchdogPolicyTest, StartupGetsTheLongerDeadline) {
  CoreWatchdogPolicy policy;
  policy.OnTaskStarted(CoreOperationClass::kStartup, kStart);
  EXPECT_EQ(policy.Evaluate(kStart + base::Seconds(119)), std::nullopt);
  EXPECT_EQ(policy.Evaluate(kStart + base::Seconds(121)),
            CoreHealth::kDegraded);
}

TEST(MahoCoreWatchdogPolicyTest, LongIoIsNeverReportedAsHung) {
  CoreWatchdogPolicy policy;
  policy.OnTaskStarted(CoreOperationClass::kLongIo, kStart);
  EXPECT_EQ(policy.Evaluate(kStart + base::Hours(2)), std::nullopt);
  EXPECT_EQ(policy.health(), CoreHealth::kHealthy);
}

TEST(MahoCoreWatchdogPolicyTest, StaysDegradedWhileAnyTaskIsOverdue) {
  CoreWatchdogPolicy policy;
  const uint64_t first =
      policy.OnTaskStarted(CoreOperationClass::kInteractive, kStart);
  const uint64_t second = policy.OnTaskStarted(
      CoreOperationClass::kInteractive, kStart + base::Seconds(5));
  EXPECT_EQ(policy.Evaluate(kStart + base::Seconds(40)), CoreHealth::kDegraded);
  policy.OnTaskFinished(first);
  EXPECT_EQ(policy.Evaluate(kStart + base::Seconds(41)), std::nullopt);
  policy.OnTaskFinished(second);
  EXPECT_EQ(policy.Evaluate(kStart + base::Seconds(42)), CoreHealth::kHealthy);
  policy.OnTaskFinished(12345);
  EXPECT_EQ(policy.in_flight(), 0u);
}

TEST(MahoCoreHolderWatchdogTest, NotifiesSubscribersOncePerTransition) {
  std::vector<CoreHealth> seen;
  auto subscription = AddCoreHealthCallback(base::BindRepeating(
      [](std::vector<CoreHealth>* seen, CoreHealth health) {
        seen->push_back(health);
      },
      &seen));
  const base::TimeTicks start = base::TimeTicks::Now();
  const uint64_t id = BeginWatchedCoreTask(CoreOperationClass::kInteractive);
  EvaluateCoreWatchdog(start + base::Minutes(2));
  EvaluateCoreWatchdog(start + base::Minutes(3));
  EndWatchedCoreTask(id);
  EvaluateCoreWatchdog(start + base::Minutes(4));
  EXPECT_EQ(seen,
            (std::vector<CoreHealth>{CoreHealth::kDegraded, CoreHealth::kHealthy}));
}

}  // namespace
}  // namespace maho
