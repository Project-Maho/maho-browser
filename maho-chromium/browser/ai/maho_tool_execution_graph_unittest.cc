// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_tool_execution_graph.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "base/values.h"
#include "maho/browser/ai/maho_browser_tool_executor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

// Test fixture that uses MOCK_TIME to control timer advancement.
// Since we can't easily construct a real Browser* in unit tests,
// we directly test the timer/callback plumbing by injecting results
// through the private OnExecutorResult path (friend access).
class ToolExecutionGraphTest : public testing::Test {
 protected:
  ToolExecutionGraphTest()
      : task_environment_(
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}

  // Helper: create a ToolExecutionGraph with a nullptr Browser.
  // The executor will be created but any actual tool call will fail.
  // We use this for timer behavior tests only.
  std::unique_ptr<ToolExecutionGraph> CreateGraph(
      base::TimeDelta deadline = base::Seconds(30)) {
    // Pass nullptr for Browser* — the executor will error on any real
    // call but that's fine for testing the graph's timer/callback logic.
    return std::make_unique<ToolExecutionGraph>(
        /*browser=*/nullptr, /*browser_tools_v1_enabled=*/true, deadline);
  }

  // Friend access: directly invoke OnExecutorResult to simulate the
  // executor completing without needing a real Browser.
  void SimulateExecutorResult(ToolExecutionGraph* graph,
                              base::DictValue result) {
    graph->OnExecutorResult(std::move(result));
  }

  // Friend access: directly invoke OnDeadline to simulate timer fire.
  void SimulateDeadline(ToolExecutionGraph* graph) {
    graph->OnDeadline();
  }

  base::test::TaskEnvironment task_environment_;
};

TEST_F(ToolExecutionGraphTest, HasInflightTransitions) {
  auto graph = CreateGraph();

  EXPECT_FALSE(graph->has_inflight());

  // Start an execution — the executor will call back synchronously
  // with an error since browser is nullptr, but let's test the case
  // where we simulate the result manually. We need to prevent the
  // real executor from firing by using a tool name that still triggers
  // async behavior. Instead, use the direct simulation approach:
  // Mark inflight manually via Execute path, then simulate result.

  // Execute with "get_active_tab" — this will call back synchronously
  // because the executor handles it without async I/O.  But since
  // browser is nullptr, it returns an error result synchronously.
  // After that, has_inflight should be false (the callback path
  // already cleared it via OnExecutorResult).
  bool callback_fired = false;
  base::DictValue received_result;
  graph->Execute(
      "get_active_tab", base::DictValue(),
      base::BindOnce(
          [](bool* fired, base::DictValue* out, base::DictValue result) {
            *fired = true;
            *out = std::move(result);
          },
          &callback_fired, &received_result));

  // The executor calls back synchronously for get_active_tab (no async).
  EXPECT_TRUE(callback_fired);
  EXPECT_FALSE(graph->has_inflight());

  // Verify the result contains error info (browser is null).
  const std::string* error = received_result.FindString("error");
  ASSERT_TRUE(error);
  EXPECT_FALSE(error->empty());
}

