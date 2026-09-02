// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_agent_event_replay_buffer.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#ifndef MAHO_STANDALONE_TEST
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::ai {

TEST(MahoAgentEventReplayBufferTest, AppendThreeAndReplayAfterOne) {
  MahoAgentEventReplayBuffer buffer(10);
  EXPECT_TRUE(buffer.Append(1, "{\"event\":\"start\"}"));
  EXPECT_TRUE(buffer.Append(2, "{\"event\":\"step_1\"}"));
  EXPECT_TRUE(buffer.Append(3, "{\"event\":\"step_2\"}"));

  ReplayResult res = buffer.ReplayAfter(1);
  EXPECT_TRUE(res.is_ok());
  EXPECT_FALSE(res.is_gap());

  const auto& events = res.events();
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].seq, 2u);
  EXPECT_EQ(events[0].payload, "{\"event\":\"step_1\"}");
  EXPECT_EQ(events[1].seq, 3u);
  EXPECT_EQ(events[1].payload, "{\"event\":\"step_2\"}");
}

TEST(MahoAgentEventReplayBufferTest, OverflowCapacityReturnsReplayGap) {
  // Bounded capacity of 2
  MahoAgentEventReplayBuffer buffer(2);
  EXPECT_TRUE(buffer.Append(1, "e1"));
  EXPECT_TRUE(buffer.Append(2, "e2"));
  EXPECT_TRUE(buffer.Append(3, "e3"));  // seq 1 is evicted, oldest is now 2

  EXPECT_EQ(buffer.size(), 2u);
  EXPECT_EQ(buffer.oldest_seq(), std::optional<uint64_t>(2u));
  EXPECT_EQ(buffer.newest_seq(), std::optional<uint64_t>(3u));

  // Requesting events after 0 (which needs seq 1) MUST return explicit ReplayGap
  ReplayResult gap_res = buffer.ReplayAfter(0);
  EXPECT_TRUE(gap_res.is_gap());
  EXPECT_FALSE(gap_res.is_ok());
  EXPECT_EQ(gap_res.oldest_retained_seq(), 2u);
  EXPECT_EQ(gap_res.gap().oldest_retained_seq, 2u);

  // Requesting after 1 (which needs seq 2, 3) should succeed with [2, 3]
  ReplayResult ok_res = buffer.ReplayAfter(1);
  EXPECT_TRUE(ok_res.is_ok());
  ASSERT_EQ(ok_res.events().size(), 2u);
  EXPECT_EQ(ok_res.events()[0].seq, 2u);
  EXPECT_EQ(ok_res.events()[1].seq, 3u);
}

TEST(MahoAgentEventReplayBufferTest, MonotonicSequenceEnforcementAndRejection) {
  MahoAgentEventReplayBuffer buffer(10);
  // Initial append
  EXPECT_TRUE(buffer.Append(10, "payload_10"));

  // Duplicate sequence rejected
  EXPECT_FALSE(buffer.Append(10, "payload_10_duplicate"));

  // Out-of-order sequence rejected (seq < 10)
  EXPECT_FALSE(buffer.Append(5, "payload_5_out_of_order"));
  EXPECT_FALSE(buffer.Append(9, "payload_9_out_of_order"));
  EXPECT_FALSE(buffer.Append(0, "payload_0_out_of_order"));

  EXPECT_EQ(buffer.size(), 1u);
  EXPECT_EQ(buffer.newest_seq(), std::optional<uint64_t>(10u));

  // Monotonically higher sequence accepted
  EXPECT_TRUE(buffer.Append(15, "payload_15"));
  EXPECT_EQ(buffer.size(), 2u);

  // Older than highest (15) rejected
  EXPECT_FALSE(buffer.Append(12, "payload_12_out_of_order"));
  EXPECT_FALSE(buffer.Append(15, "payload_15_duplicate"));

  // Append higher
  EXPECT_TRUE(buffer.Append(20, "payload_20"));
  EXPECT_EQ(buffer.size(), 3u);
}

TEST(MahoAgentEventReplayBufferTest, BoundedMemoryWithLargeStream) {
  constexpr size_t kBoundedCap = 50;
  MahoAgentEventReplayBuffer buffer(kBoundedCap);
  EXPECT_EQ(buffer.capacity(), kBoundedCap);

  // Push 10,000 monotonic events
  for (uint64_t i = 1; i <= 10000; ++i) {
    EXPECT_TRUE(buffer.Append(i, "data_" + std::to_string(i)));
    // Size never exceeds capacity
    EXPECT_LE(buffer.size(), kBoundedCap);
  }

  EXPECT_EQ(buffer.size(), kBoundedCap);
  EXPECT_EQ(buffer.oldest_seq(), std::optional<uint64_t>(10000 - kBoundedCap + 1));
  EXPECT_EQ(buffer.newest_seq(), std::optional<uint64_t>(10000u));

  // Sequence before oldest retained gives ReplayGap
  ReplayResult gap_res = buffer.ReplayAfter(10000 - kBoundedCap - 1);
  EXPECT_TRUE(gap_res.is_gap());
  EXPECT_EQ(gap_res.oldest_retained_seq(), 10000 - kBoundedCap + 1);

  // Sequence at boundary gives full retained
  ReplayResult ok_res = buffer.ReplayAfter(10000 - kBoundedCap);
  EXPECT_TRUE(ok_res.is_ok());
  EXPECT_EQ(ok_res.events().size(), kBoundedCap);
}

