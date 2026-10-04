// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_tool_execution_graph.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "maho/browser/ai/maho_browser_tool_executor.h"

namespace maho {

ToolExecutionGraph::ToolExecutionGraph(Browser* browser,
                                       bool browser_tools_v1_enabled,
                                       base::TimeDelta deadline)
    : executor_(
          std::make_unique<MahoBrowserToolExecutor>(browser,
                                                   browser_tools_v1_enabled)),
      deadline_(deadline) {}

ToolExecutionGraph::~ToolExecutionGraph() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  deadline_timer_.Stop();
}

void ToolExecutionGraph::Execute(const std::string& tool_name,
                                 const base::DictValue& arguments,
                                 CompletionCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!has_inflight_) << "Only one in-flight tool execution at a time";

  inflight_callback_ = std::move(callback);
  inflight_tool_name_ = tool_name;
  has_inflight_ = true;

  deadline_timer_.Start(
      FROM_HERE, deadline_,
      base::BindOnce(&ToolExecutionGraph::OnDeadline,
                     weak_factory_.GetWeakPtr()));

  executor_->Execute(
      tool_name, arguments,
      base::BindOnce(&ToolExecutionGraph::OnExecutorResult,
                     weak_factory_.GetWeakPtr()));
}

void ToolExecutionGraph::OnExecutorResult(base::DictValue result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Guard: if already cancelled or timed-out, drop the late result.
  if (!has_inflight_) {
    return;
  }

  deadline_timer_.Stop();
  has_inflight_ = false;
  std::move(inflight_callback_).Run(std::move(result));
}

void ToolExecutionGraph::OnDeadline() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  has_inflight_ = false;

  if (inflight_callback_) {
    base::DictValue error_result;
    error_result.Set("ok", false);
    error_result.Set("tool", inflight_tool_name_);
    error_result.Set("error", "deadline exceeded");
    std::move(inflight_callback_).Run(std::move(error_result));
  }
}

void ToolExecutionGraph::CancelInflight() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  deadline_timer_.Stop();
  has_inflight_ = false;
  // Drop the callback without firing it.
  inflight_callback_.Reset();
}

bool ToolExecutionGraph::has_inflight() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return has_inflight_;
}

}  // namespace maho
