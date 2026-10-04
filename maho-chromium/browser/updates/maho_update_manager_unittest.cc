// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/maho_update_manager.h"

#include "base/command_line.h"
#include "base/environment.h"
#include "base/functional/bind.h"
#include "content/public/test/browser_task_environment.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "components/prefs/testing_pref_service.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "components/version_info/version_info.h"
#include "maho/browser/updates/crash_loop_sentinel.h"
#include "maho/browser/updates/maho_product_version.h"
#include "maho/browser/updates/maho_update_pref_names.h"
#include "maho/browser/updates/maho_update_prefs.h"
#include "maho/browser/updates/maho_update_server_url.h"
#include "maho/browser/updates/platform_updater_delegate.h"
#include "maho/browser/updates/platform_updater_error_mapping.h"
#include "maho/browser/updates/rollout_bucket.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

class MockPlatformUpdaterDelegate : public PlatformUpdaterDelegate {
 public:
  MockPlatformUpdaterDelegate() = default;
  ~MockPlatformUpdaterDelegate() override = default;

  void Initialize() override { ++init_count; }
  void Check(bool manual_check) override {
    ++check_count;
    last_manual = manual_check;
  }
  void SetChannel(const std::string& channel_name) override {
    ++set_channel_count;
    last_channel = channel_name;
  }
  void ApplyUpdateAndRestart() override { ++apply_count; }
  std::string GetUpdateGuidance() const override { return std::string(); }
  void SetVersionHistoryHandler(base::RepeatingClosure handler) override {
    ++set_version_history_handler_count;
    version_history_handler = std::move(handler);
  }

  int init_count = 0;
  int check_count = 0;
  int set_channel_count = 0;
  int apply_count = 0;
  int set_version_history_handler_count = 0;
  bool last_manual = false;
  std::string last_channel;
  base::RepeatingClosure version_history_handler;
};

class TestUpdateObserver : public MahoUpdateObserver {
 public:
  void OnUpdateStateChanged(UpdateState state) override {
    last_state = state;
    ++state_changed_count;
  }
  void OnUpdateProgress(double percent) override {
    last_progress = percent;
    ++progress_changed_count;
  }

  UpdateState last_state = UpdateState::kIdle;
  int state_changed_count = 0;
  double last_progress = 0.0;
  int progress_changed_count = 0;
};

class MahoUpdateManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto* local_state =
        TestingBrowserProcess::GetGlobal()->GetTestingLocalState();
    if (!local_state->FindPreference(prefs::kMahoUpdateInstallId)) {
      updates::RegisterLocalStatePrefs(local_state->registry());
    }
    local_state->SetBoolean(prefs::kMahoUpdateEnabled, true);
    local_state->ClearPref(prefs::kMahoUpdateCrashSentinel);
    local_state->ClearPref(prefs::kMahoUpdateRollbackRequested);
    MahoUpdateManager::GetInstance()->ResetForTesting();
  }

  void TearDown() override {
    MahoUpdateManager::GetInstance()->ResetForTesting();
  }

  content::BrowserTaskEnvironment task_environment_{
      content::BrowserTaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(MahoUpdateManagerTest, GuidanceDefaultsToEmpty) {
  auto* manager = MahoUpdateManager::GetInstance();
  EXPECT_TRUE(manager->GetUpdateGuidance().empty());
  manager->SetDelegateForTesting(
      std::make_unique<MockPlatformUpdaterDelegate>());
  EXPECT_TRUE(manager->GetUpdateGuidance().empty());
}

TEST_F(MahoUpdateManagerTest, GuidanceDelegatesWithoutEnablingApply) {
  class GuidedDelegate : public MockPlatformUpdaterDelegate {
   public:
    std::string GetUpdateGuidance() const override { return "external-update"; }
  };
  auto* manager = MahoUpdateManager::GetInstance();
  auto delegate = std::make_unique<GuidedDelegate>();
  auto* delegate_ptr = delegate.get();
  manager->SetDelegateForTesting(std::move(delegate));
  manager->TransitionToState(UpdateState::kUpdateAvailable);
  EXPECT_EQ(manager->GetUpdateGuidance(), "external-update");
  manager->ApplyUpdateAndRestart();
  EXPECT_EQ(delegate_ptr->apply_count, 0);
  manager->CheckForUpdates(true);
  EXPECT_EQ(delegate_ptr->check_count, 1);
}

#if BUILDFLAG(IS_WIN)
TEST_F(MahoUpdateManagerTest, UnpackagedWindowsDelegateCannotApplyInstaller) {
  auto* manager = MahoUpdateManager::GetInstance();
  manager->Initialize(nullptr, nullptr);
  EXPECT_FALSE(manager->GetUpdateGuidance().empty());
  manager->TransitionToState(UpdateState::kUpdateAvailable);
  manager->ApplyUpdateAndRestart();
  EXPECT_EQ(manager->GetState(), UpdateState::kUpdateAvailable);
  manager->TransitionToState(UpdateState::kReadyToInstall);
  manager->ApplyUpdateAndRestart();
  EXPECT_EQ(manager->GetState(), UpdateState::kReadyToInstall);
}
#endif

TEST_F(MahoUpdateManagerTest, VersionHistoryHandlerReachesTheDelegate) {
  auto* manager = MahoUpdateManager::GetInstance();

  // Set before any delegate exists: it must be applied when one appears.
  int early_runs = 0;
  manager->SetVersionHistoryHandler(
      base::BindRepeating([](int* runs) { ++*runs; }, &early_runs));

  auto delegate = std::make_unique<MockPlatformUpdaterDelegate>();
  auto* delegate_ptr = delegate.get();
  manager->SetDelegateForTesting(std::move(delegate));
  ASSERT_EQ(delegate_ptr->set_version_history_handler_count, 1);
  ASSERT_TRUE(delegate_ptr->version_history_handler);
  delegate_ptr->version_history_handler.Run();
  EXPECT_EQ(early_runs, 1);

  // Set after the delegate exists: forwarded immediately, replacing the old
  // handler. This is the live order -- the browser injects the handler right
  // after MahoUpdateManager::Initialize() creates the platform delegate.
  int late_runs = 0;
  manager->SetVersionHistoryHandler(
      base::BindRepeating([](int* runs) { ++*runs; }, &late_runs));
  EXPECT_EQ(delegate_ptr->set_version_history_handler_count, 2);
  delegate_ptr->version_history_handler.Run();
  EXPECT_EQ(late_runs, 1);
  EXPECT_EQ(early_runs, 1);
}

TEST_F(MahoUpdateManagerTest, NoUpdateIsNotAnUpdateFailure) {
  // Regression: Sparkle reports SUNoUpdateError through the error parameter of
  // -updater:didFinishUpdateCycleForUpdateCheck:error: when nothing newer
  // exists. The macOS delegate turned every non-nil error into kConnectionFailed
  // after updaterDidNotFindUpdate: had already set kUpToDate, so an up-to-date
  // app displayed "Update check failed".
  EXPECT_EQ(updates::ClassifyUpdateCycleError(/*is_sparkle_domain=*/true,
                                              updates::kSparkleNoUpdateError),
            updates::UpdateCycleOutcome::kNoUpdateFound);
  EXPECT_EQ(updates::UpdateErrorFromUpdaterCode(updates::kSparkleNoUpdateError),
            UpdateError::kNone);

  // Declining the authorization prompt is a user action, not a malfunction.
  EXPECT_EQ(
      updates::ClassifyUpdateCycleError(
          /*is_sparkle_domain=*/true,
          updates::kSparkleInstallationCanceledError),
      updates::UpdateCycleOutcome::kUserCanceled);

  // A code is only interpretable inside Sparkle's error domain.
  EXPECT_EQ(updates::ClassifyUpdateCycleError(/*is_sparkle_domain=*/false,
                                              updates::kSparkleNoUpdateError),
            updates::UpdateCycleOutcome::kFailure);

  // Real failures still classify as failures.
  EXPECT_EQ(updates::ClassifyUpdateCycleError(/*is_sparkle_domain=*/true,
                                              updates::kSparkleAppcastError),
            updates::UpdateCycleOutcome::kFailure);
}

TEST_F(MahoUpdateManagerTest, SparkleErrorCodesMapToMatchingUpdateErrors) {
  // Signature failures are 3001/3002. The mapping used to test 2001/2002, which
  // are download-phase codes, so a failed download was reported as a signature
  // mismatch.
  EXPECT_EQ(updates::UpdateErrorFromUpdaterCode(updates::kSparkleSignatureError),
            UpdateError::kSignatureMismatch);
  EXPECT_EQ(updates::UpdateErrorFromUpdaterCode(updates::kSparkleValidationError),
            UpdateError::kSignatureMismatch);
  EXPECT_EQ(updates::UpdateErrorFromUpdaterCode(updates::kSparkleDownloadError),
            UpdateError::kDownloadFailed);
  EXPECT_EQ(updates::UpdateErrorFromUpdaterCode(updates::kSparkleAppcastError),
            UpdateError::kConnectionFailed);
  // Install-phase and disk-image refusals have no taxonomy entry; they must not
  // be misreported as connectivity problems.
  EXPECT_EQ(updates::UpdateErrorFromUpdaterCode(/*install failure=*/4005),
            UpdateError::kUnknown);
  EXPECT_EQ(updates::UpdateErrorFromUpdaterCode(/*running from dmg=*/1003),
            UpdateError::kUnknown);
}

TEST_F(MahoUpdateManagerTest, InitializeIsIdempotent_DelegateInitsExactlyOnce) {
  auto* manager = MahoUpdateManager::GetInstance();
  auto delegate = std::make_unique<MockPlatformUpdaterDelegate>();
  auto* delegate_ptr = delegate.get();
  manager->SetDelegateForTesting(std::move(delegate));

  // SetDelegateForTesting itself runs Initialize() + SetChannel() once.
  EXPECT_EQ(delegate_ptr->init_count, 1);

  manager->Initialize(nullptr, nullptr);
  manager->Initialize(nullptr, nullptr);
  manager->Initialize(nullptr, nullptr);

  // No further delegate Initialize() calls (manager early-returns; existing
  // delegate is preserved, no platform delegate is created).
  EXPECT_EQ(delegate_ptr->init_count, 1);
}

TEST_F(MahoUpdateManagerTest, UnconfiguredLocalStateEnablesUpdatesByDefault) {
  TestingPrefServiceSimple local_state;
  updates::RegisterLocalStatePrefs(local_state.registry());
  ASSERT_FALSE(local_state.HasPrefPath(prefs::kMahoUpdateEnabled));

  auto* manager = MahoUpdateManager::GetInstance();
  auto delegate = std::make_unique<MockPlatformUpdaterDelegate>();
  auto* delegate_ptr = delegate.get();
  manager->SetDelegateForTesting(std::move(delegate));
  manager->Initialize(nullptr, &local_state);

  manager->CheckForUpdates(true);
  EXPECT_EQ(delegate_ptr->check_count, 1);
  EXPECT_TRUE(delegate_ptr->last_manual);
}

TEST_F(MahoUpdateManagerTest, KillSwitchPrevents_Initialize) {
  auto* local_state =
      TestingBrowserProcess::GetGlobal()->GetTestingLocalState();
  local_state->SetBoolean(prefs::kMahoUpdateEnabled, false);

  auto* manager = MahoUpdateManager::GetInstance();
  manager->Initialize(nullptr, local_state);

  // Disabled: CheckForUpdates is a no-op, no delegate exists.
  manager->CheckForUpdates(true);
  manager->ApplyUpdateAndRestart();
  // Manager stays in kIdle; the test asserts no crash, no observer storm.
  EXPECT_EQ(manager->GetState(), UpdateState::kIdle);
}

TEST_F(MahoUpdateManagerTest, ObserverNotifications) {
  auto* manager = MahoUpdateManager::GetInstance();
  TestUpdateObserver observer;
  manager->AddObserver(&observer);

  manager->TransitionToState(UpdateState::kChecking);
  EXPECT_EQ(observer.last_state, UpdateState::kChecking);
  EXPECT_EQ(observer.state_changed_count, 1);

  manager->NotifyProgress(42.5);
  EXPECT_EQ(observer.last_progress, 42.5);
  EXPECT_EQ(observer.progress_changed_count, 1);

  manager->RemoveObserver(&observer);
  manager->TransitionToState(UpdateState::kDownloading);
  EXPECT_EQ(observer.last_state, UpdateState::kChecking);
  EXPECT_EQ(observer.state_changed_count, 1);
}

TEST_F(MahoUpdateManagerTest, RolloutBucketDeterminism) {
  std::string id1 = "install_id_test_1";
  std::string id2 = "install_id_test_2";

  int bucket1_a = updates::RolloutBucket::Compute(id1);
  int bucket1_b = updates::RolloutBucket::Compute(id1);
  int bucket2 = updates::RolloutBucket::Compute(id2);

  EXPECT_EQ(bucket1_a, bucket1_b);
  EXPECT_GE(bucket1_a, 0);
  EXPECT_LT(bucket1_a, 100);
  EXPECT_GE(bucket2, 0);
  EXPECT_LT(bucket2, 100);
}

TEST_F(MahoUpdateManagerTest, CrashLoopSentinel_TripsAfterThreeBareStarts) {
  std::string version = "126.0.0.1";
  TestingPrefServiceSimple local_state;
  updates::RegisterLocalStatePrefs(local_state.registry());

  EXPECT_FALSE(updates::CrashLoopSentinel::CheckAndMarkStart(&local_state, version));
  EXPECT_FALSE(updates::CrashLoopSentinel::CheckAndMarkStart(&local_state, version));
  EXPECT_TRUE(updates::CrashLoopSentinel::CheckAndMarkStart(&local_state, version));

  updates::CrashLoopSentinel::MarkStable(&local_state, version);
  EXPECT_FALSE(updates::CrashLoopSentinel::CheckAndMarkStart(&local_state, version));
}

TEST_F(MahoUpdateManagerTest, CrashLoopSentinel_StableInBetweenNeverTrips) {
  std::string version = "126.0.0.1";
  TestingPrefServiceSimple local_state;
  updates::RegisterLocalStatePrefs(local_state.registry());

  // Normal usage: every start reaches stable runtime before next start.
  for (int i = 0; i < 10; ++i) {
    EXPECT_FALSE(updates::CrashLoopSentinel::CheckAndMarkStart(&local_state, version))
        << "Iteration " << i;
    updates::CrashLoopSentinel::MarkStable(&local_state, version);
  }
}

TEST_F(MahoUpdateManagerTest, CrashLoopSentinel_VersionChangeResetsCount) {
  TestingPrefServiceSimple local_state;
  updates::RegisterLocalStatePrefs(local_state.registry());

  EXPECT_FALSE(updates::CrashLoopSentinel::CheckAndMarkStart(&local_state, "1.0.0"));
  EXPECT_FALSE(updates::CrashLoopSentinel::CheckAndMarkStart(&local_state, "1.0.0"));
  // New version observed: count resets to 1, no trip.
  EXPECT_FALSE(updates::CrashLoopSentinel::CheckAndMarkStart(&local_state, "2.0.0"));
  EXPECT_FALSE(updates::CrashLoopSentinel::CheckAndMarkStart(&local_state, "2.0.0"));
}

TEST_F(MahoUpdateManagerTest, ResetForTestingClearsObservers) {
  auto* manager = MahoUpdateManager::GetInstance();
  TestUpdateObserver observer;
  manager->AddObserver(&observer);

  manager->ResetForTesting();
  manager->TransitionToState(UpdateState::kChecking);

  // Observer was removed by Reset; no notification dispatched.
  EXPECT_EQ(observer.state_changed_count, 0);
}

TEST_F(MahoUpdateManagerTest, UrlOverride_StripTrailingSlash) {
  EXPECT_EQ(updates::StripTrailingSlash("http://x:1"), "http://x:1");
  EXPECT_EQ(updates::StripTrailingSlash("http://x:1/"), "http://x:1");
  EXPECT_EQ(updates::StripTrailingSlash("http://x:1///"), "http://x:1");
}

TEST_F(MahoUpdateManagerTest, UrlOverride_RejectsFileScheme) {
  EXPECT_FALSE(updates::IsAcceptableUpdateServerUrl("file:///etc/passwd"));
  EXPECT_FALSE(updates::IsAcceptableUpdateServerUrl(""));
  EXPECT_FALSE(updates::IsAcceptableUpdateServerUrl("ftp://x"));
  EXPECT_TRUE(updates::IsAcceptableUpdateServerUrl("http://localhost:1"));
  EXPECT_TRUE(updates::IsAcceptableUpdateServerUrl("https://relay.mahobrowser.com"));
}

TEST_F(MahoUpdateManagerTest, UrlOverride_EnvVarBeatsPref) {
  TestingProfile profile;
  if (!profile.GetPrefs()->FindPreference(prefs::kMahoUpdateServerUrl)) {
    updates::RegisterProfilePrefs(profile.GetTestingPrefService()->registry());
  }
  profile.GetPrefs()->SetString(prefs::kMahoUpdateServerUrl, "http://prefhost:8888/");

  std::unique_ptr<base::Environment> env = base::Environment::Create();
  env->SetVar(updates::kMahoUpdateServerOverrideEnvVar,
              "https://envhost:9999/");

  EXPECT_EQ(updates::ResolveUpdateServerBaseUrl(&profile),
            "https://envhost:9999");

  env->UnSetVar(updates::kMahoUpdateServerOverrideEnvVar);
}

TEST_F(MahoUpdateManagerTest, UrlOverride_PrefBeatsDefault) {
  TestingProfile profile;
  if (!profile.GetPrefs()->FindPreference(prefs::kMahoUpdateServerUrl)) {
    updates::RegisterProfilePrefs(profile.GetTestingPrefService()->registry());
  }
  profile.GetPrefs()->SetString(prefs::kMahoUpdateServerUrl, "https://prefhost:8888/");

  std::unique_ptr<base::Environment> env = base::Environment::Create();
  env->UnSetVar(updates::kMahoUpdateServerOverrideEnvVar);

  EXPECT_EQ(updates::ResolveUpdateServerBaseUrl(&profile),
            "https://prefhost:8888");
}

TEST_F(MahoUpdateManagerTest, UrlOverride_DefaultWhenNothingSet) {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  env->UnSetVar(updates::kMahoUpdateServerOverrideEnvVar);
  EXPECT_EQ(updates::ResolveUpdateServerBaseUrl(nullptr),
            std::string(updates::kMahoUpdateServerDefaultUrl));
}

TEST_F(MahoUpdateManagerTest, UrlOverride_EnvFileSchemeFallsBackToDefault) {
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  env->SetVar(updates::kMahoUpdateServerOverrideEnvVar, "file:///nope");
  EXPECT_EQ(updates::ResolveUpdateServerBaseUrl(nullptr),
            std::string(updates::kMahoUpdateServerDefaultUrl));
  env->UnSetVar(updates::kMahoUpdateServerOverrideEnvVar);
}

TEST_F(MahoUpdateManagerTest, SetChannel_ReResolvesBaseUrl) {
  TestingProfile profile;
  if (!profile.GetPrefs()->FindPreference(prefs::kMahoUpdateServerUrl)) {
    updates::RegisterProfilePrefs(profile.GetTestingPrefService()->registry());
  }

  auto* manager = MahoUpdateManager::GetInstance();
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  env->UnSetVar(updates::kMahoUpdateServerOverrideEnvVar);

  auto delegate = std::make_unique<MockPlatformUpdaterDelegate>();
  manager->SetDelegateForTesting(std::move(delegate));
  manager->Initialize(&profile, nullptr);

  EXPECT_EQ(manager->GetServerBaseUrl(),
            std::string(updates::kMahoUpdateServerDefaultUrl));

  profile.GetPrefs()->SetString(prefs::kMahoUpdateServerUrl,
                                 "https://newhost:7777/");
  manager->SetChannel(UpdateChannel::kBeta);

  EXPECT_EQ(manager->GetServerBaseUrl(), "https://newhost:7777");
}

TEST_F(MahoUpdateManagerTest, AutoCheckTimer_FiresAfterInitialDelay) {
  auto* manager = MahoUpdateManager::GetInstance();
  manager->set_auto_check_interval_for_testing(base::Milliseconds(10));

  auto delegate = std::make_unique<MockPlatformUpdaterDelegate>();
  auto* delegate_ptr = delegate.get();
  manager->SetDelegateForTesting(std::move(delegate));
  manager->Initialize(nullptr, nullptr);

  EXPECT_EQ(delegate_ptr->check_count, 0);
  task_environment_.FastForwardBy(base::Milliseconds(15));
  EXPECT_GE(delegate_ptr->check_count, 1);

  task_environment_.FastForwardBy(base::Milliseconds(15));
  EXPECT_GE(delegate_ptr->check_count, 2);
}

TEST_F(MahoUpdateManagerTest, AutoCheckTimer_StoppedWhenDisabled) {
  auto* local_state =
      TestingBrowserProcess::GetGlobal()->GetTestingLocalState();
  local_state->SetBoolean(prefs::kMahoUpdateEnabled, false);

  auto* manager = MahoUpdateManager::GetInstance();
  manager->set_auto_check_interval_for_testing(base::Milliseconds(10));

  auto delegate = std::make_unique<MockPlatformUpdaterDelegate>();
  auto* delegate_ptr = delegate.get();
  manager->SetDelegateForTesting(std::move(delegate));
  manager->Initialize(nullptr, local_state);

  task_environment_.FastForwardBy(base::Seconds(60));
  EXPECT_EQ(delegate_ptr->check_count, 0);
}

TEST_F(MahoUpdateManagerTest, AutoCheckTimer_StoppedOnCrashLoop) {
  auto* local_state =
      TestingBrowserProcess::GetGlobal()->GetTestingLocalState();
  local_state->SetBoolean(prefs::kMahoUpdateEnabled, true);

  // Pre-populate the crash sentinel so the third bare start trips the loop.
  std::string version(updates::kMahoProductVersion);
  updates::CrashLoopSentinel::CheckAndMarkStart(local_state, version);
  updates::CrashLoopSentinel::CheckAndMarkStart(local_state, version);

  auto* manager = MahoUpdateManager::GetInstance();
  manager->set_auto_check_interval_for_testing(base::Milliseconds(10));

  auto delegate = std::make_unique<MockPlatformUpdaterDelegate>();
  auto* delegate_ptr = delegate.get();
  manager->SetDelegateForTesting(std::move(delegate));
  manager->Initialize(nullptr, local_state);

  // Crash-loop tripped during Initialize → state == kError(kCrashLoop), and
  // the timer must NOT have started.
  EXPECT_EQ(manager->GetState(), UpdateState::kError);
  EXPECT_EQ(manager->GetLastError(), UpdateError::kCrashLoop);

  task_environment_.FastForwardBy(base::Seconds(60));
  EXPECT_EQ(delegate_ptr->check_count, 0);
}

TEST_F(MahoUpdateManagerTest, SimulateSwitch_ForcesReadyToInstall) {
  base::CommandLine::ForCurrentProcess()->AppendSwitchASCII(
      "maho-simulate-update-state", "ready_to_install");

  auto* manager = MahoUpdateManager::GetInstance();
  auto delegate = std::make_unique<MockPlatformUpdaterDelegate>();
  auto* delegate_ptr = delegate.get();
  manager->SetDelegateForTesting(std::move(delegate));
  manager->Initialize(nullptr, nullptr);

  EXPECT_TRUE(manager->is_simulating_for_testing());
  EXPECT_EQ(manager->GetState(), UpdateState::kReadyToInstall);

  task_environment_.FastForwardBy(base::Seconds(60));
  EXPECT_EQ(delegate_ptr->check_count, 0);

  manager->CheckForUpdates(true);
  EXPECT_EQ(delegate_ptr->check_count, 0);

  base::CommandLine::ForCurrentProcess()->RemoveSwitch(
      "maho-simulate-update-state");
}

TEST_F(MahoUpdateManagerTest, SimulateSwitch_ForcesErrorCrashLoop) {
  base::CommandLine::ForCurrentProcess()->AppendSwitchASCII(
      "maho-simulate-update-state", "error_crash_loop");

  auto* manager = MahoUpdateManager::GetInstance();
  manager->SetDelegateForTesting(
      std::make_unique<MockPlatformUpdaterDelegate>());
  manager->Initialize(nullptr, nullptr);

  EXPECT_TRUE(manager->is_simulating_for_testing());
  EXPECT_EQ(manager->GetState(), UpdateState::kError);
  EXPECT_EQ(manager->GetLastError(), UpdateError::kCrashLoop);

  base::CommandLine::ForCurrentProcess()->RemoveSwitch(
      "maho-simulate-update-state");
}

TEST_F(MahoUpdateManagerTest, SimulateSwitch_UnknownValueIsIgnored) {
  base::CommandLine::ForCurrentProcess()->AppendSwitchASCII(
      "maho-simulate-update-state", "completely-bogus");

  auto* manager = MahoUpdateManager::GetInstance();
  manager->set_auto_check_interval_for_testing(base::Milliseconds(10));
  auto delegate = std::make_unique<MockPlatformUpdaterDelegate>();
  auto* delegate_ptr = delegate.get();
  manager->SetDelegateForTesting(std::move(delegate));
  manager->Initialize(nullptr, nullptr);

  EXPECT_FALSE(manager->is_simulating_for_testing());
  EXPECT_EQ(manager->GetState(), UpdateState::kIdle);

  task_environment_.FastForwardBy(base::Milliseconds(15));
  EXPECT_GE(delegate_ptr->check_count, 1);

  base::CommandLine::ForCurrentProcess()->RemoveSwitch(
      "maho-simulate-update-state");
}

}  // namespace
}  // namespace maho
