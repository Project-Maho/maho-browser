// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_action_handler.h"

#include <string>
#include <vector>

#include "maho/browser/maho_private_context_policy.h"  // nogncheck
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "maho/browser/ui/views/split_view/maho_split_view_controller.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

class MahoCommandActionHandlerTest : public ::testing::Test {};

TEST_F(MahoCommandActionHandlerTest, ForgedActionIdsAreDenied) {
  for (const std::string& id : MahoCommandModel::PrivateActionAllowlist()) {
    EXPECT_TRUE(IsPrimaryIncognitoAllowedActionId(id)) << id;
  }

  for (const char* forged :
       {"new_tab", "new_incognito", "open_history", "open_bookmarks",
        "next_space", "select_tab_1", "add_split_view", "remove_split_view",
        "ai_panel", "settings", "little_arc", "new_note", "", "toggle_sidebarX",
        "CLOSE_TAB", "print", "action:close_tab"}) {
    EXPECT_FALSE(IsPrimaryIncognitoAllowedActionId(forged)) << forged;
  }
}

TEST_F(MahoCommandActionHandlerTest, PrivateSplitAndSidebarAreLocal) {
  EXPECT_FALSE(MahoSplitViewController::ShouldDispatchPersistentSplitEvent(
      MahoSplitPersistence::kPrivateLocal));
  EXPECT_FALSE(ShouldPersistSidebarToggle(
      MahoPrivateContextClass::kPrimaryIncognito));
  EXPECT_FALSE(ShouldPersistSidebarToggle(MahoPrivateContextClass::kOtherOtr));
  EXPECT_FALSE(ShouldPersistSidebarToggle(MahoPrivateContextClass::kGuest));
}

TEST_F(MahoCommandActionHandlerTest, RegularSplitAndSearchEnginePersist) {
  EXPECT_TRUE(MahoSplitViewController::ShouldDispatchPersistentSplitEvent(
      MahoSplitPersistence::kPersistent));
  EXPECT_TRUE(ShouldPersistSidebarToggle(MahoPrivateContextClass::kRegular));
  EXPECT_FALSE(MahoCommandModel::PrivateSearchUsesDefaultProviderOnly(
      MahoPrivateContextClass::kRegular));
  EXPECT_TRUE(MahoCommandModel::ShouldReadSavedSourcesForContext(
      MahoPrivateContextClass::kRegular));
}

}  // namespace
}  // namespace maho
