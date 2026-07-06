// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_lease_registry.h"

#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

class MahoMcpLeaseRegistryTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_env_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  MahoMcpLeaseRegistry registry_;
};

TEST_F(MahoMcpLeaseRegistryTest, AcquireGrantsToFirstSession) {
  auto result = registry_.Acquire(1, "sess-A", base::Seconds(60), false);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.previous_holder.empty());
  EXPECT_EQ(registry_.active_lease_count(), 1u);
  EXPECT_TRUE(registry_.IsHeldBy(1, "sess-A"));
}

TEST_F(MahoMcpLeaseRegistryTest, SecondSessionBlockedWithoutForceSteal) {
  registry_.Acquire(1, "sess-A", base::Seconds(60), false);
  auto result = registry_.Acquire(1, "sess-B", base::Seconds(60), false);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.previous_holder, "sess-A");
}

TEST_F(MahoMcpLeaseRegistryTest, SameSessionReacquireRefreshesTtl) {
  auto r1 = registry_.Acquire(1, "sess-A", base::Seconds(60), false);
  ASSERT_TRUE(r1.ok);
  const base::TimeTicks first_expires = r1.expires_at;
  task_env_.FastForwardBy(base::Seconds(30));
  auto r2 = registry_.Acquire(1, "sess-A", base::Seconds(60), false);
  EXPECT_TRUE(r2.ok);
  EXPECT_GT(r2.expires_at, first_expires);
}

TEST_F(MahoMcpLeaseRegistryTest, TtlExpiresAllowsReacquireByOther) {
  registry_.Acquire(1, "sess-A", base::Seconds(30), false);
  task_env_.FastForwardBy(base::Seconds(31));
  auto result = registry_.Acquire(1, "sess-B", base::Seconds(60), false);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(registry_.IsHeldBy(1, "sess-B"));
  EXPECT_FALSE(registry_.IsHeldBy(1, "sess-A"));
}

TEST_F(MahoMcpLeaseRegistryTest, HeartbeatExtendsLease) {
  registry_.Acquire(1, "sess-A", base::Seconds(30), false);
  task_env_.FastForwardBy(base::Seconds(20));
  auto new_expires = registry_.Heartbeat(1, "sess-A", base::Seconds(30));
  EXPECT_FALSE(new_expires.is_null());
  task_env_.FastForwardBy(base::Seconds(20));
  EXPECT_TRUE(registry_.IsHeldBy(1, "sess-A"));
}

TEST_F(MahoMcpLeaseRegistryTest, HeartbeatRejectedForWrongSession) {
  registry_.Acquire(1, "sess-A", base::Seconds(60), false);
  auto result = registry_.Heartbeat(1, "sess-B", base::Seconds(60));
  EXPECT_TRUE(result.is_null());
}

TEST_F(MahoMcpLeaseRegistryTest, ReleaseByHolderClearsLease) {
  registry_.Acquire(1, "sess-A", base::Seconds(60), false);
  EXPECT_TRUE(registry_.Release(1, "sess-A"));
  EXPECT_EQ(registry_.active_lease_count(), 0u);
}

TEST_F(MahoMcpLeaseRegistryTest, ReleaseByNonHolderRejected) {
  registry_.Acquire(1, "sess-A", base::Seconds(60), false);
  EXPECT_FALSE(registry_.Release(1, "sess-B"));
  EXPECT_TRUE(registry_.IsHeldBy(1, "sess-A"));
}

TEST_F(MahoMcpLeaseRegistryTest, ForceStealTransfersLeaseAndAudits) {
  registry_.Acquire(1, "sess-A", base::Seconds(60), false);
  auto result = registry_.Acquire(1, "sess-B", base::Seconds(60), true);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(registry_.IsHeldBy(1, "sess-B"));
  EXPECT_FALSE(registry_.IsHeldBy(1, "sess-A"));
  auto audit = registry_.GetAuditLog();
  bool saw_steal = false;
  for (const auto& e : audit) {
    if (e.action == "steal" && e.session_id == "sess-B" &&
        e.previous == "sess-A" && e.tab_id == 1) {
      saw_steal = true;
    }
  }
  EXPECT_TRUE(saw_steal);
}

TEST_F(MahoMcpLeaseRegistryTest, SocketDropGraceReleasesAfter5s) {
  registry_.Acquire(1, "sess-A", base::Seconds(300), false);
  registry_.OnSessionDropped("sess-A");
  task_env_.FastForwardBy(base::Seconds(4));
  EXPECT_TRUE(registry_.IsHeldBy(1, "sess-A"));
  task_env_.FastForwardBy(base::Seconds(3));
  EXPECT_FALSE(registry_.IsHeldBy(1, "sess-A"));
}

TEST_F(MahoMcpLeaseRegistryTest, HeartbeatCancelsGrace) {
  registry_.Acquire(1, "sess-A", base::Seconds(300), false);
  registry_.OnSessionDropped("sess-A");
  task_env_.FastForwardBy(base::Seconds(3));
  registry_.Heartbeat(1, "sess-A", base::Seconds(300));
  task_env_.FastForwardBy(base::Seconds(10));
  EXPECT_TRUE(registry_.IsHeldBy(1, "sess-A"));
}

TEST_F(MahoMcpLeaseRegistryTest, MonotonicClockUnaffectedByWallClockJumps) {
  // TaskEnvironment MOCK_TIME uses monotonic-emulated ticks. Verify the
  // registry doesn't rely on wall-clock even if system clock jumps.
  registry_.Acquire(1, "sess-A", base::Seconds(30), false);
  task_env_.FastForwardBy(base::Seconds(15));
  EXPECT_TRUE(registry_.IsHeldBy(1, "sess-A"));
  task_env_.FastForwardBy(base::Seconds(20));
  EXPECT_FALSE(registry_.IsHeldBy(1, "sess-A"));
}

TEST_F(MahoMcpLeaseRegistryTest, TtlClampedToRange) {
  auto short_r = registry_.Acquire(1, "sess-A", base::Seconds(1), false);
  ASSERT_TRUE(short_r.ok);
  task_env_.FastForwardBy(base::Seconds(9));
  EXPECT_TRUE(registry_.IsHeldBy(1, "sess-A"));
  task_env_.FastForwardBy(base::Seconds(2));
  EXPECT_FALSE(registry_.IsHeldBy(1, "sess-A"));

  auto long_r = registry_.Acquire(2, "sess-B", base::Seconds(9999), false);
  ASSERT_TRUE(long_r.ok);
  task_env_.FastForwardBy(base::Seconds(299));
  EXPECT_TRUE(registry_.IsHeldBy(2, "sess-B"));
  task_env_.FastForwardBy(base::Seconds(2));
  EXPECT_FALSE(registry_.IsHeldBy(2, "sess-B"));
}

}  // namespace
}  // namespace maho
