// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_AGENT_TASKS_POLICY_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_AGENT_TASKS_POLICY_H_

#include "maho/browser/ui/views/sidebar/maho_sidebar_agent_task_model.h"

namespace maho {

struct AgentTasksSectionState {
  bool visible = false;
  bool expanded = false;
};

AgentTasksSectionState ComputeAgentTasksSectionState(
    const std::vector<AgentTaskRow>& rows,
    bool expanded);

bool CanDisconnectAgentTask(const AgentTaskRow& row);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_AGENT_TASKS_POLICY_H_
