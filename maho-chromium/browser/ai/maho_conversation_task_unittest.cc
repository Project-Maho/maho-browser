// Copyright 2025 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "maho/browser/ai/maho_conversation_task.h"

#include <string>

#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

class ConversationTaskTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(ConversationTaskTest, InitialStateIsIdle) {
  ConversationTask task("test-task-001");
  EXPECT_EQ(task.state(), ConversationTask::State::kIdle);
  EXPECT_TRUE(task.accumulated_text().empty());
  EXPECT_FALSE(task.is_at_capacity());
}

TEST_F(ConversationTaskTest, HappyPathStreamCompletes) {
  ConversationTask task("happy-path");

  EXPECT_TRUE(task.StartStreaming());
  EXPECT_EQ(task.state(), ConversationTask::State::kStreaming);

  EXPECT_TRUE(task.OnToken("Hello"));
  EXPECT_TRUE(task.OnToken(", "));
  EXPECT_TRUE(task.OnToken("world!"));

  EXPECT_TRUE(task.Complete());
  EXPECT_EQ(task.state(), ConversationTask::State::kDone);
  EXPECT_EQ(task.accumulated_text(), "Hello, world!");
}

TEST_F(ConversationTaskTest, ToolCallRoundTrip) {
  ConversationTask task("tool-call");

  EXPECT_TRUE(task.StartStreaming());
  EXPECT_TRUE(task.OnToken("thinking..."));

  EXPECT_TRUE(task.EnterToolCall());
  EXPECT_EQ(task.state(), ConversationTask::State::kToolCall);

  // Cannot append tokens during tool call.
  EXPECT_FALSE(task.OnToken("nope"));

  EXPECT_TRUE(task.LeaveToolCall());
  EXPECT_EQ(task.state(), ConversationTask::State::kStreaming);

  EXPECT_TRUE(task.OnToken(" done"));
  EXPECT_TRUE(task.Complete());
  EXPECT_EQ(task.state(), ConversationTask::State::kDone);
  EXPECT_EQ(task.accumulated_text(), "thinking... done");
}

TEST_F(ConversationTaskTest, OnTokenAtCapacityReturnsFalse) {
  ConversationTask task("capacity-test");
  EXPECT_TRUE(task.StartStreaming());

  // Create a 1 MB chunk.
  const std::string one_mb(1024 * 1024, 'A');

  // First 1 MB: success.
  EXPECT_TRUE(task.OnToken(one_mb));
  EXPECT_FALSE(task.is_at_capacity());

  // Second 1 MB: fills exactly to cap — success (fills to exactly 2 MB).
  EXPECT_TRUE(task.OnToken(one_mb));
  EXPECT_TRUE(task.is_at_capacity());

  // Third attempt: at capacity, returns false immediately.
  EXPECT_FALSE(task.OnToken("X"));
  EXPECT_TRUE(task.is_at_capacity());

  // Accumulated size should be exactly kMaxAccumulatedBytes.
  EXPECT_EQ(task.accumulated_text().size(),
            ConversationTask::kMaxAccumulatedBytes);
}

TEST_F(ConversationTaskTest, OnTokenAfterCompleteRejected) {
  ConversationTask task("after-complete");

  EXPECT_TRUE(task.StartStreaming());
  EXPECT_TRUE(task.OnToken("partial"));
  EXPECT_TRUE(task.Complete());
  EXPECT_EQ(task.state(), ConversationTask::State::kDone);

  // OnToken after Complete is an illegal state transition.
  EXPECT_FALSE(task.OnToken("more"));
  // Text should not have changed.
  EXPECT_EQ(task.accumulated_text(), "partial");
}

TEST_F(ConversationTaskTest, CancelFromAnyState) {
  // Cancel from Idle.
  {
    ConversationTask task("cancel-idle");
    EXPECT_TRUE(task.Cancel());
    EXPECT_EQ(task.state(), ConversationTask::State::kCancelled);
  }

  // Cancel from Streaming.
  {
    ConversationTask task("cancel-streaming");
    task.StartStreaming();
    EXPECT_TRUE(task.Cancel());
    EXPECT_EQ(task.state(), ConversationTask::State::kCancelled);
  }

  // Cancel from ToolCall.
  {
    ConversationTask task("cancel-toolcall");
    task.StartStreaming();
    task.EnterToolCall();
    EXPECT_TRUE(task.Cancel());
    EXPECT_EQ(task.state(), ConversationTask::State::kCancelled);
  }

  // Cancel from Done.
  {
    ConversationTask task("cancel-done");
    task.StartStreaming();
    task.Complete();
    EXPECT_TRUE(task.Cancel());
    EXPECT_EQ(task.state(), ConversationTask::State::kCancelled);
  }

  // Cancel from Error.
  {
    ConversationTask task("cancel-error");
    task.StartStreaming();
    task.Error("oops");
    EXPECT_TRUE(task.Cancel());
    EXPECT_EQ(task.state(), ConversationTask::State::kCancelled);
  }

  // Cancel from Cancelled is rejected.
  {
    ConversationTask task("cancel-cancelled");
    task.Cancel();
    EXPECT_FALSE(task.Cancel());
  }
}

TEST_F(ConversationTaskTest, ErrorStoresMessage) {
  ConversationTask task("error-msg");

  EXPECT_TRUE(task.StartStreaming());
  EXPECT_TRUE(task.Error("something went wrong"));
  EXPECT_EQ(task.state(), ConversationTask::State::kError);
  EXPECT_EQ(task.error_message(), "something went wrong");
}

TEST_F(ConversationTaskTest, TaskIdIsStable) {
  ConversationTask task("stable-id-42");

  EXPECT_EQ(task.task_id(), "stable-id-42");

  task.StartStreaming();
  EXPECT_EQ(task.task_id(), "stable-id-42");

  task.OnToken("data");
  EXPECT_EQ(task.task_id(), "stable-id-42");

  task.Complete();
  EXPECT_EQ(task.task_id(), "stable-id-42");

  task.Cancel();
  EXPECT_EQ(task.task_id(), "stable-id-42");
}

}  // namespace
}  // namespace maho
