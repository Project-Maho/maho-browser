// Copyright 2025 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MAHO_BROWSER_AI_MAHO_CONVERSATION_TASK_H_
#define MAHO_BROWSER_AI_MAHO_CONVERSATION_TASK_H_

#include <string>
#include <string_view>

#include "base/sequence_checker.h"

namespace maho {

// ConversationTask owns one conversation turn's lifecycle.
//
// Audit findings closed:
//   H13 — accumulated_text_ unbounded growth → 2 MB cap with backpressure
//          signal.
//   Contributes to C2 — replaces ad-hoc bool flags with a clean state machine
//     so the executor's lifetime can be tied to task state transitions.
//
// Lifecycle: Idle → Streaming → ToolCall? → (Done | Error | Cancelled)
//   - StartStreaming: Idle → Streaming
//   - OnToken: Streaming → Streaming (appends to bounded accumulator)
//   - EnterToolCall: Streaming → ToolCall
//   - LeaveToolCall: ToolCall → Streaming
//   - Complete: Streaming → Done
//   - Error: any → Error
//   - Cancel: any → Cancelled
//
// Backpressure: when accumulated_text_ reaches 2 MB, is_at_capacity()
// returns true and OnToken returns false (caller is expected to stop
// accepting tokens and call Error("response too large")).
class ConversationTask {
 public:
  enum class State {
    kIdle,
    kStreaming,
    kToolCall,
    kDone,
    kError,
    kCancelled,
  };

  static constexpr size_t kMaxAccumulatedBytes = 2 * 1024 * 1024;  // 2 MB

  explicit ConversationTask(std::string task_id);
  ~ConversationTask();

  ConversationTask(const ConversationTask&) = delete;
  ConversationTask& operator=(const ConversationTask&) = delete;
  ConversationTask(ConversationTask&&) = delete;
  ConversationTask& operator=(ConversationTask&&) = delete;

  const std::string& task_id() const;
  State state() const;

  // State transition methods. Each returns true on legal transition,
  // false if the current state forbids it (with a DCHECK in debug).
  bool StartStreaming();
  bool OnToken(std::string_view token);
  bool EnterToolCall();
  bool LeaveToolCall();
  bool Complete();
  bool Error(const std::string& message);
  bool Cancel();

  // Accumulator state.
  std::string_view accumulated_text() const;
  bool is_at_capacity() const;
  const std::string& error_message() const;  // valid only in kError

 private:
  SEQUENCE_CHECKER(sequence_checker_);
  const std::string task_id_;
  State state_ = State::kIdle;
  std::string accumulated_text_;
  std::string error_message_;
  bool accumulator_reserved_ = false;
};

}  // namespace maho

#endif  // MAHO_BROWSER_AI_MAHO_CONVERSATION_TASK_H_
