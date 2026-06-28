// Copyright 2025 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "maho/browser/ai/maho_conversation_task.h"

#include <utility>

#include "base/check.h"
#include "base/notreached.h"

namespace maho {

ConversationTask::ConversationTask(std::string task_id)
    : task_id_(std::move(task_id)) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

ConversationTask::~ConversationTask() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

const std::string& ConversationTask::task_id() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return task_id_;
}

ConversationTask::State ConversationTask::state() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return state_;
}

bool ConversationTask::StartStreaming() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ != State::kIdle) {
    DUMP_WILL_BE_NOTREACHED();
    return false;
  }
  state_ = State::kStreaming;
  return true;
}

bool ConversationTask::OnToken(std::string_view token) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ != State::kStreaming) {
    DUMP_WILL_BE_NOTREACHED();
    return false;
  }
  if (is_at_capacity()) {
    return false;
  }

  // Lazy reserve to avoid repeated realloc during streaming.
  if (!accumulator_reserved_) {
    accumulated_text_.reserve(kMaxAccumulatedBytes);
    accumulator_reserved_ = true;
  }

  // Clamp: only append up to the remaining capacity.
  size_t remaining = kMaxAccumulatedBytes - accumulated_text_.size();
  if (token.size() > remaining) {
    accumulated_text_.append(token.data(), remaining);
    return false;
  }

  accumulated_text_.append(token.data(), token.size());
  return true;
}

bool ConversationTask::EnterToolCall() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ != State::kStreaming) {
    DUMP_WILL_BE_NOTREACHED();
    return false;
  }
  state_ = State::kToolCall;
  return true;
}

bool ConversationTask::LeaveToolCall() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ != State::kToolCall) {
    DUMP_WILL_BE_NOTREACHED();
    return false;
  }
  state_ = State::kStreaming;
  return true;
}

bool ConversationTask::Complete() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ != State::kStreaming) {
    DUMP_WILL_BE_NOTREACHED();
    return false;
  }
  state_ = State::kDone;
  return true;
}

bool ConversationTask::Error(const std::string& message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Error is valid from any non-terminal state.
  if (state_ == State::kError || state_ == State::kCancelled) {
    DUMP_WILL_BE_NOTREACHED();
    return false;
  }
  error_message_ = message;
  state_ = State::kError;
  return true;
}

bool ConversationTask::Cancel() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Cancel is valid from any non-terminal state (including kDone for cleanup).
  if (state_ == State::kCancelled) {
    DUMP_WILL_BE_NOTREACHED();
    return false;
  }
  state_ = State::kCancelled;
  return true;
}

std::string_view ConversationTask::accumulated_text() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return accumulated_text_;
}

bool ConversationTask::is_at_capacity() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return accumulated_text_.size() >= kMaxAccumulatedBytes;
}

const std::string& ConversationTask::error_message() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return error_message_;
}

}  // namespace maho