TEST(MahoAgentEventReplayBufferTest, ClearResetsSequenceTracking) {
  MahoAgentEventReplayBuffer buffer(10);
  EXPECT_TRUE(buffer.Append(50, "p50"));
  EXPECT_FALSE(buffer.Append(25, "p25"));

  buffer.Clear();
  EXPECT_TRUE(buffer.empty());
  EXPECT_EQ(buffer.size(), 0u);
  EXPECT_EQ(buffer.oldest_seq(), std::nullopt);
  EXPECT_EQ(buffer.newest_seq(), std::nullopt);

  // After clear, sequence tracking is reset so seq 25 can be appended
  EXPECT_TRUE(buffer.Append(25, "p25_new"));
  EXPECT_EQ(buffer.size(), 1u);
  EXPECT_EQ(buffer.newest_seq(), std::optional<uint64_t>(25u));
}

TEST(MahoAgentEventReplayBufferTest, DefaultCapacityLargeStreamAndGaps) {
  MahoAgentEventReplayBuffer buffer;  // default capacity 512
  EXPECT_EQ(buffer.capacity(), 512u);

  for (uint64_t i = 1; i <= 1000; ++i) {
    EXPECT_TRUE(buffer.Append(i, "ev_" + std::to_string(i)));
  }

  EXPECT_EQ(buffer.size(), 512u);
  EXPECT_EQ(buffer.oldest_seq(), std::optional<uint64_t>(1000 - 512 + 1));  // 489
  EXPECT_EQ(buffer.newest_seq(), std::optional<uint64_t>(1000u));

  // Sequence 488 (oldest - 1) gives all retained events
  ReplayResult full_retained = buffer.ReplayAfter(488);
  EXPECT_TRUE(full_retained.is_ok());
  EXPECT_EQ(full_retained.events().size(), 512u);
  EXPECT_EQ(full_retained.events().front().seq, 489u);
  EXPECT_EQ(full_retained.events().back().seq, 1000u);

  // Sequence 487 (oldest - 2) was evicted -> explicit ReplayGap
  ReplayResult gap = buffer.ReplayAfter(487);
  EXPECT_TRUE(gap.is_gap());
  EXPECT_EQ(gap.oldest_retained_seq(), 489u);

  // Sequence 1000 (newest) -> returns empty Ok
  ReplayResult at_tail = buffer.ReplayAfter(1000);
  EXPECT_TRUE(at_tail.is_ok());
  EXPECT_TRUE(at_tail.events().empty());
}

// Plan row 6: FFI kind-8 (InteractionRequest) payloads replay through the
// same kind-agnostic sequenced path as every other event kind — byte-for-byte
// payload fidelity, no special-casing, no gap.
TEST(MahoAgentEventReplayBufferTest, InteractionRequestKind8PayloadsReplayLikeOtherKinds) {
  MahoAgentEventReplayBuffer buffer(10);
  const char* kInteractionPayload =
      "{\"request_id\":\"interaction-req-3\",\"kind\":\"question\","
      "\"args\":\"{\\\"question\\\":\\\"Which target?\\\","
      "\\\"options\\\":[]\"}\"}";

  EXPECT_TRUE(buffer.Append(1, "{\"event\":\"token\"}"));
  EXPECT_TRUE(buffer.Append(2, kInteractionPayload));
  EXPECT_TRUE(buffer.Append(3, "{\"event\":\"complete\"}"));

  ReplayResult res = buffer.ReplayAfter(0);
  EXPECT_TRUE(res.is_ok());
  EXPECT_FALSE(res.is_gap());
  ASSERT_EQ(res.events().size(), 3u);
  EXPECT_EQ(res.events()[1].seq, 2u);
  EXPECT_EQ(res.events()[1].payload, kInteractionPayload);

  // Resume from the kind-8 event: only the tail replays.
  ReplayResult tail = buffer.ReplayAfter(2);
  EXPECT_TRUE(tail.is_ok());
  ASSERT_EQ(tail.events().size(), 1u);
  EXPECT_EQ(tail.events()[0].seq, 3u);
}

}  // namespace maho::ai

#else

