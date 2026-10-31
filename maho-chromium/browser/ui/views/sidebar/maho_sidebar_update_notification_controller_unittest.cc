// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_update_notification_controller.h"

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/test/bind.h"
#include "maho/browser/updates/maho_product_version.h"
#include "content/public/test/browser_task_environment.h"
#include "chrome/test/base/testing_browser_process.h"
#include "components/prefs/testing_pref_service.h"
#include "maho/browser/updates/maho_update_manager.h"
#include "maho/browser/updates/maho_update_pref_names.h"
#include "maho/browser/updates/maho_update_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

class MahoSidebarUpdateNotificationControllerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto* local_state =
        TestingBrowserProcess::GetGlobal()->GetTestingLocalState();
    if (!local_state->FindPreference(prefs::kMahoUpdateInstallId)) {
      updates::RegisterLocalStatePrefs(local_state->registry());
    }
    local_state->SetBoolean(prefs::kMahoUpdateEnabled, true);
    local_state->ClearPref(prefs::kMahoUpdateLastSeenVersion);
    local_state->ClearPref(prefs::kMahoUpdateCrashSentinel);
    MahoUpdateManager::GetInstance()->ResetForTesting();
  }

  void TearDown() override {
    MahoUpdateManager::GetInstance()->ResetForTesting();
  }

  content::BrowserTaskEnvironment task_environment_;
};

TEST_F(MahoSidebarUpdateNotificationControllerTest,
       ShowsOnReadyToInstallForFreshVersion) {
  int callback_count = 0;
  MahoSidebarUpdateNotificationController controller(
      nullptr, base::BindLambdaForTesting([&] { ++callback_count; }));

  MahoUpdateManager::GetInstance()->TransitionToState(
      UpdateState::kReadyToInstall);

  auto model = controller.BuildModel();
  EXPECT_TRUE(model.visible);
  EXPECT_FALSE(model.heading.empty());
  EXPECT_TRUE(model.checkbox.has_value());
  EXPECT_EQ(model.actions.size(), 1u);
  EXPECT_TRUE(model.actions[0].special);
  EXPECT_GE(callback_count, 1);
}

TEST_F(MahoSidebarUpdateNotificationControllerTest,
       HiddenWhenLastSeenVersionEqualsCurrent) {
  TestingBrowserProcess::GetGlobal()->GetTestingLocalState()->SetString(
      prefs::kMahoUpdateLastSeenVersion,
      std::string(updates::kMahoProductVersion));

  MahoSidebarUpdateNotificationController controller(
      nullptr, base::DoNothing());
  MahoUpdateManager::GetInstance()->TransitionToState(
      UpdateState::kReadyToInstall);

  auto model = controller.BuildModel();
  EXPECT_FALSE(model.visible);
}

TEST_F(MahoSidebarUpdateNotificationControllerTest,
       DismissPersistsLastSeenVersion) {
  MahoSidebarUpdateNotificationController controller(
      nullptr, base::DoNothing());
  MahoUpdateManager::GetInstance()->TransitionToState(
      UpdateState::kReadyToInstall);

  EXPECT_TRUE(controller.BuildModel().visible);

  controller.Dismiss();

  EXPECT_TRUE(controller.dismissed_in_session_for_testing());
  EXPECT_EQ(TestingBrowserProcess::GetGlobal()
                ->GetTestingLocalState()
                ->GetString(prefs::kMahoUpdateLastSeenVersion),
            std::string(updates::kMahoProductVersion));
  EXPECT_FALSE(controller.BuildModel().visible);
}

TEST_F(MahoSidebarUpdateNotificationControllerTest,
       SimulateModeBypassesVersionCheck) {
  base::CommandLine::ForCurrentProcess()->AppendSwitchASCII(
      "maho-simulate-update-state", "ready_to_install");
  TestingBrowserProcess::GetGlobal()->GetTestingLocalState()->SetString(
      prefs::kMahoUpdateLastSeenVersion,
      std::string(updates::kMahoProductVersion));

  auto* manager = MahoUpdateManager::GetInstance();
  manager->Initialize(nullptr, TestingBrowserProcess::GetGlobal()->GetTestingLocalState());
  ASSERT_TRUE(manager->is_simulating_for_testing());

  MahoSidebarUpdateNotificationController controller(
      nullptr, base::DoNothing());

  auto model = controller.BuildModel();
  EXPECT_TRUE(model.visible);

  base::CommandLine::ForCurrentProcess()->RemoveSwitch(
      "maho-simulate-update-state");
}

TEST_F(MahoSidebarUpdateNotificationControllerTest,
       KillSwitchSuppressesNotification) {
  TestingBrowserProcess::GetGlobal()->GetTestingLocalState()->SetBoolean(
      prefs::kMahoUpdateEnabled, false);

  auto* manager = MahoUpdateManager::GetInstance();
  manager->Initialize(nullptr, TestingBrowserProcess::GetGlobal()->GetTestingLocalState());
  // Kill-switch makes Initialize early-return; state stays kIdle, no
  // delegate runs, so kReadyToInstall is unreachable from production flow.
  EXPECT_EQ(manager->GetState(), UpdateState::kIdle);

  MahoSidebarUpdateNotificationController controller(
      nullptr, base::DoNothing());

  auto model = controller.BuildModel();
  EXPECT_FALSE(model.visible);
}

TEST_F(MahoSidebarUpdateNotificationControllerTest,
       NonReadyStatesDoNotShow) {
  MahoSidebarUpdateNotificationController controller(
      nullptr, base::DoNothing());

  for (UpdateState s : {UpdateState::kIdle, UpdateState::kChecking,
                        UpdateState::kUpdateAvailable,
                        UpdateState::kDownloading, UpdateState::kVerifying,
                        UpdateState::kUpToDate, UpdateState::kError}) {
    MahoUpdateManager::GetInstance()->ResetForTesting();
    TestingBrowserProcess::GetGlobal()->GetTestingLocalState()->SetBoolean(
        prefs::kMahoUpdateEnabled, true);
    TestingBrowserProcess::GetGlobal()->GetTestingLocalState()->ClearPref(
        prefs::kMahoUpdateLastSeenVersion);
    controller.clear_session_dismissed_for_testing();
    MahoUpdateManager::GetInstance()->TransitionToState(s);
    auto model = controller.BuildModel();
    EXPECT_FALSE(model.visible)
        << "state=" << static_cast<int>(s);
  }
}

}  // namespace
}  // namespace maho
