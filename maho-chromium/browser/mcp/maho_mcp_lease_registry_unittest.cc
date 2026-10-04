// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_lease_registry.h"

#include <string>

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

TEST_F(MahoMcpLeaseRegistryTest, HasActiveLeaseIsHolderAgnostic) {
  EXPECT_FALSE(registry_.HasActiveLease(1));
  auto result = registry_.Acquire(1, "sess-A", base::Seconds(60), false);
  ASSERT_TRUE(result.ok);
  // Held by ANY session: a per-session IsHeldBy("sess-B") fails but the
  // tab still has an active lease, which is what chooser suppression needs.
  EXPECT_TRUE(registry_.HasActiveLease(1));
  EXPECT_FALSE(registry_.IsHeldBy(1, "sess-B"));
  // Expiry sweeps make the tab lease-free again (MOCK_TIME advances).
  task_env_.FastForwardBy(base::Seconds(61));
  EXPECT_FALSE(registry_.HasActiveLease(1));
}

TEST_F(MahoMcpLeaseRegistryTest, SecondSessionBlockedWithoutForceSteal) {
  registry_.Acquire(1, "sess-A", base::Seconds(60), false);
  auto result = registry_.Acquire(1, "sess-B", base::Seconds(60), false);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.previous_holder, "sess-A");
  EXPECT_EQ(result.message, "tab already leased");
  EXPECT_TRUE(registry_.IsHeldBy(1, "sess-A"));
  EXPECT_FALSE(registry_.IsHeldBy(1, "sess-B"));
  for (const auto& entry : registry_.GetAuditLog()) {
    EXPECT_NE(entry.action, "steal");
  }
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
  EXPECT_EQ(result.previous_holder, "sess-A");
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

TEST_F(MahoMcpLeaseRegistryTest, MemoryThreadAuditRingAcrossChurn) {
  // Given: one server-owned registry surviving 10,000 disconnected clients.
  // When: each client acquires a lease, disconnects and expires through the real sweep.
  for (int i = 0; i < 10000; ++i) {
    const std::string session = "mt-" + std::to_string(i);
    ASSERT_TRUE(registry_.Acquire(i, session, base::Seconds(300), false).ok);
    registry_.OnSessionDropped(session);
    task_env_.FastForwardBy(base::Seconds(6));
    ASSERT_FALSE(registry_.HasActiveLease(i));
  }
  // Then: diagnostics retain the newest window, not the server's entire lifetime.
  const auto audit = registry_.GetAuditLog();
  ASSERT_FALSE(audit.empty());
  EXPECT_LE(audit.size(), 4096u);
  EXPECT_EQ(audit.front().tab_id, 7952);
  EXPECT_EQ(audit.back().tab_id, 9999);
  EXPECT_EQ(audit.back().action, "expire");
}

TEST_F(MahoMcpLeaseRegistryTest, MemoryThreadAuditByteBudgetIncludesPreviousHolder) {
  // Given: diagnostic payloads large enough to hit the byte budget before the count cap.
  const std::string owner(1024, 'o');
  const std::string successor(1024, 's');
  // When: force-steal and release retain both current and previous holder identities.
  for (int i = 0; i < 600; ++i) {
    ASSERT_TRUE(registry_.Acquire(i, owner, base::Seconds(300), false).ok);
    ASSERT_TRUE(registry_.Acquire(i, successor, base::Seconds(300), true).ok);
    ASSERT_TRUE(registry_.Release(i, successor));
  }
  // Then: even the payload-only lower bound fits in the resident diagnostic budget.
  const auto audit = registry_.GetAuditLog();
  size_t payload_bytes = 0;
  for (const auto& entry : audit) {
    payload_bytes += entry.session_id.size() + entry.previous.size() + entry.action.size();
  }
  EXPECT_LE(payload_bytes, 1024u * 1024u);
  ASSERT_FALSE(audit.empty());
  EXPECT_EQ(audit.back().tab_id, 599);
  EXPECT_EQ(audit.back().action, "release");
}

