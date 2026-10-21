// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_AGENT_TASK_MODEL_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_AGENT_TASK_MODEL_H_

#include <string>
#include <vector>

#include "base/time/time.h"
#include "maho/browser/ai/maho_control_activity_service.h"

namespace maho {

enum class AgentTaskStatus {
  kRunning,
  kNeedsYou,
  kPaused,
  kDisconnected,
  kDone,
  kFailed,
};

struct AgentTaskRow {
  std::string session_id;
  std::u16string title;
  AgentTaskStatus status = AgentTaskStatus::kRunning;
  bool attention = false;
  int64_t focus_tab_id = 0;
  std::string last_receipt_summary;
};

// Builds a compact projection of control activities; it does not include AI
// chat history. Finished records are retained only for this projection.
std::vector<AgentTaskRow> BuildAgentTaskRows(
    const std::vector<ai::ControlActivity>& activities,
    base::TimeTicks now);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_AGENT_TASK_MODEL_H_
