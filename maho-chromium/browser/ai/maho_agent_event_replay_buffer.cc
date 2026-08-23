// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_agent_event_replay_buffer.h"

namespace maho::ai {

MahoAgentEventReplayBuffer::MahoAgentEventReplayBuffer(size_t capacity)
    : capacity_(capacity > 0 ? capacity : 1) {}

MahoAgentEventReplayBuffer::~MahoAgentEventReplayBuffer() = default;

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

  // Enforce bounded ring capacity
  if (buffer_.size() >= capacity_) {
    buffer_.pop_front();
  }

  buffer_.push_back(ReplayEvent{seq, std::move(payload)});
  return true;
}

ReplayResult MahoAgentEventReplayBuffer::ReplayAfter(uint64_t after_seq) const {
  if (buffer_.empty()) {
    return ReplayResult::Ok({});
  }

  const uint64_t oldest_retained = buffer_.front().seq;

  // If the requester is asking for events after `after_seq`, they require events
  // starting from `after_seq + 1`. If `after_seq + 1 < oldest_retained` (i.e.
  // `after_seq < oldest_retained - 1`), an intermediate event was already evicted.
  // We MUST return an explicit ReplayGap rather than a silent partial history.
  if (oldest_retained > 0 && after_seq < oldest_retained - 1) {
    return ReplayResult::Gap(oldest_retained);
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

bool MahoAgentEventReplayBuffer::empty() const {
  return buffer_.empty();
}

void MahoAgentEventReplayBuffer::Clear() {
  buffer_.clear();
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