TEST_F(MahoMcpLeaseRegistryTest, HybridLeaseEpoch_MonotonicAndNeverReused) {
  // 1. Initial acquisition gets a positive monotonic epoch.
  auto r1 = registry_.Acquire(1, "sess-A", base::Seconds(60), false);
  ASSERT_TRUE(r1.ok);
  const uint64_t epoch1 = r1.lease_epoch;
  EXPECT_GT(epoch1, 0u);
  EXPECT_EQ(registry_.LeaseEpoch(1), epoch1);

  // 2. Same-session reacquire/refresh gets a strictly greater epoch.
  auto r2 = registry_.Acquire(1, "sess-A", base::Seconds(60), false);
  ASSERT_TRUE(r2.ok);
  const uint64_t epoch2 = r2.lease_epoch;
  EXPECT_GT(epoch2, epoch1);
  EXPECT_EQ(registry_.LeaseEpoch(1), epoch2);

  // 3. Force-steal by another session gets a strictly greater epoch.
  auto r3 = registry_.Acquire(1, "sess-B", base::Seconds(60), true);
  ASSERT_TRUE(r3.ok);
  const uint64_t epoch3 = r3.lease_epoch;
  EXPECT_GT(epoch3, epoch2);
  EXPECT_EQ(registry_.LeaseEpoch(1), epoch3);

  // 4. Release invalidates the epoch: LeaseEpoch returns 0.
  EXPECT_TRUE(registry_.Release(1, "sess-B"));
  EXPECT_EQ(registry_.LeaseEpoch(1), 0u);

  // 5. Subsequent acquisition gets a new strictly greater epoch (never reused).
  auto r4 = registry_.Acquire(1, "sess-C", base::Seconds(60), false);
  ASSERT_TRUE(r4.ok);
  const uint64_t epoch4 = r4.lease_epoch;
  EXPECT_GT(epoch4, epoch3);
  EXPECT_EQ(registry_.LeaseEpoch(1), epoch4);
}

TEST_F(MahoMcpLeaseRegistryTest, HybridLeaseEpoch_ExpirySweepsEpoch) {
  auto r = registry_.Acquire(1, "sess-A", base::Seconds(30), false);
  ASSERT_TRUE(r.ok);
  const uint64_t epoch = r.lease_epoch;
  EXPECT_EQ(registry_.LeaseEpoch(1), epoch);

  // Fast-forward past TTL to expire the lease.
  task_env_.FastForwardBy(base::Seconds(31));
  EXPECT_EQ(registry_.LeaseEpoch(1), 0u);
  EXPECT_FALSE(registry_.HasActiveLease(1));

  // The audit log records the expiry with the ended lease epoch.
  const auto audit = registry_.GetAuditLog();
  ASSERT_FALSE(audit.empty());
  EXPECT_EQ(audit.back().action, "expire");
  EXPECT_EQ(audit.back().tab_id, 1);
  EXPECT_EQ(audit.back().lease_epoch, epoch);
}

TEST_F(MahoMcpLeaseRegistryTest, HybridCancelLeaseOnDisconnect_BypassesGracePeriod) {
  auto r = registry_.Acquire(1, "sess-A", base::Seconds(300), false);
  ASSERT_TRUE(r.ok);
  const uint64_t epoch = r.lease_epoch;
  EXPECT_EQ(registry_.LeaseEpoch(1), epoch);

  // Cancel immediately on disconnect: bypasses the 5s grace period.
  EXPECT_TRUE(registry_.CancelLeaseOnDisconnect(1, "sess-A"));
  EXPECT_EQ(registry_.LeaseEpoch(1), 0u);
  EXPECT_FALSE(registry_.HasActiveLease(1));
  EXPECT_FALSE(registry_.IsHeldBy(1, "sess-A"));

  // Audit log records "cancel" action with the ended epoch.
  const auto audit = registry_.GetAuditLog();
  ASSERT_FALSE(audit.empty());
  EXPECT_EQ(audit.back().action, "cancel");
  EXPECT_EQ(audit.back().tab_id, 1);
  EXPECT_EQ(audit.back().session_id, "sess-A");
  EXPECT_EQ(audit.back().lease_epoch, epoch);
}

