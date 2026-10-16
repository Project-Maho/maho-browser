// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_agent_task_model.h"

#include <string>
#include <utility>
#include <vector>

#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

ai::ControlActivity Activity(std::string session_id,
                             ai::ControlActivityState state,
                             base::TimeTicks updated_at) {
  ai::ControlActivity activity;
  activity.session_id = std::move(session_id);
  activity.controller_display_name = "Maho Agent";
  activity.state = state;
  activity.updated_at = updated_at;
  return activity;
}

TEST(MahoSidebarAgentTaskModelTest, NeedsYouIsAttentiveAndSortedFirst) {
  const base::TimeTicks now = base::TimeTicks() + base::Minutes(60);
  auto running = Activity("running", ai::ControlActivityState::kActing,
                          now - base::Minutes(1));
  auto waiting = Activity("waiting", ai::ControlActivityState::kWaitingApproval,
                          now - base::Minutes(10));
  waiting.receipts.push_back({.summary = "Approval requested"});

  const auto rows = BuildAgentTaskRows({running, waiting}, now);

  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0].session_id, "waiting");
  EXPECT_EQ(rows[0].status, AgentTaskStatus::kNeedsYou);
  EXPECT_TRUE(rows[0].attention);
  EXPECT_EQ(rows[0].last_receipt_summary, "Approval requested");
  EXPECT_EQ(rows[1].session_id, "running");
}

TEST(MahoSidebarAgentTaskModelTest, DisconnectedIsNotFailed) {
  const base::TimeTicks now = base::TimeTicks() + base::Minutes(60);
  const auto disconnected = Activity(
      "disconnected", ai::ControlActivityState::kDisconnected, now);
  const auto failed =
      Activity("failed", ai::ControlActivityState::kFailed, now);

  const auto rows = BuildAgentTaskRows({disconnected, failed}, now);

  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0].status, AgentTaskStatus::kDisconnected);
  EXPECT_EQ(rows[1].status, AgentTaskStatus::kFailed);
}

TEST(MahoSidebarAgentTaskModelTest, DropsFinishedOlderThanThirtyMinutes) {
  const base::TimeTicks now = base::TimeTicks() + base::Minutes(60);
  const auto old = Activity("old", ai::ControlActivityState::kCompleted,
                            now - base::Minutes(30) - base::Microseconds(1));

  EXPECT_TRUE(BuildAgentTaskRows({old}, now).empty());
}

TEST(MahoSidebarAgentTaskModelTest, KeepsOnlyFiveMostRecentFinishedRows) {
  const base::TimeTicks now = base::TimeTicks() + base::Minutes(60);
  std::vector<ai::ControlActivity> activities;
  for (int i = 0; i < 7; ++i) {
    activities.push_back(Activity(
        "finished-" + std::to_string(i), ai::ControlActivityState::kCompleted,
        now - base::Minutes(i)));
  }

  const auto rows = BuildAgentTaskRows(activities, now);

  ASSERT_EQ(rows.size(), 5u);
  EXPECT_EQ(rows[0].session_id, "finished-0");
  EXPECT_EQ(rows[4].session_id, "finished-4");
}

}  // namespace
}  // namespace maho
