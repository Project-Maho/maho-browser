// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_agent_tasks_policy.h"

#include <algorithm>

namespace maho {

AgentTasksSectionState ComputeAgentTasksSectionState(
    const std::vector<AgentTaskRow>& rows,
    bool expanded) {
  const bool has_attention = std::any_of(
      rows.begin(), rows.end(), [](const AgentTaskRow& row) {
        return row.attention;
      });
  return {.visible = !rows.empty(),
          .expanded = !rows.empty() && (expanded || has_attention)};
}

bool CanDisconnectAgentTask(const AgentTaskRow& row) {
  return row.focus_tab_id > 0 &&
         (row.status == AgentTaskStatus::kRunning ||
          row.status == AgentTaskStatus::kNeedsYou ||
          row.status == AgentTaskStatus::kPaused);
}

}  // namespace maho
