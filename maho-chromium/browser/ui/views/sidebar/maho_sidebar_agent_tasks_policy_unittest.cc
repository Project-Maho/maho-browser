// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_agent_tasks_policy.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

TEST(MahoSidebarAgentTasksPolicyTest, HidesEmptySection) {
  const auto state = ComputeAgentTasksSectionState({}, true);

  EXPECT_FALSE(state.visible);
  EXPECT_FALSE(state.expanded);
}

TEST(MahoSidebarAgentTasksPolicyTest, AttentionForcesExpansion) {
  AgentTaskRow row;
  row.attention = true;

  const auto state = ComputeAgentTasksSectionState({row}, false);

  EXPECT_TRUE(state.visible);
  EXPECT_TRUE(state.expanded);
}

TEST(MahoSidebarAgentTasksPolicyTest, PreservesUserExpansion) {
  const auto state = ComputeAgentTasksSectionState({AgentTaskRow()}, true);

  EXPECT_TRUE(state.visible);
  EXPECT_TRUE(state.expanded);
}

TEST(MahoSidebarAgentTasksPolicyTest, DisconnectRequiresLiveTabAndActivity) {
  AgentTaskRow row;
  row.focus_tab_id = 12;
  row.status = AgentTaskStatus::kRunning;
  EXPECT_TRUE(CanDisconnectAgentTask(row));

  row.status = AgentTaskStatus::kDone;
  EXPECT_FALSE(CanDisconnectAgentTask(row));
  row.status = AgentTaskStatus::kRunning;
  row.focus_tab_id = 0;
  EXPECT_FALSE(CanDisconnectAgentTask(row));
}

}  // namespace
}  // namespace maho
