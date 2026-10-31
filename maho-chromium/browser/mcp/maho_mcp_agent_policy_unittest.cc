// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/mcp/maho_mcp_agent_policy.h"

#include "maho/browser/mcp/maho_mcp_session.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

TEST(MahoMcpAgentPolicyTest, AgentControllersOpenTabsInBackground) {
  EXPECT_TRUE(ShouldForegroundAgentTab(MahoMcpControllerKind::kMahoCli,
                                       std::nullopt));
  EXPECT_TRUE(ShouldForegroundAgentTab(MahoMcpControllerKind::kMahoCliRepl,
                                       std::nullopt));
  EXPECT_FALSE(ShouldForegroundAgentTab(MahoMcpControllerKind::kMahoBrowserMcp,
                                        std::nullopt));
  EXPECT_TRUE(
      ShouldForegroundAgentTab(MahoMcpControllerKind::kMahoBrowserMcp, false));
  EXPECT_FALSE(
      ShouldForegroundAgentTab(MahoMcpControllerKind::kMahoCli, true));
  EXPECT_FALSE(ShouldForegroundAgentTab(MahoMcpControllerKind::kThirdParty,
                                        std::nullopt));
}

TEST(MahoMcpAgentPolicyTest, KeyPressNeverStealsFocusFromBackgroundWindows) {
  EXPECT_FALSE(ShouldFocusForKeyPress(/*window_active=*/false,
                                      /*view_has_focus=*/false));
  EXPECT_FALSE(ShouldFocusForKeyPress(/*window_active=*/true,
                                      /*view_has_focus=*/true));
  EXPECT_TRUE(ShouldFocusForKeyPress(/*window_active=*/true,
                                     /*view_has_focus=*/false));
}

TEST(MahoMcpAgentPolicyTest, TaskSpaceLifecycle) {
  MahoAgentTaskSpaceRegistry spaces;
  constexpr int64_t kTab = 41;
  EXPECT_EQ(spaces.CheckMutation(kTab, "s1"),
            TaskSpaceMutationDecision::kAllowed);
  EXPECT_FALSE(spaces.Create(kTab, ""));
  ASSERT_TRUE(spaces.Create(kTab, "s1"));
  EXPECT_FALSE(spaces.Create(kTab, "s2"));
  EXPECT_EQ(spaces.CheckMutation(kTab, "s1"),
            TaskSpaceMutationDecision::kAllowed);
  EXPECT_EQ(spaces.CheckMutation(kTab, "s2"),
            TaskSpaceMutationDecision::kNotOwner);

  EXPECT_FALSE(spaces.Takeover(kTab, "s1"));
  EXPECT_FALSE(spaces.Handoff(kTab, "s2"));
  ASSERT_TRUE(spaces.Handoff(kTab, "s1"));
  EXPECT_FALSE(spaces.Handoff(kTab, "s1"));
  EXPECT_EQ(spaces.CheckMutation(kTab, "s1"),
            TaskSpaceMutationDecision::kUserInControl);
  ASSERT_TRUE(spaces.Takeover(kTab, "s1"));
  EXPECT_EQ(spaces.CheckMutation(kTab, "s1"),
            TaskSpaceMutationDecision::kAllowed);

  ASSERT_TRUE(spaces.Complete(kTab, "s1"));
  EXPECT_EQ(spaces.Find(kTab)->state, TaskSpaceState::kCompleted);
  EXPECT_EQ(spaces.CheckMutation(kTab, "s1"),
            TaskSpaceMutationDecision::kClosed);
  EXPECT_FALSE(spaces.Handoff(kTab, "s1"));
  ASSERT_TRUE(spaces.Create(kTab, "s2"));
  ASSERT_TRUE(spaces.Fail(kTab, "s2"));
  EXPECT_EQ(spaces.Find(kTab)->state, TaskSpaceState::kError);

  spaces.Remove(kTab);
  EXPECT_FALSE(spaces.Find(kTab).has_value());
  EXPECT_EQ(spaces.CheckMutation(kTab, "s1"),
            TaskSpaceMutationDecision::kAllowed);
}

TEST(MahoMcpAgentPolicyTest, PendingDialogBlocksPageCommandsOnly) {
  EXPECT_TRUE(IsBlockedByPendingDialog("browser_click"));
  EXPECT_TRUE(IsBlockedByPendingDialog("browser_navigate"));
  EXPECT_TRUE(IsBlockedByPendingDialog("page.accessibility_snapshot_v2"));
  EXPECT_FALSE(IsBlockedByPendingDialog("browser_dialog_respond"));
  EXPECT_FALSE(IsBlockedByPendingDialog("browser_tab_list"));
  EXPECT_FALSE(IsBlockedByPendingDialog("browser_release_lease"));
  EXPECT_FALSE(IsBlockedByPendingDialog("browser_task_space"));
}

TEST(MahoMcpAgentPolicyTest, DownloadRequestsAreConfined) {
  EXPECT_TRUE(ValidateDownloadRequest("https://a.test/f.pdf", std::nullopt).ok);
  EXPECT_TRUE(ValidateDownloadRequest("http://a.test/f", "reports/q3").ok);
  EXPECT_FALSE(ValidateDownloadRequest("", std::nullopt).ok);
  EXPECT_FALSE(ValidateDownloadRequest("file:///etc/passwd", std::nullopt).ok);
  EXPECT_FALSE(ValidateDownloadRequest("javascript:alert(1)", std::nullopt).ok);
  EXPECT_FALSE(ValidateDownloadRequest("https://a.test/f", "../up").ok);
  EXPECT_FALSE(ValidateDownloadRequest("https://a.test/f", "a/../../b").ok);
  EXPECT_FALSE(ValidateDownloadRequest("https://a.test/f", "/abs").ok);
  EXPECT_FALSE(ValidateDownloadRequest("https://a.test/f", "\\server").ok);
  EXPECT_FALSE(ValidateDownloadRequest("https://a.test/f", "C:\\x").ok);
  EXPECT_FALSE(
      ValidateDownloadRequest("https://a.test/f", std::string("a\0b", 3)).ok);
}

TEST(MahoMcpAgentPolicyTest, TypeRoutesToInsertTextOnlyForAdapters) {
  EXPECT_EQ(DecideTypeRoute("browser_type", false), TypeRoute::kDomTyping);
  EXPECT_EQ(DecideTypeRoute("browser_type", true), TypeRoute::kInsertText);
  EXPECT_EQ(DecideTypeRoute("browser_click", true), TypeRoute::kDomTyping);
}

TEST(MahoMcpAgentPolicyTest, ImportSourcesSerializeWithIndexes) {
  base::DictValue result = SerializeImportSources(
      {{"Chrome", "Google Chrome", "/p/Bookmarks", 6u, false},
       {"Safari", "Safari", "/p/Bookmarks.plist", 2u, true}});
  EXPECT_EQ(result.FindInt("count"), 2);
  const base::ListValue* sources = result.FindList("sources");
  ASSERT_TRUE(sources);
  ASSERT_EQ(sources->size(), 2u);
  const base::DictValue& safari = (*sources)[1].GetDict();
  EXPECT_EQ(safari.FindInt("index"), 1);
  EXPECT_EQ(*safari.FindString("browser_type"), "Safari");
  EXPECT_EQ(safari.FindBool("requires_full_disk_access"), true);
  EXPECT_EQ(safari.FindInt("services_supported"), 2);
}

}  // namespace
}  // namespace maho