int main() {
  using namespace maho::ai;
  std::cout << "[RUN] MahoAgentEventReplayBuffer standalone tests..." << std::endl;

  // Test 1: Append 3, replay after 1 -> [2, 3]
  {
    MahoAgentEventReplayBuffer buffer(10);
    assert(buffer.Append(1, "{\"event\":\"start\"}"));
    assert(buffer.Append(2, "{\"event\":\"step_1\"}"));
    assert(buffer.Append(3, "{\"event\":\"step_2\"}"));

    ReplayResult res = buffer.ReplayAfter(1);
    assert(res.is_ok());
    assert(!res.is_gap());
    assert(res.events().size() == 2);
    assert(res.events()[0].seq == 2);
    assert(res.events()[1].seq == 3);
  }

  // Test 2: Overflow capacity -> replay after 0 -> ReplayGap
  {
    MahoAgentEventReplayBuffer buffer(2);
    assert(buffer.Append(1, "e1"));
    assert(buffer.Append(2, "e2"));
    assert(buffer.Append(3, "e3"));

    assert(buffer.size() == 2);
    assert(buffer.oldest_seq() == std::optional<uint64_t>(2));

    ReplayResult gap_res = buffer.ReplayAfter(0);
    assert(gap_res.is_gap());
    assert(!gap_res.is_ok());
    assert(gap_res.oldest_retained_seq() == 2);

    ReplayResult ok_res = buffer.ReplayAfter(1);
    assert(ok_res.is_ok());
    assert(ok_res.events().size() == 2);
    assert(ok_res.events()[0].seq == 2);
    assert(ok_res.events()[1].seq == 3);
  }

  // Test 3: Monotonic sequence enforcement and out-of-order rejection
  {
    MahoAgentEventReplayBuffer buffer(10);
    assert(buffer.Append(10, "payload_10"));

    // Duplicate sequence rejected
    assert(!buffer.Append(10, "payload_10_duplicate"));

    // Out-of-order sequences rejected
    assert(!buffer.Append(5, "payload_5_out_of_order"));
    assert(!buffer.Append(9, "payload_9_out_of_order"));
    assert(!buffer.Append(0, "payload_0_out_of_order"));

    assert(buffer.size() == 1);
    assert(buffer.newest_seq() == std::optional<uint64_t>(10));

    // Monotonically higher sequence accepted
    assert(buffer.Append(15, "payload_15"));
    assert(buffer.size() == 2);

    // Older than highest rejected
    assert(!buffer.Append(12, "payload_12_out_of_order"));
    assert(!buffer.Append(15, "payload_15_duplicate"));

    assert(buffer.Append(20, "payload_20"));
    assert(buffer.size() == 3);
  }

  // Test 4: Bounded memory with large stream
  {
    constexpr size_t kBoundedCap = 50;
    MahoAgentEventReplayBuffer buffer(kBoundedCap);
    assert(buffer.capacity() == kBoundedCap);

    for (uint64_t i = 1; i <= 10000; ++i) {
      assert(buffer.Append(i, "data_" + std::to_string(i)));
      assert(buffer.size() <= kBoundedCap);
    }

    assert(buffer.size() == kBoundedCap);
    assert(buffer.oldest_seq() == std::optional<uint64_t>(10000 - kBoundedCap + 1));
    assert(buffer.newest_seq() == std::optional<uint64_t>(10000));

    ReplayResult gap_res = buffer.ReplayAfter(10000 - kBoundedCap - 1);
    assert(gap_res.is_gap());
    assert(gap_res.oldest_retained_seq() == 10000 - kBoundedCap + 1);

    ReplayResult ok_res = buffer.ReplayAfter(10000 - kBoundedCap);
    assert(ok_res.is_ok());
    assert(ok_res.events().size() == kBoundedCap);
  }

  // Test 5: Clear resets sequence tracking
  {
    MahoAgentEventReplayBuffer buffer(10);
    assert(buffer.Append(50, "p50"));
    assert(!buffer.Append(25, "p25"));

    buffer.Clear();
    assert(buffer.empty());
    assert(buffer.size() == 0);
    assert(buffer.oldest_seq() == std::nullopt);
    assert(buffer.newest_seq() == std::nullopt);

    assert(buffer.Append(25, "p25_new"));
    assert(buffer.size() == 1);
    assert(buffer.newest_seq() == std::optional<uint64_t>(25));
  }

  // Test 6: Default capacity large stream and gap detection
  {
    MahoAgentEventReplayBuffer buffer;
    assert(buffer.capacity() == 512);

    for (uint64_t i = 1; i <= 1000; ++i) {
      assert(buffer.Append(i, "ev_" + std::to_string(i)));
    }

    assert(buffer.size() == 512);
    assert(buffer.oldest_seq() == std::optional<uint64_t>(489));
    assert(buffer.newest_seq() == std::optional<uint64_t>(1000));

    ReplayResult full_retained = buffer.ReplayAfter(488);
    assert(full_retained.is_ok());
    assert(full_retained.events().size() == 512);
    assert(full_retained.events().front().seq == 489);
    assert(full_retained.events().back().seq == 1000);

    ReplayResult gap = buffer.ReplayAfter(487);
    assert(gap.is_gap());
    assert(gap.oldest_retained_seq() == 489);

    ReplayResult at_tail = buffer.ReplayAfter(1000);
    assert(at_tail.is_ok());
    assert(at_tail.events().empty());
  }

  std::cout << "[PASS] All MahoAgentEventReplayBuffer standalone tests passed!" << std::endl;
  return 0;
}

#endif
