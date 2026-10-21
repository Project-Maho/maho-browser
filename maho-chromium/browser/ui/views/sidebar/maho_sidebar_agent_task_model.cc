// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_agent_task_model.h"

#include <algorithm>

#include "base/strings/utf_string_conversions.h"

namespace maho {
namespace {

constexpr base::TimeDelta kFinishedRetention = base::Minutes(30);
constexpr size_t kMaxFinishedRows = 5;

bool IsFinished(AgentTaskStatus status) {
  return status == AgentTaskStatus::kDisconnected ||
         status == AgentTaskStatus::kDone ||
         status == AgentTaskStatus::kFailed;
}

AgentTaskStatus StatusFor(const ai::ControlActivity& activity) {
  switch (activity.state) {
    case ai::ControlActivityState::kReading:
    case ai::ControlActivityState::kActing:
      return AgentTaskStatus::kRunning;
    case ai::ControlActivityState::kWaitingApproval:
      return AgentTaskStatus::kNeedsYou;
    case ai::ControlActivityState::kPaused:
      return AgentTaskStatus::kPaused;
    case ai::ControlActivityState::kDisconnected:
      return AgentTaskStatus::kDisconnected;
    case ai::ControlActivityState::kCompleted:
      return AgentTaskStatus::kDone;
    case ai::ControlActivityState::kFailed:
      return AgentTaskStatus::kFailed;
  }
}

AgentTaskRow MakeRow(const ai::ControlActivity& activity) {
  AgentTaskRow row;
  row.session_id = activity.session_id;
  row.title = base::UTF8ToUTF16(activity.controller_display_name);
  if (activity.target && !activity.target->title.empty()) {
    row.title += u" - ";
    row.title += activity.target->title;
    row.focus_tab_id = activity.target->tab_id;
  } else if (activity.target) {
    row.focus_tab_id = activity.target->tab_id;
  }
  row.status = StatusFor(activity);
  row.attention = row.status == AgentTaskStatus::kNeedsYou;
  if (!activity.receipts.empty()) {
    row.last_receipt_summary = activity.receipts.back().summary;
  }
  return row;
}

}  // namespace

std::vector<AgentTaskRow> BuildAgentTaskRows(
    const std::vector<ai::ControlActivity>& activities,
    base::TimeTicks now) {
  std::vector<const ai::ControlActivity*> ordered;
  ordered.reserve(activities.size());
  for (const auto& activity : activities) {
    const AgentTaskStatus status = StatusFor(activity);
    if (!IsFinished(status) ||
        (activity.updated_at <= now &&
         now - activity.updated_at <= kFinishedRetention)) {
      ordered.push_back(&activity);
    }
  }

  std::stable_sort(ordered.begin(), ordered.end(), [](const auto* lhs,
                                                       const auto* rhs) {
    const AgentTaskStatus lhs_status = StatusFor(*lhs);
    const AgentTaskStatus rhs_status = StatusFor(*rhs);
    const int lhs_group = lhs_status == AgentTaskStatus::kNeedsYou
                              ? 0
                              : (IsFinished(lhs_status) ? 2 : 1);
    const int rhs_group = rhs_status == AgentTaskStatus::kNeedsYou
                              ? 0
                              : (IsFinished(rhs_status) ? 2 : 1);
    if (lhs_group != rhs_group) {
      return lhs_group < rhs_group;
    }
    return lhs->updated_at > rhs->updated_at;
  });

  std::vector<AgentTaskRow> rows;
  rows.reserve(ordered.size());
  size_t finished_count = 0;
  for (const auto* activity : ordered) {
    const AgentTaskStatus status = StatusFor(*activity);
    if (IsFinished(status) && finished_count++ >= kMaxFinishedRows) {
      continue;
    }
    rows.push_back(MakeRow(*activity));
  }
  return rows;
}

}  // namespace maho
