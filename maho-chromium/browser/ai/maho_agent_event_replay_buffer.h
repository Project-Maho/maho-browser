// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_AGENT_EVENT_REPLAY_BUFFER_H_
#define MAHO_BROWSER_AI_MAHO_AGENT_EVENT_REPLAY_BUFFER_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace maho::ai {

/// An individual sequenced event recorded in the replay buffer.
struct ReplayEvent {
  uint64_t seq = 0;
  std::string payload;

  bool operator==(const ReplayEvent& other) const {
    return seq == other.seq && payload == other.payload;
  }
};

/// Explicit gap indication when requested sequence has already been evicted from ring buffer.
struct ReplayGap {
  uint64_t oldest_retained_seq = 0;

  bool operator==(const ReplayGap& other) const {
    return oldest_retained_seq == other.oldest_retained_seq;
  }
};

/// Result of a replay request: either a continuous event slice or an explicit ReplayGap.
class ReplayResult {
 public:
  enum class Status {
    kOk,
    kGap,
  };

  static ReplayResult Ok(std::vector<ReplayEvent> events) {
    return ReplayResult(Status::kOk, std::move(events), 0);
  }

  static ReplayResult Gap(uint64_t oldest_retained_seq) {
    return ReplayResult(Status::kGap, {}, oldest_retained_seq);
  }

  bool is_ok() const { return status_ == Status::kOk; }
  bool is_gap() const { return status_ == Status::kGap; }

  const std::vector<ReplayEvent>& events() const { return events_; }
  uint64_t oldest_retained_seq() const { return oldest_retained_seq_; }
  ReplayGap gap() const { return ReplayGap{oldest_retained_seq_}; }

 private:
  ReplayResult(Status status, std::vector<ReplayEvent> events, uint64_t oldest)
      : status_(status), events_(std::move(events)), oldest_retained_seq_(oldest) {}

  Status status_;
  std::vector<ReplayEvent> events_;
  uint64_t oldest_retained_seq_ = 0;
};

/// Bounded per-run ring buffer for runtime event replay.
/// Guarantees monotonic event sequence replay and explicit gap reporting.
class MahoAgentEventReplayBuffer {
 public:
  static constexpr size_t kDefaultCapacity = 512;

  explicit MahoAgentEventReplayBuffer(size_t capacity = kDefaultCapacity);
  ~MahoAgentEventReplayBuffer();

  MahoAgentEventReplayBuffer(const MahoAgentEventReplayBuffer&) = delete;
  MahoAgentEventReplayBuffer& operator=(const MahoAgentEventReplayBuffer&) = delete;
  MahoAgentEventReplayBuffer(MahoAgentEventReplayBuffer&&) noexcept = default;
  MahoAgentEventReplayBuffer& operator=(MahoAgentEventReplayBuffer&&) noexcept = default;

  /// Appends an event to the ring buffer.
  /// Strictly enforces monotonic sequence order: if seq <= highest_seq_seen_,
  /// rejects with false (duplicate or out-of-order sequence).
  /// When capacity is exceeded, the oldest retained event is evicted.
  bool Append(uint64_t seq, std::string payload);

  /// Replays events with sequence number strictly greater than `after_seq`.
  /// If `after_seq` is older than the oldest retained event sequence - 1 (meaning an event
  /// with seq > after_seq was already evicted), returns ReplayGap{oldest_retained_seq}.
  /// Otherwise returns ReplayResult::Ok with the events in sequence order.
  ReplayResult ReplayAfter(uint64_t after_seq) const;

  size_t size() const;
  size_t capacity() const;
  bool empty() const;
  void Clear();

  std::optional<uint64_t> oldest_seq() const;
  std::optional<uint64_t> newest_seq() const;

 private:
  size_t capacity_;
  std::deque<ReplayEvent> buffer_;
  uint64_t highest_seq_seen_ = 0;
  bool has_seen_any_ = false;
};

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_AGENT_EVENT_REPLAY_BUFFER_H_