TEST_F(ToolExecutionGraphTest, DeadlineFiresCallback) {
  auto graph = CreateGraph(base::Seconds(5));

  // We need to test deadline behavior. Since the executor fires
  // synchronously with nullptr browser for known tools, we test
  // with an unknown tool name that the executor rejects sync.
  // Actually, any tool call with nullptr browser will complete
  // synchronously. To properly test deadline, we directly set up
  // the inflight state and advance time.

  // Approach: manually set inflight state via internals (friend).
  graph->has_inflight_ = true;
  graph->inflight_tool_name_ = "slow_tool";
  bool callback_fired = false;
  base::DictValue received_result;
  graph->inflight_callback_ = base::BindOnce(
      [](bool* fired, base::DictValue* out, base::DictValue result) {
        *fired = true;
        *out = std::move(result);
      },
      &callback_fired, &received_result);

  // Start deadline timer manually (simulates what Execute does).
  graph->deadline_timer_.Start(
      FROM_HERE, base::Seconds(5),
      base::BindOnce(&ToolExecutionGraph::OnDeadline,
                     graph->weak_factory_.GetWeakPtr()));

  EXPECT_TRUE(graph->has_inflight());
  EXPECT_FALSE(callback_fired);

  // Advance time past deadline.
  task_environment_.FastForwardBy(base::Seconds(5));

  EXPECT_TRUE(callback_fired);
  EXPECT_FALSE(graph->has_inflight());

  const std::string* error = received_result.FindString("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(*error, "deadline exceeded");

  const std::string* tool = received_result.FindString("tool");
  ASSERT_TRUE(tool);
  EXPECT_EQ(*tool, "slow_tool");
}

TEST_F(ToolExecutionGraphTest, CancelDropsCallback) {
  auto graph = CreateGraph(base::Seconds(5));

  // Set up inflight state directly.
  graph->has_inflight_ = true;
  graph->inflight_tool_name_ = "some_tool";
  bool callback_fired = false;
  graph->inflight_callback_ = base::BindOnce(
      [](bool* fired, base::DictValue result) { *fired = true; },
      &callback_fired);

  graph->deadline_timer_.Start(
      FROM_HERE, base::Seconds(5),
      base::BindOnce(&ToolExecutionGraph::OnDeadline,
                     graph->weak_factory_.GetWeakPtr()));

  EXPECT_TRUE(graph->has_inflight());

  // Cancel.
  graph->CancelInflight();
  EXPECT_FALSE(graph->has_inflight());

  // Advance past deadline — callback should NOT fire.
  task_environment_.FastForwardBy(base::Seconds(10));
  EXPECT_FALSE(callback_fired);
}

TEST_F(ToolExecutionGraphTest, LateExecutorResultAfterDeadlineIsDropped) {
  auto graph = CreateGraph(base::Seconds(5));

  // Set up inflight state.
  graph->has_inflight_ = true;
  graph->inflight_tool_name_ = "slow_tool";
  int callback_count = 0;
  graph->inflight_callback_ = base::BindOnce(
      [](int* count, base::DictValue result) { (*count)++; },
      &callback_count);

  graph->deadline_timer_.Start(
      FROM_HERE, base::Seconds(5),
      base::BindOnce(&ToolExecutionGraph::OnDeadline,
                     graph->weak_factory_.GetWeakPtr()));

  // Advance past deadline — fires the callback once.
  task_environment_.FastForwardBy(base::Seconds(5));
  EXPECT_EQ(callback_count, 1);
  EXPECT_FALSE(graph->has_inflight());

  // Now simulate a late executor result — should be dropped.
  base::DictValue late_result;
  late_result.Set("ok", true);
  SimulateExecutorResult(graph.get(), std::move(late_result));

  // Still only one callback invocation.
  EXPECT_EQ(callback_count, 1);
}

TEST_F(ToolExecutionGraphTest, NormalResultStopsDeadlineTimer) {
  auto graph = CreateGraph(base::Seconds(30));

  // Set up inflight state.
  graph->has_inflight_ = true;
  graph->inflight_tool_name_ = "read_current_page";
  int callback_count = 0;
  base::DictValue received_result;
  graph->inflight_callback_ = base::BindOnce(
      [](int* count, base::DictValue* out, base::DictValue result) {
        (*count)++;
        *out = std::move(result);
      },
      &callback_count, &received_result);

  graph->deadline_timer_.Start(
      FROM_HERE, base::Seconds(30),
      base::BindOnce(&ToolExecutionGraph::OnDeadline,
                     graph->weak_factory_.GetWeakPtr()));

  // Simulate normal result before deadline.
  base::DictValue normal_result;
  normal_result.Set("ok", true);
  normal_result.Set("tool", "read_current_page");
  SimulateExecutorResult(graph.get(), std::move(normal_result));

  EXPECT_EQ(callback_count, 1);
  EXPECT_FALSE(graph->has_inflight());

  // The result should be the normal one, not deadline error.
  std::optional<bool> ok = received_result.FindBool("ok");
  ASSERT_TRUE(ok.has_value());
  EXPECT_TRUE(ok.value());

  // Advance time past what would have been the deadline.
  task_environment_.FastForwardBy(base::Seconds(30));

  // No additional callback invocation.
  EXPECT_EQ(callback_count, 1);
}

}  // namespace
}  // namespace maho
