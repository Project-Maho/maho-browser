// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_agent_event_replay_buffer.h"

#include <algorithm>

namespace maho::ai {

ReplayResult::ReplayResult(Status status,
                           std::vector<ReplayEvent> events,
                           uint64_t oldest)
    : status_(status),
      events_(std::move(events)),
      oldest_retained_seq_(oldest) {}

ReplayResult::ReplayResult(const ReplayResult&) = default;
ReplayResult::ReplayResult(ReplayResult&&) = default;
ReplayResult& ReplayResult::operator=(const ReplayResult&) = default;
ReplayResult& ReplayResult::operator=(ReplayResult&&) = default;
ReplayResult::~ReplayResult() = default;

MahoAgentEventReplayBuffer::MahoAgentEventReplayBuffer(size_t capacity,
                                                       size_t max_bytes)
    : capacity_(capacity > 0 ? capacity : 1),
      max_bytes_(max_bytes > 0 ? max_bytes : 1) {}

MahoAgentEventReplayBuffer::~MahoAgentEventReplayBuffer() = default;

MahoAgentEventReplayBuffer::MahoAgentEventReplayBuffer(
    MahoAgentEventReplayBuffer&&) noexcept = default;
MahoAgentEventReplayBuffer& MahoAgentEventReplayBuffer::operator=(
    MahoAgentEventReplayBuffer&&) noexcept = default;

bool MahoAgentEventReplayBuffer::Append(uint64_t seq, std::string payload) {
  // Sequences are strictly 1-based and monotonic: reject seq == 0 or non-increasing seq
  if (seq == 0) {
    return false;
  }
  if (has_seen_any_ && seq <= highest_seq_seen_) {
    return false;
  }

  highest_seq_seen_ = seq;
  has_seen_any_ = true;

  bytes_ += payload.size();
  buffer_.push_back(ReplayEvent{seq, std::move(payload)});

  // Oversized events are consumed but evicted too, preserving a strict budget
  // and reporting a gap to any reader that needs the omitted event.
  while (!buffer_.empty() &&
         (buffer_.size() > capacity_ || bytes_ > max_bytes_)) {
    EvictOldest();
  }
  return true;
}

void MahoAgentEventReplayBuffer::EvictOldest() {
  const ReplayEvent& oldest = buffer_.front();
  bytes_ -= oldest.payload.size();
  highest_evicted_seq_ = std::max(highest_evicted_seq_, oldest.seq);
  buffer_.pop_front();
}

ReplayResult MahoAgentEventReplayBuffer::ReplayAfter(uint64_t after_seq) const {
  // A requester asking for events after `after_seq` needs every event with
  // seq > after_seq. If any such event was evicted (or dropped by Clear()), the
  // history is incomplete: return an explicit ReplayGap rather than a silent
  // partial history. Events that were never appended (e.g. the stream's first
  // seq is > 1) are not a gap.
  if (highest_evicted_seq_ > after_seq) {
    return ReplayResult::Gap(buffer_.empty() ? 0 : buffer_.front().seq);
  }

  if (buffer_.empty()) {
    if (after_seq > highest_seq_seen_) {
      return ReplayResult::Gap(0);
    }
    return ReplayResult::Ok({});
  }

  // The requester's cursor is ahead of anything this buffer ever produced since
  // it was last reset (e.g. the run restarted its sequence): tell them to
  // resynchronise instead of returning an empty "caught up" result.
  if (after_seq > buffer_.back().seq) {
    return ReplayResult::Gap(buffer_.front().seq);
  }

  std::vector<ReplayEvent> replayed;
  for (const auto& entry : buffer_) {
    if (entry.seq > after_seq) {
      replayed.push_back(entry);
    }
  }

  return ReplayResult::Ok(std::move(replayed));
}

size_t MahoAgentEventReplayBuffer::size() const {
  return buffer_.size();
}

size_t MahoAgentEventReplayBuffer::capacity() const {
  return capacity_;
}

size_t MahoAgentEventReplayBuffer::max_bytes() const {
  return max_bytes_;
}

size_t MahoAgentEventReplayBuffer::bytes() const {
  return bytes_;
}

bool MahoAgentEventReplayBuffer::empty() const {
  return buffer_.empty();
}

void MahoAgentEventReplayBuffer::Clear() {
  // Everything appended so far is dropped: later replay requests that still
  // need those events must see a gap, not an empty "caught up" result.
  highest_evicted_seq_ = std::max(highest_evicted_seq_, highest_seq_seen_);
  buffer_.clear();
  bytes_ = 0;
  highest_seq_seen_ = 0;
  has_seen_any_ = false;
}

std::optional<uint64_t> MahoAgentEventReplayBuffer::oldest_seq() const {
  if (buffer_.empty()) {
    return std::nullopt;
  }
  return buffer_.front().seq;
}

std::optional<uint64_t> MahoAgentEventReplayBuffer::newest_seq() const {
  if (buffer_.empty()) {
    return std::nullopt;
  }
  return buffer_.back().seq;
}

}  // namespace maho::ai