TEST_F(MahoMcpLeaseRegistryTest, HybridCancelLeaseOnDisconnect_RejectsWrongSessionOrMissing) {
  auto r = registry_.Acquire(1, "sess-A", base::Seconds(300), false);
  ASSERT_TRUE(r.ok);

  // Wrong session cannot cancel.
  EXPECT_FALSE(registry_.CancelLeaseOnDisconnect(1, "sess-B"));
  EXPECT_EQ(registry_.LeaseEpoch(1), r.lease_epoch);

  // Non-existent tab cannot be cancelled.
  EXPECT_FALSE(registry_.CancelLeaseOnDisconnect(99, "sess-A"));
}

TEST_F(MahoMcpLeaseRegistryTest, HybridLeaseEpoch_GracePeriodRetainsEpochUntilExpiry) {
  auto r = registry_.Acquire(1, "sess-A", base::Seconds(300), false);
  ASSERT_TRUE(r.ok);
  const uint64_t epoch = r.lease_epoch;

  registry_.OnSessionDropped("sess-A");
  // Within the 5s grace period, epoch remains valid.
  task_env_.FastForwardBy(base::Seconds(4));
  EXPECT_EQ(registry_.LeaseEpoch(1), epoch);

  // Once grace period expires, epoch becomes 0.
  task_env_.FastForwardBy(base::Seconds(3));
  EXPECT_EQ(registry_.LeaseEpoch(1), 0u);
}

TEST_F(MahoMcpLeaseRegistryTest, TryBorrowAcquiresAndStoresTicket) {
  auto ticket = registry_.TryBorrow(10, "sess-A", 1, 2, base::Seconds(60));
  ASSERT_TRUE(ticket.has_value());
  EXPECT_EQ(ticket->tab_id, 10);
  EXPECT_EQ(ticket->session_id, "sess-A");
  EXPECT_EQ(ticket->origin_space_id, 1);
  EXPECT_EQ(ticket->agent_space_id, 2);
  EXPECT_GT(ticket->epoch, 0u);

  auto retrieved = registry_.GetBorrowTicket(10);
  ASSERT_TRUE(retrieved.has_value());
  EXPECT_EQ(retrieved->session_id, "sess-A");
  EXPECT_EQ(retrieved->origin_space_id, 1);
  EXPECT_EQ(retrieved->agent_space_id, 2);
}

TEST_F(MahoMcpLeaseRegistryTest, TryBorrowRejectsSecondBorrower) {
  auto ticket1 = registry_.TryBorrow(10, "sess-A", 1, 2, base::Seconds(60));
  ASSERT_TRUE(ticket1.has_value());

  auto ticket2 = registry_.TryBorrow(10, "sess-B", 1, 3, base::Seconds(60));
  EXPECT_FALSE(ticket2.has_value());
}

TEST_F(MahoMcpLeaseRegistryTest, ReturnBorrowedRestoresState) {
  auto ticket = registry_.TryBorrow(10, "sess-A", 1, 2, base::Seconds(60));
  ASSERT_TRUE(ticket.has_value());

  // Wrong session cannot return
  EXPECT_FALSE(registry_.ReturnBorrowed(10, "sess-B"));
  EXPECT_TRUE(registry_.GetBorrowTicket(10).has_value());

  // Correct session returns
  EXPECT_TRUE(registry_.ReturnBorrowed(10, "sess-A"));
  EXPECT_FALSE(registry_.GetBorrowTicket(10).has_value());

  // Can be borrowed again
  auto ticket2 = registry_.TryBorrow(10, "sess-B", 1, 3, base::Seconds(60));
  EXPECT_TRUE(ticket2.has_value());
}

TEST_F(MahoMcpLeaseRegistryTest, SessionDropReturnsBorrowedTabs) {
  auto ticket = registry_.TryBorrow(10, "sess-A", 1, 2, base::Seconds(60));
  ASSERT_TRUE(ticket.has_value());

  registry_.OnSessionDropped("sess-A");
  EXPECT_FALSE(registry_.GetBorrowTicket(10).has_value());
}

TEST_F(MahoMcpLeaseRegistryTest, SweepExpiredBorrowsEvicts) {
  auto ticket = registry_.TryBorrow(10, "sess-A", 1, 2, base::Seconds(30));
  ASSERT_TRUE(ticket.has_value());

  task_env_.FastForwardBy(base::Seconds(31));
  EXPECT_FALSE(registry_.GetBorrowTicket(10).has_value());
}

}  // namespace
}  // namespace maho
