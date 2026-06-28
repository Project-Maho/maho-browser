// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_TOOL_EXECUTION_GRAPH_H_
#define MAHO_BROWSER_AI_MAHO_TOOL_EXECUTION_GRAPH_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"

class Browser;
class MahoBrowserToolExecutor;

namespace maho {

// ToolExecutionGraph owns tool-executor instance(s) as members so that
// async tool callbacks (e.g., ExecuteJavaScriptInIsolatedWorld) cannot
// outlive their executor.
//
// Audit findings closed:
//   C2 — stack-local MahoBrowserToolExecutor destroyed before async
//        callback fires. ToolExecutionGraph holds the executor as a
//        member, alive for the full conversation task.
//
// Each Execute() invocation starts a deadline timer (default 30 s).
// If the executor's callback does not fire before the deadline, the
// CompletionCallback fires with success=false and a deadline-exceeded
// result DictValue.
class ToolExecutionGraph {
 public:
  // Result delivery: base::DictValue containing tool result or error.
  using CompletionCallback = base::OnceCallback<void(base::DictValue result)>;

  explicit ToolExecutionGraph(Browser* browser,
                              bool browser_tools_v1_enabled,
                              base::TimeDelta deadline = base::Seconds(30));
  ~ToolExecutionGraph();

  ToolExecutionGraph(const ToolExecutionGraph&) = delete;
  ToolExecutionGraph& operator=(const ToolExecutionGraph&) = delete;

  // Execute a tool call. The callback fires exactly once: either with
  // the executor's actual result, or with an error DictValue containing
  // "deadline exceeded" if the deadline elapses first.
  void Execute(const std::string& tool_name,
               const base::DictValue& arguments,
               CompletionCallback callback);

  // Cancel any in-flight execution. Subsequent callbacks are dropped.
  void CancelInflight();

  bool has_inflight() const;

 private:
  friend class ToolExecutionGraphTest;

  void OnExecutorResult(base::DictValue result);
  void OnDeadline();

  SEQUENCE_CHECKER(sequence_checker_);
  std::unique_ptr<MahoBrowserToolExecutor> executor_;
  base::TimeDelta deadline_;
  base::OneShotTimer deadline_timer_;
  CompletionCallback inflight_callback_;
  std::string inflight_tool_name_;
  bool has_inflight_ = false;
  base::WeakPtrFactory<ToolExecutionGraph> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_AI_MAHO_TOOL_EXECUTION_GRAPH_H_
