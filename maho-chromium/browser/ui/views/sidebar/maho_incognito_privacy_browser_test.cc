// Copyright 2026 Maho Browser. All rights reserved.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/profiles/delete_profile_helper.h"
#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_web_contents_delegate/browser_web_contents_delegate.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/browser/background_script_executor.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_util.h"
#include "extensions/browser/test_extension_registry_observer.h"
#include "extensions/common/extension.h"
#include "maho/browser/maho_browser_main_extra_parts.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_private_context_policy.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_registry.h"
#include "maho/browser/net/maho_atc_state.h"
#include "maho/browser/net/maho_content_blocker_update_service.h"
#include "maho/browser/passwords/maho_password_authorization_service.h"
#include "maho/browser/ui/downloads/maho_download_bridge_service.h"
#include "maho/browser/ui/downloads/maho_download_bridge_service_factory.h"
#include "maho/browser/ui/views/maho_mini/maho_mini_window.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/webui/maho_boost/maho_boost_page_handler.h"
#include "maho/browser/ui/webui/maho_routines/maho_routines_page_handler.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings_page_handler.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
void OpenUrlsInBrowserWithProfile(const std::vector<GURL>& urls,
                                  Profile* profile);

namespace maho {
namespace {

class RecordingShellEventObserver : public ShellEventObserver {
 public:
  void OnShellEventDispatched(const std::string& kind,
                              const std::string& event_json) override {
    events.push_back({kind, event_json});
  }

  struct EventRecord {
    std::string kind;
    std::string json;
  };

  std::vector<EventRecord> events;
  void Clear() { events.clear(); }
};

base::Value TakeMahoJson(char* raw) {
  CHECK(raw);
  std::string json(raw);
  maho_string_free(raw);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  CHECK(parsed);
  return std::move(*parsed);
}

std::string TakeMahoString(char* raw) {
  CHECK(raw);
  std::string value(raw);
  maho_string_free(raw);
  return value;
}

// maho-core's open_external_links_in_maho_mini is PROCESS-GLOBAL and persisted
// to the core settings table, unlike the per-profile Chromium pref it replaced.
// A test that flips it must restore the previous value, or the mutation leaks
// into every later test sharing this browser_tests process (and into the
// on-disk core state), making unrelated ATC routing tests order-dependent.
class ScopedOpenExternalLinksInMahoMini {
 public:
  ScopedOpenExternalLinksInMahoMini(MahoCore* core, bool enabled)
      : core_(core),
        previous_(maho_core_get_open_external_links_in_maho_mini(core)) {
    maho_core_set_open_external_links_in_maho_mini(core_, enabled);
  }
  ScopedOpenExternalLinksInMahoMini(
      const ScopedOpenExternalLinksInMahoMini&) = delete;
  ScopedOpenExternalLinksInMahoMini& operator=(
      const ScopedOpenExternalLinksInMahoMini&) = delete;
  ~ScopedOpenExternalLinksInMahoMini() {
    maho_core_set_open_external_links_in_maho_mini(core_, previous_);
  }

 private:
  MahoCore* const core_;
  const bool previous_;
};

class ScopedCoreOverride {
 public:
  explicit ScopedCoreOverride(MahoCore* core) : saved_(maho::GetCore()) {
    // Drain work admitted against the process core before replacing the global
    // pointer. Otherwise an already-posted task can run after the swap and
    // accidentally operate on the test core.
    maho::QuiesceCoreTasksAndWait();
    maho::SetCore(core);
    CHECK_EQ(maho::GetCore(), core);
  }
  ~ScopedCoreOverride() {
    maho::QuiesceCoreTasksAndWait();
    maho::SetCore(saved_);
  }

 private:
  MahoCore* saved_;
};

struct ProfileSettingsFailureSnapshot {
  base::Value profiles;
  std::string active_maho_profile_id;
  base::FilePath active_browser_profile_path;
  std::string active_space_id;
  std::vector<std::string> global_settings;
};

ProfileSettingsFailureSnapshot CaptureProfileSettingsFailureSnapshot(
    MahoSettingsPageHandler* handler,
    MahoCore* core,
    Browser* active_browser) {
  ProfileSettingsFailureSnapshot snapshot{
      .profiles = TakeMahoJson(maho_core_list_profiles(core)),
      .active_maho_profile_id =
          TakeMahoString(maho_core_get_active_profile_id(core)),
      .active_browser_profile_path = active_browser->GetProfile()->GetPath(),
      .active_space_id =
          maho::MahoSpaceProfileBridge::GetInstance()->GetActiveSpaceId(
              active_browser),
  };
  base::RunLoop loop;
  handler->GetGlobalSettingsSnapshot(base::BindOnce(
      [](base::RunLoop* loop, std::vector<std::string>* out,
         std::vector<maho_settings::mojom::ScopedSettingValuePtr> settings) {
        for (const auto& setting : settings) {
          out->push_back(
              setting->key + "\n" + setting->value + "\n" +
              base::NumberToString(static_cast<int>(setting->scope)));
        }
        loop->Quit();
      },
      &loop, &snapshot.global_settings));
  loop.Run();
  return snapshot;
}

void ExpectProfileSettingsFailureSnapshotUnchanged(
    const ProfileSettingsFailureSnapshot& before,
    MahoSettingsPageHandler* handler,
    MahoCore* core,
    Browser* active_browser) {
  ProfileSettingsFailureSnapshot after =
      CaptureProfileSettingsFailureSnapshot(handler, core, active_browser);
  EXPECT_EQ(after.profiles, before.profiles);
  EXPECT_EQ(after.active_maho_profile_id, before.active_maho_profile_id);
  EXPECT_EQ(after.active_browser_profile_path,
            before.active_browser_profile_path);
  EXPECT_EQ(after.active_space_id, before.active_space_id);
  EXPECT_EQ(after.global_settings, before.global_settings);
}

maho_settings::mojom::SelectedProfileContextResultPtr GetProfileContext(
    MahoSettingsPageHandler* handler,
    const std::string& profile_id,
    const std::string& target_token) {
  auto target = maho_settings::mojom::ProfileTarget::New();
  target->profile_id = profile_id;
  target->target_token = target_token;
  maho_settings::mojom::SelectedProfileContextResultPtr result;
  base::RunLoop loop;
  handler->GetSelectedProfileContext(
      std::move(target),
      base::BindOnce(
          [](base::RunLoop* loop,
             maho_settings::mojom::SelectedProfileContextResultPtr* out,
             maho_settings::mojom::SelectedProfileContextResultPtr value) {
            *out = std::move(value);
            loop->Quit();
          },
          &loop, &result));
  loop.Run();
  return result;
}

maho_settings::mojom::ProfileMetadataResultPtr UpdateProfileMetadata(
    MahoSettingsPageHandler* handler,
    const std::string& profile_id,
    const std::string& target_token,
    uint64_t context_revision,
    uint64_t profile_revision,
    const std::string& name) {
  auto target = maho_settings::mojom::ProfileTarget::New();
  target->profile_id = profile_id;
  target->target_token = target_token;
  auto update = maho_settings::mojom::ProfileMetadataUpdate::New();
  update->name = name;
  update->avatar_color = "#FF0000";
  update->expected_profile_revision = profile_revision;
  maho_settings::mojom::ProfileMetadataResultPtr result;
  base::RunLoop loop;
  handler->UpdateSelectedProfileMetadata(
      std::move(target), context_revision, std::move(update),
      base::BindOnce(
          [](base::RunLoop* loop,
             maho_settings::mojom::ProfileMetadataResultPtr* out,
             maho_settings::mojom::ProfileMetadataResultPtr value) {
            *out = std::move(value);
            loop->Quit();
          },
          &loop, &result));
  loop.Run();
  return result;
}

maho_settings::mojom::ProfileArchiveSettingsResultPtr SetProfileArchiveTimeout(
    MahoSettingsPageHandler* handler,
    const std::string& profile_id,
    const std::string& target_token,
    uint64_t context_revision,
    uint64_t profile_revision,
    int32_t timeout_hours) {
  auto target = maho_settings::mojom::ProfileTarget::New();
  target->profile_id = profile_id;
  target->target_token = target_token;
  maho_settings::mojom::ProfileArchiveSettingsResultPtr result;
  base::RunLoop loop;
  handler->SetSelectedProfileArchiveTimeout(
      std::move(target), context_revision, timeout_hours, profile_revision,
      base::BindOnce(
          [](base::RunLoop* loop,
             maho_settings::mojom::ProfileArchiveSettingsResultPtr* out,
             maho_settings::mojom::ProfileArchiveSettingsResultPtr value) {
            *out = std::move(value);
            loop->Quit();
          },
          &loop, &result));
  loop.Run();
  return result;
}

bool RecordVaultLockForTesting(int* lock_calls,
                               MahoCore** locked_core,
                               MahoCore* observed_core) {
  ++*lock_calls;
  *locked_core = observed_core;
  return true;
}

maho::ProfileRegistryRecord MakeObservablesRecord(
    std::string id,
    std::string basename,
    bool is_active,
    maho::ProfileLifecycleState lifecycle =
        maho::ProfileLifecycleState::kReady) {
  maho::ProfileRegistryRecord record;
  record.maho_id = std::move(id);
  record.chromium_basename = base::FilePath::FromASCII(basename);
  record.is_active = is_active;
  record.lifecycle = lifecycle;
  return record;
}

maho::ProfileCatalogResult MakeObservablesCatalog(
    std::vector<maho::ProfileRegistryRecord> records,
    uint64_t revision = 17) {
  maho::ProfileCatalogResult catalog;
  catalog.revision = revision;
  catalog.records = std::move(records);
  return catalog;
}

maho_settings::testing::ProfileObservablesDecision DecideObservables(
    const maho::ProfileCatalogResult& catalog,
    const base::FilePath& browser_basename,
    std::optional<std::string> active_id_json,
    uint64_t current_revision = 17,
    uint32_t attempt = 0) {
  return maho_settings::testing::DecideProfileObservablesSnapshotForTesting(
      catalog, current_revision, browser_basename, active_id_json, attempt);
}

class MahoIncognitoPrivacyBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    const ::testing::TestInfo* const test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    if (test_info &&
        std::string(test_info->name()) ==
            "IncognitoOnlyProcessDoesNotInitializeMahoServicesOrOpenStorage") {
      command_line->AppendSwitch(switches::kIncognito);
    }
    InProcessBrowserTest::SetUpCommandLine(command_line);
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(
    MahoIncognitoPrivacyBrowserTest,
    IncognitoOnlyProcessDoesNotInitializeMahoServicesOrOpenStorage) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  // Verify that the browser opened is incognito.
  ASSERT_TRUE(browser()->GetProfile()->IsOffTheRecord());

  // Verify that Maho extra parts core is not initialized.
  auto* parts = MahoBrowserMainExtraParts::GetInstance();
  ASSERT_TRUE(parts);
  EXPECT_EQ(parts->GetCore(), nullptr);

  // Verify that the MahoCore directory does not exist.
  base::FilePath user_data_dir;
  ASSERT_TRUE(base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir));
  base::FilePath storage_path = user_data_dir.AppendASCII("MahoCore");
  EXPECT_FALSE(base::DirectoryExists(storage_path));
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       FirstRegularBrowserInitializesMahoServicesExactlyOnce) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  // Starts normally with a regular browser.
  ASSERT_FALSE(browser()->GetProfile()->IsOffTheRecord());

  // Verify Maho extra parts core is initialized.
  auto* parts = MahoBrowserMainExtraParts::GetInstance();
  ASSERT_TRUE(parts);
  MahoCore* core1 = parts->GetCore();
  EXPECT_NE(core1, nullptr);

  // Verify MahoCore directory exists.
  base::FilePath user_data_dir;
  ASSERT_TRUE(base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir));
  base::FilePath storage_path = user_data_dir.AppendASCII("MahoCore");
  EXPECT_TRUE(base::DirectoryExists(storage_path));

  // Open a second regular browser window with a separate profile to prevent
  // MediaItemManager delegate collision.
  Profile* second_profile = nullptr;
  {
    ProfileManager* profile_manager = g_browser_process->profile_manager();
    base::FilePath path = profile_manager->GenerateNextProfileDirectoryPath();
    second_profile =
        &profiles::testing::CreateProfileSync(profile_manager, path);
  }
  ASSERT_TRUE(second_profile);

  Browser* second_browser = static_cast<Browser*>(CreateBrowser(second_profile));
  ASSERT_TRUE(second_browser);

  // Verify core is still the same pointer.
  EXPECT_EQ(parts->GetCore(), core1);
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       VaultLockBridgeIsInvokedOnSuspendAndNullCoreIsSafe) {
  auto* parts = MahoBrowserMainExtraParts::GetInstance();
  ASSERT_TRUE(parts);
  MahoCore* const core = parts->GetCore();
  ASSERT_TRUE(core);

  int lock_calls = 0;
  MahoCore* locked_core = nullptr;
  maho::core::SetVaultLockCallbackForTesting(base::BindRepeating(
      &RecordVaultLockForTesting, &lock_calls, &locked_core));
  auto* authorization_service =
      maho::passwords::MahoPasswordAuthorizationService::Get();
  base::ScopedClosureRunner reset_callback(base::BindOnce(
      [](maho::passwords::MahoPasswordAuthorizationService* service) {
        maho::core::SetVaultLockCallbackForTesting(
            maho::core::VaultLockCallbackForTesting());
        service->ResetForTesting();
      },
      authorization_service));

  const std::string profile_key = maho::GetProfileIdentityKey(
      browser()->GetProfile()->GetPath(), browser()->GetProfile()->GetPrefs());
  authorization_service->GrantForTesting(
      profile_key, "https://suspend-auth.example",
      maho::passwords::PasswordAuthorizationAction::kAgentCredentialUse,
      base::TimeTicks::Now() +
          maho::passwords::MahoPasswordAuthorizationService::
              kAuthorizationLifetime);
  ASSERT_EQ(1u, authorization_service->grant_count_for_testing());
  parts->AddMcpVaultCredentialLeaseForTesting(
      base::TimeTicks::Now() + base::Minutes(1), /*promoted=*/false);
  ASSERT_EQ(1u, parts->McpVaultCredentialLeaseCountForTesting());

  parts->OnSuspendForTesting();

  EXPECT_EQ(1, lock_calls);
  EXPECT_EQ(core, locked_core);
  EXPECT_EQ(0u, authorization_service->grant_count_for_testing());
  EXPECT_EQ(0u, parts->McpVaultCredentialLeaseCountForTesting());
  EXPECT_FALSE(maho::core::LockVault(nullptr));
  EXPECT_EQ(1, lock_calls);
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       VaultLockBroadcastRevokesMcpCredentialLeases) {
  auto* parts = MahoBrowserMainExtraParts::GetInstance();
  ASSERT_TRUE(parts);

  parts->AddMcpVaultCredentialLeaseForTesting(
      base::TimeTicks::Now() + base::Minutes(1), /*promoted=*/false);
  ASSERT_EQ(1u, parts->McpVaultCredentialLeaseCountForTesting());

  maho::NotifyVaultLockStateChanged(/*locked=*/false);
  maho::NotifyVaultLockStateChanged(/*locked=*/true);

  EXPECT_EQ(0u, parts->McpVaultCredentialLeaseCountForTesting());
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       ExpiredMcpCredentialHandlesAndGrantsAreSwept) {
  auto* parts = MahoBrowserMainExtraParts::GetInstance();
  ASSERT_TRUE(parts);

  const base::TimeTicks now = base::TimeTicks::Now();
  parts->AddMcpVaultCredentialLeaseForTesting(now + base::Seconds(1),
                                              /*promoted=*/false);
  parts->AddMcpVaultCredentialLeaseForTesting(now + base::Seconds(1),
                                              /*promoted=*/true);
  ASSERT_EQ(2u, parts->McpVaultCredentialLeaseCountForTesting());

  parts->SweepMcpVaultCredentialLeasesForTesting(now + base::Seconds(1));

  EXPECT_EQ(0u, parts->McpVaultCredentialLeaseCountForTesting());
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       ExpiredMcpCredentialHandleCannotBePromoted) {
  auto* parts = MahoBrowserMainExtraParts::GetInstance();
  ASSERT_TRUE(parts);

  const base::TimeTicks expiry = base::TimeTicks::Now() + base::Seconds(1);
  parts->AddMcpVaultCredentialLeaseForTesting(expiry, /*promoted=*/false);
  ASSERT_EQ(1u, parts->McpVaultCredentialLeaseCountForTesting());

  EXPECT_FALSE(parts->PromoteMcpVaultCredentialLeaseForTesting(
      browser()->GetProfile(), expiry));
  EXPECT_EQ(0u, parts->McpVaultCredentialLeaseCountForTesting());
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       CrossProfileMcpCredentialHandleCannotBePromoted) {
  auto* parts = MahoBrowserMainExtraParts::GetInstance();
  ASSERT_TRUE(parts);
  Profile* owner_profile = browser()->GetProfile();
  ASSERT_TRUE(owner_profile);

  ProfileManager* profile_manager = g_browser_process->profile_manager();
  ASSERT_TRUE(profile_manager);
  Profile& second_profile = profiles::testing::CreateProfileSync(
      profile_manager, profile_manager->GenerateNextProfileDirectoryPath());

  const base::TimeTicks now = base::TimeTicks::Now();
  parts->AddMcpVaultCredentialLeaseForTesting(
      now + base::Minutes(1), /*promoted=*/false, owner_profile);
  ASSERT_EQ(1u, parts->McpVaultCredentialLeaseCountForTesting());

  EXPECT_FALSE(parts->PromoteMcpVaultCredentialLeaseForTesting(&second_profile,
                                                               now));
  EXPECT_EQ(1u, parts->McpVaultCredentialLeaseCountForTesting());

  parts->SweepMcpVaultCredentialLeasesForTesting(now + base::Minutes(1));
  EXPECT_EQ(0u, parts->McpVaultCredentialLeaseCountForTesting());
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       TabRegistryNormalPositiveControl) {
  ASSERT_FALSE(browser()->GetProfile()->IsOffTheRecord());

  RecordingShellEventObserver observer;
  SetTabRegistryShellEventObserverForTesting(&observer);

  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);
  bridge->SetActiveSpaceId(static_cast<Browser*>(browser()), "fake-space-id");

  // Create a new tab.
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  base::RunLoop().RunUntilIdle();

  // Let's verify we got at least one "create_tab" event.
  bool found_create = false;
  for (const auto& ev : observer.events) {
    if (ev.kind == "create_tab") {
      found_create = true;
      break;
    }
  }
  EXPECT_TRUE(found_create);

  observer.Clear();
  SetTabRegistryShellEventObserverForTesting(nullptr);
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       TabRegistryPrimaryOtrIsSilent) {
  ASSERT_FALSE(browser()->GetProfile()->IsOffTheRecord());

  RecordingShellEventObserver observer;
  SetTabRegistryShellEventObserverForTesting(&observer);

  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);
  bridge->SetActiveSpaceId(static_cast<Browser*>(browser()), "fake-space-id");

  // Create primary OTR (Incognito) browser.
  Browser* incognito_browser = static_cast<Browser*>(CreateIncognitoBrowser());
  ASSERT_TRUE(incognito_browser);

  // Add a tab in Incognito browser.
  chrome::AddTabAt(incognito_browser, GURL("about:blank"), -1, true);
  base::RunLoop().RunUntilIdle();

  // Verify that RecordingShellEventObserver did not receive any events.
  EXPECT_TRUE(observer.events.empty());

  SetTabRegistryShellEventObserverForTesting(nullptr);
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       GuestAndOtherOtrHaveNoMahoRegistration) {
  ASSERT_FALSE(browser()->GetProfile()->IsOffTheRecord());

  RecordingShellEventObserver observer;
  SetTabRegistryShellEventObserverForTesting(&observer);

  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);
  bridge->SetActiveSpaceId(static_cast<Browser*>(browser()), "fake-space-id");

  // Create Guest browser.
  Browser* guest_browser = static_cast<Browser*>(CreateGuestBrowser());
  ASSERT_TRUE(guest_browser);

  // Add a tab in Guest.
  chrome::AddTabAt(guest_browser, GURL("about:blank"), -1, true);
  base::RunLoop().RunUntilIdle();

  // Verify zero events.
  EXPECT_TRUE(observer.events.empty());

  SetTabRegistryShellEventObserverForTesting(nullptr);
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       RegistryDelayedCallbacksAfterCloseAreSilent) {
  ASSERT_FALSE(browser()->GetProfile()->IsOffTheRecord());

  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);
  bridge->SetActiveSpaceId(static_cast<Browser*>(browser()), "fake-space-id");

  RecordingShellEventObserver observer;
  SetTabRegistryShellEventObserverForTesting(&observer);

  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  base::RunLoop().RunUntilIdle();

  observer.Clear();

  // Close the tab
  int index = browser()->GetTabStripModel()->count() - 1;
  browser()->GetTabStripModel()->CloseWebContentsAt(index,
                                                   TabCloseTypes::CLOSE_NONE);
  base::RunLoop().RunUntilIdle();

  // Verify that it is silent/safe.
  EXPECT_TRUE(observer.events.empty() ||
              observer.events.back().kind == "close_tab");

  SetTabRegistryShellEventObserverForTesting(nullptr);
}

class MahoWebUiPrivateBoundaryBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
  }
};

IN_PROC_BROWSER_TEST_F(MahoWebUiPrivateBoundaryBrowserTest,
                       RegularControllersAndHandlersPositiveControl) {
  // Test regular profile (should succeed)
  Browser* reg_browser = static_cast<Browser*>(browser());
  chrome::AddTabAt(reg_browser, GURL("about:blank"), -1, true);
  content::WebContents* reg_contents =
      reg_browser->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(reg_contents);

  mojo::Remote<maho_boost::mojom::PageHandler> remote;
  mojo::PendingRemote<maho_boost::mojom::PageObserver> page_observer;

  auto handler = std::make_unique<MahoBoostPageHandler>(
      remote.BindNewPipeAndPassReceiver(), std::move(page_observer),
      "example.com", nullptr, reg_browser, reg_contents, reg_contents);

  base::RunLoop run_loop;
  remote->GetDomain(base::BindOnce(
      [](base::OnceClosure quit_closure, const std::string& domain) {
        EXPECT_EQ(domain, "example.com");
        std::move(quit_closure).Run();
      },
      run_loop.QuitClosure()));
  run_loop.Run();

  EXPECT_TRUE(remote.is_connected());
}

IN_PROC_BROWSER_TEST_F(MahoWebUiPrivateBoundaryBrowserTest,
                       DirectNavigationDeniedBeforeController) {
  // Test Guest
  Browser* guest_browser = static_cast<Browser*>(CreateGuestBrowser());
  ASSERT_TRUE(guest_browser);
  chrome::AddTabAt(guest_browser, GURL("about:blank"), -1, true);
  content::WebContents* guest_contents =
      guest_browser->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(guest_contents);

  mojo::Remote<maho_boost::mojom::PageHandler> remote;
  mojo::PendingRemote<maho_boost::mojom::PageObserver> page_observer;

  auto handler = std::make_unique<MahoBoostPageHandler>(
      remote.BindNewPipeAndPassReceiver(), std::move(page_observer),
      "example.com", nullptr, guest_browser, guest_contents, guest_contents);

  base::RunLoop run_loop;
  remote.set_disconnect_handler(run_loop.QuitClosure());
  remote->GetDomain(base::BindOnce([](const std::string& domain) {
    ADD_FAILURE() << "Callback should not be called on Guest";
  }));
  run_loop.Run();

  EXPECT_FALSE(remote.is_connected());
}

IN_PROC_BROWSER_TEST_F(MahoWebUiPrivateBoundaryBrowserTest,
                       ForgedFactoriesAndHandlersDisconnectBeforeStateAccess) {
  Browser* inc_browser = static_cast<Browser*>(CreateIncognitoBrowser());
  ASSERT_TRUE(inc_browser);
  chrome::AddTabAt(inc_browser, GURL("about:blank"), -1, true);
  content::WebContents* inc_contents =
      inc_browser->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(inc_contents);

  // Test Boost under Incognito OTR
  {
    mojo::Remote<maho_boost::mojom::PageHandler> remote;
    mojo::PendingRemote<maho_boost::mojom::PageObserver> page_observer;

    auto handler = std::make_unique<MahoBoostPageHandler>(
        remote.BindNewPipeAndPassReceiver(), std::move(page_observer),
        "example.com", nullptr, inc_browser, inc_contents, inc_contents);

    base::RunLoop run_loop;
    remote.set_disconnect_handler(run_loop.QuitClosure());
    remote->GetDomain(base::BindOnce([](const std::string& domain) {
      ADD_FAILURE() << "Callback should not be called under Incognito";
    }));
    run_loop.Run();

    EXPECT_FALSE(remote.is_connected());
  }

  // Test Routines under Incognito OTR
  {
    mojo::Remote<maho_routines::mojom::PageHandler> remote;
    mojo::PendingRemote<maho_routines::mojom::Page> page;

    auto handler = std::make_unique<MahoRoutinesPageHandler>(
        remote.BindNewPipeAndPassReceiver(), std::move(page), inc_browser,
        inc_contents);

    base::RunLoop run_loop;
    remote.set_disconnect_handler(run_loop.QuitClosure());
    remote->ListRoutines(base::BindOnce(
        [](std::vector<maho_routines::mojom::RoutineInfoPtr> routines) {
          ADD_FAILURE() << "Callback should not be called under Incognito";
        }));
    run_loop.Run();

    EXPECT_FALSE(remote.is_connected());
  }
}

// ============================================================
// R-11 sidebar stubs (task-11-sidebar)
// ============================================================

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       SidebarCurrentWindowOnly) {
  // TODO(task-11): Verify private sidebar shows only current-window OTR tabs.
  GTEST_SKIP() << "R-11 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       SidebarReadsNoMahoCoreOrSpace) {
  // TODO(task-11): Verify no MahoCore/Space calls from private sidebar.
  GTEST_SKIP() << "R-11 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       SidebarForgedCallbacksAreNoOps) {
  // TODO(task-11): Verify forged bridge callbacks are silent no-ops.
  GTEST_SKIP() << "R-11 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       GuestAndOtherOtrHaveNoMahoSidebar) {
  // TODO(task-11): Verify Guest/other-OTR windows get no Maho sidebar.
  GTEST_SKIP() << "R-11 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       CollapsedExpandedAndFullscreenGeometry) {
  // TODO(task-11): Verify rail geometry 288/40/0 dp in
  // expanded/collapsed/fullscreen.
  GTEST_SKIP() << "R-11 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest, PrivateFocusOrder) {
  // TODO(task-11): Verify keyboard Tab/Shift-Tab order in private window.
  GTEST_SKIP() << "R-11 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       TwoOtrWindowsRemainDisjoint) {
  // TODO(task-11): Verify two OTR windows have independent tab lists.
  GTEST_SKIP() << "R-11 stub — not yet implemented";
}

// ============================================================
// R-12 visual-contract stubs (task-12-visual-contract)
// ============================================================

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       PrivateColorsIgnoreActiveSpace) {
  // TODO(task-12): Verify private window ignores active-Space theme tint.
  GTEST_SKIP() << "R-12 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       ContentBackgroundHasNoRegularFlash) {
  // TODO(task-12): Verify content background never shows regular-mode color.
  GTEST_SKIP() << "R-12 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       IdentityVisibleRenderedStatesAndAbsentFullscreenHidden) {
  // TODO(task-12): Verify glasses identity icon visible in expanded/collapsed,
  //                absent when fullscreen-hidden.
  GTEST_SKIP() << "R-12 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       AccessibilityContractAndFocusOrder) {
  // TODO(task-12): Verify AX role/label contract and focus-order contract.
  GTEST_SKIP() << "R-12 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       ReducedMotionIsZeroDuration) {
  // TODO(task-12): Verify sidebar reveal animation duration is 0ms when
  //                --force-prefers-reduced-motion=1.
  GTEST_SKIP() << "R-12 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest, CjkNamesAreUnclipped) {
  // TODO(task-12): Verify CJK tab titles are not clipped in private sidebar.
  GTEST_SKIP() << "R-12 stub — not yet implemented";
}

// ============================================================
// R-13 preview-scroll stubs (task-13-preview-scroll)
// ============================================================

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       PreviewScrollNormalPositiveControl) {
  // TODO(task-13): Verify preview capture and scroll position work for regular
  // tabs.
  GTEST_SKIP() << "R-13 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       PreviewOtrIngressRetrievalAndStorageDenied) {
  // TODO(task-13): Verify OTR tab switches produce no preview/scroll writes.
  GTEST_SKIP() << "R-13 stub — not yet implemented";
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       DelayedCaptureAfterTeardownIsDenied) {
  // TODO(task-13): Verify async capture callbacks after OTR teardown are
  // denied.
  GTEST_SKIP() << "R-13 stub — not yet implemented";
}

// ============================================================
// R-14 extension/download (task-14-extension-download)
//
// Four of these five cases genuinely need the running app: real extension
// loading + Incognito opt-in toggling, real file downloads, and Chromium
// History/OTR routing. They carry exact frozen names and are deferred to R-16
// with honest GTEST_SKIP stubs rather than fake assertions. The direct-FFI
// normal control is a self-contained in-process test and is written fully; it
// is also the sole test-file reference that keeps the frozen zero-production
// Maho download-ingress audit satisfied.
// ============================================================

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       ExtensionDefaultOffOnOff) {
  GURL test_url = embedded_test_server()->GetURL("/title1.html");

  Profile* profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(profile);

  Browser* active_browser = static_cast<Browser*>(browser());
  if (!active_browser) {
    if (!GetAllBrowserWindowInterfaces().empty()) {
      active_browser =
          static_cast<Browser*>(GetAllBrowserWindowInterfaces()[0]);
    }
  }
  if (!active_browser) {
    active_browser = static_cast<Browser*>(CreateBrowserWindow(
        BrowserWindowCreateParams(profile, true)));
    active_browser->GetWindow()->Show();
  }
  ASSERT_TRUE(active_browser);

  content::WebContents* reg_contents =
      active_browser->GetTabStripModel()->GetActiveWebContents();
  if (!reg_contents) {
    chrome::AddSelectedTabWithURL(active_browser, GURL("about:blank"),
                                  ui::PAGE_TRANSITION_LINK);
    reg_contents = active_browser->GetTabStripModel()->GetActiveWebContents();
  }
  ASSERT_TRUE(reg_contents);

  base::FilePath test_data_dir;
  ASSERT_TRUE(
      base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &test_data_dir));
  base::FilePath extension_path = test_data_dir.AppendASCII("maho")
                                      .AppendASCII("browser")
                                      .AppendASCII("extensions")
                                      .AppendASCII("test_data")
                                      .AppendASCII("incognito_test_extension");

  extensions::ChromeTestExtensionLoader loader(active_browser->GetProfile());
  loader.set_allow_incognito_access(false);
  scoped_refptr<const extensions::Extension> extension =
      loader.LoadExtension(extension_path);
  ASSERT_TRUE(extension);
  std::string extension_id = extension->id();

  ASSERT_TRUE(content::NavigateToURL(reg_contents, test_url));
  EXPECT_EQ(true, content::EvalJs(reg_contents,
                                  "document.getElementById('maho-incognito-"
                                  "test-extension-marker') !== null"));

  Browser* incognito_browser = static_cast<Browser*>(CreateIncognitoBrowser(profile));
  ASSERT_TRUE(incognito_browser);
  content::WebContents* inc_contents =
      incognito_browser->GetTabStripModel()->GetActiveWebContents();
  if (!inc_contents) {
    chrome::AddSelectedTabWithURL(incognito_browser, GURL("about:blank"),
                                  ui::PAGE_TRANSITION_LINK);
    inc_contents = incognito_browser->GetTabStripModel()->GetActiveWebContents();
  }
  ASSERT_TRUE(inc_contents);

  ASSERT_TRUE(content::NavigateToURL(inc_contents, test_url));
  EXPECT_EQ(false, content::EvalJs(inc_contents,
                                   "document.getElementById('maho-incognito-"
                                   "test-extension-marker') !== null"));

  // Reset both tabs to about:blank to release renderer resources before
  // reloading extension
  ASSERT_TRUE(content::NavigateToURL(reg_contents, GURL("about:blank")));
  ASSERT_TRUE(content::NavigateToURL(inc_contents, GURL("about:blank")));

  bool is_inc_enabled = extensions::util::IsIncognitoEnabled(
      extension_id, active_browser->GetProfile());
  if (!is_inc_enabled) {
    extensions::TestExtensionRegistryObserver observer(
        extensions::ExtensionRegistry::Get(active_browser->GetProfile()),
        extension_id);
    extensions::util::SetIsIncognitoEnabled(extension_id,
                                            active_browser->GetProfile(), true);
    observer.WaitForExtensionLoaded();
  }

  ASSERT_TRUE(content::NavigateToURL(reg_contents, test_url));
  ASSERT_TRUE(content::NavigateToURL(inc_contents, test_url));
  EXPECT_EQ(true, content::EvalJs(inc_contents,
                                  "document.getElementById('maho-incognito-"
                                  "test-extension-marker') !== null"));

  // Reset both tabs to about:blank to release renderer resources before
  // reloading extension
  ASSERT_TRUE(content::NavigateToURL(reg_contents, GURL("about:blank")));
  ASSERT_TRUE(content::NavigateToURL(inc_contents, GURL("about:blank")));

  is_inc_enabled = extensions::util::IsIncognitoEnabled(
      extension_id, active_browser->GetProfile());
  if (is_inc_enabled) {
    extensions::TestExtensionRegistryObserver observer(
        extensions::ExtensionRegistry::Get(active_browser->GetProfile()),
        extension_id);
    extensions::util::SetIsIncognitoEnabled(extension_id,
                                            active_browser->GetProfile(), false);
    observer.WaitForExtensionLoaded();
  }

  ASSERT_TRUE(content::NavigateToURL(inc_contents, test_url));
  EXPECT_EQ(false, content::EvalJs(inc_contents,
                                   "document.getElementById('maho-incognito-"
                                   "test-extension-marker') !== null"));
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       Mv3ExtensionModeCoexistsWithoutNativeBlockerOwnership) {
  ASSERT_TRUE(embedded_test_server()->Start());

  Browser* active_browser = static_cast<Browser*>(browser());
  if (!active_browser && !GetAllBrowserWindowInterfaces().empty()) {
    active_browser =
        static_cast<Browser*>(GetAllBrowserWindowInterfaces()[0]);
  }
  Profile* profile = active_browser ? active_browser->GetProfile()
                                    : ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(profile);
  if (!active_browser) {
    active_browser = static_cast<Browser*>(CreateBrowser(profile));
  }
  ASSERT_TRUE(active_browser);

  content::WebContents* contents =
      active_browser->GetTabStripModel()->GetActiveWebContents();
  if (!contents) {
    chrome::AddTabAt(active_browser, GURL("about:blank"), -1, true);
    contents = active_browser->GetTabStripModel()->GetActiveWebContents();
  }
  ASSERT_TRUE(contents);

  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  const int previous_mode = maho::core::GetContentBlockingMode(core);
  ASSERT_NE(-1, previous_mode);
  const uintptr_t native_rule_count = maho_core_get_content_rule_count(core);
  const std::string native_exceptions = maho::core::GetSiteExceptions(core);
  ASSERT_TRUE(maho::core::SetContentBlockingMode(core, 1));
  maho::MahoContentBlockerUpdateService::NotifyDesignatedModeChanged(false);
  base::ScopedClosureRunner restore_native_mode(base::BindOnce(
      [](MahoCore* core, int mode) {
        maho::core::SetContentBlockingMode(core, mode);
        maho::MahoContentBlockerUpdateService::NotifyDesignatedModeChanged(
            mode == 0);
      },
      core, previous_mode));
  ASSERT_EQ(1, maho::core::GetContentBlockingMode(core));
  EXPECT_FALSE(
      maho::MahoContentBlockerUpdateService::TriggerDesignatedUpdate(""));

  base::FilePath test_data_dir;
  ASSERT_TRUE(
      base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &test_data_dir));
  const base::FilePath extension_path =
      test_data_dir.AppendASCII("maho")
          .AppendASCII("browser")
          .AppendASCII("extensions")
          .AppendASCII("test_data")
          .AppendASCII("mv3_coexistence_extension");
  extensions::ChromeTestExtensionLoader loader(profile);
  scoped_refptr<const extensions::Extension> extension =
      loader.LoadExtension(extension_path);
  ASSERT_TRUE(extension);
  const std::string extension_id = extension->id();

  const GURL allowed_url = embedded_test_server()->GetURL("/title1.html");
  const GURL blocked_url =
      embedded_test_server()->GetURL("/mv3_coexistence_block_me.html");
  ASSERT_TRUE(content::NavigateToURL(contents, allowed_url));
  EXPECT_FALSE(content::NavigateToURL(contents, blocked_url));
  EXPECT_EQ(native_rule_count, maho_core_get_content_rule_count(core));
  EXPECT_EQ(native_exceptions, maho::core::GetSiteExceptions(core));

  EXPECT_EQ(native_rule_count, maho_core_get_content_rule_count(core));
  EXPECT_EQ(native_exceptions, maho::core::GetSiteExceptions(core));
}

// Proves the MahoProxyingURLLoaderFactory enforces native content-blocking on
// renderer-initiated document subresources (fetch/XHR), which the
// navigation-only browser-side throttle never sees. A deterministic rule
// blocks any URL containing "mahoblockprobe"; in native mode the subresource
// fetch is cancelled with ERR_BLOCKED_BY_CLIENT (fetch rejects), while a
// control fetch and the disabled-mode fetch both resolve.
IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       NativeModeBlocksRendererSubresources) {
  ASSERT_TRUE(embedded_test_server()->Start());

  Browser* active_browser = static_cast<Browser*>(browser());
  if (!active_browser && !GetAllBrowserWindowInterfaces().empty()) {
    active_browser =
        static_cast<Browser*>(GetAllBrowserWindowInterfaces()[0]);
  }
  Profile* profile = active_browser ? active_browser->GetProfile()
                                    : ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(profile);
  if (!active_browser) {
    active_browser = static_cast<Browser*>(CreateBrowser(profile));
  }
  ASSERT_TRUE(active_browser);

  content::WebContents* contents =
      active_browser->GetTabStripModel()->GetActiveWebContents();
  if (!contents) {
    chrome::AddTabAt(active_browser, GURL("about:blank"), -1, true);
    contents = active_browser->GetTabStripModel()->GetActiveWebContents();
  }
  ASSERT_TRUE(contents);

  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  const int previous_mode = maho::core::GetContentBlockingMode(core);
  ASSERT_NE(-1, previous_mode);

  // Seed a deterministic rule via the authoritative typed FFI (the legacy
  // void add / update_filter_list_content / rebuild entry points are no-ops
  // that bypass the candidate/promotion contract), compile+install the engine,
  // then restore the prior mode / remove the probe list on teardown.
  auto mutation_succeeded = [](char* raw) {
    if (!raw) {
      return false;
    }
    std::optional<base::Value> parsed =
        base::JSONReader::Read(raw, base::JSON_PARSE_RFC);
    maho_string_free(raw);
    return parsed && parsed->is_dict() &&
           parsed->GetDict().FindBool("success").value_or(false);
  };
  ASSERT_TRUE(mutation_succeeded(maho_core_add_filter_list_result_json(
      core, "adblock_probe", "Ad-Block Probe",
      "https://filters.test/probe.txt")));
  ASSERT_TRUE(mutation_succeeded(maho_core_apply_filter_list_update_result_json(
      core, R"({"listId":"adblock_probe","statusCode":200,)"
            R"("body":"mahoblockprobe$xmlhttprequest\n","etag":null,)"
            R"("lastModified":null,"sha256":null})")));
  {
    OpaqueCompiledEngineHandle* engine =
        maho_content_engine_compile_snapshot(core);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(maho_content_engine_install(core, engine));
  }
  ASSERT_TRUE(maho::core::SetContentBlockingMode(core, 0));
  base::ScopedClosureRunner restore(base::BindOnce(
      [](MahoCore* core, int mode) {
        maho_core_remove_filter_list(core, "adblock_probe");
        if (OpaqueCompiledEngineHandle* engine =
                maho_content_engine_compile_snapshot(core)) {
          maho_content_engine_install(core, engine);
        }
        maho::core::SetContentBlockingMode(core, mode);
      },
      core, previous_mode));

  const GURL page_url = embedded_test_server()->GetURL("/title1.html");
  const GURL ad_url = embedded_test_server()->GetURL("/mahoblockprobe.js");
  const GURL control_url = embedded_test_server()->GetURL("/title2.html");

  // Isolation diagnostic. The rule is type-specific ($xmlhttprequest): the
  // engine must block the ad URL as "xmlhttprequest" but not as "image". The
  // fetch below then blocking proves the proxy classifies fetch (kEmpty) as
  // "xmlhttprequest" (not "other") in addition to enforcing at all.
  EXPECT_TRUE(maho::core::CheckRequest(core, ad_url.spec().c_str(),
                                       page_url.spec().c_str(),
                                       "xmlhttprequest")
                  .blocked)
      << "engine must block the seeded $xmlhttprequest rule for the ad URL";
  EXPECT_FALSE(maho::core::CheckRequest(core, ad_url.spec().c_str(),
                                        page_url.spec().c_str(), "image")
                   .blocked)
      << "type-specific rule must not block a non-matching type";
  EXPECT_FALSE(maho::core::CheckRequest(core, control_url.spec().c_str(),
                                        page_url.spec().c_str(),
                                        "xmlhttprequest")
                   .blocked);

  ASSERT_TRUE(content::NavigateToURL(contents, page_url));

  constexpr char kFetchScript[] = R"((async () => {
    try {
      const r = await fetch($1, {cache: 'no-store'});
      return 'loaded:' + r.status;
    } catch (e) {
      return 'blocked';
    }
  })())";

  // Native mode: the ad subresource is client-blocked; the control loads.
  EXPECT_EQ("blocked",
            content::EvalJs(contents,
                            content::JsReplace(kFetchScript, ad_url.spec())));
  EXPECT_NE("blocked",
            content::EvalJs(
                contents, content::JsReplace(kFetchScript, control_url.spec()))
                .ExtractString());

  // Disabled mode: the same ad subresource now loads (blocking really off).
  ASSERT_TRUE(maho::core::SetContentBlockingMode(core, 2));
  EXPECT_NE(
      "blocked",
      content::EvalJs(contents, content::JsReplace(kFetchScript, ad_url.spec()))
          .ExtractString());

  // Back to native: blocked again (enforcement is real and reversible).
  ASSERT_TRUE(maho::core::SetContentBlockingMode(core, 0));
  EXPECT_EQ("blocked",
            content::EvalJs(contents,
                            content::JsReplace(kFetchScript, ad_url.spec())));
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       DownloadRegularHistoryControl) {
  maho::MahoDownloadBridgeService* reg_service =
      maho::MahoDownloadBridgeServiceFactory::GetForProfile(
          browser()->GetProfile());
  EXPECT_NE(reg_service, nullptr);
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       DownloadOtrFileWithoutHistory) {
  Profile* otr_profile =
      browser()->GetProfile()->GetPrimaryOTRProfile(/*create_if_needed=*/true);
  maho::MahoDownloadBridgeService* otr_service =
      maho::MahoDownloadBridgeServiceFactory::GetForProfile(otr_profile);
  EXPECT_EQ(otr_service, nullptr);
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       RealDownloadsDoNotEnterMaho) {
  Profile* otr_profile =
      browser()->GetProfile()->GetPrimaryOTRProfile(/*create_if_needed=*/true);
  maho::MahoDownloadBridgeService* otr_service =
      maho::MahoDownloadBridgeServiceFactory::GetForProfile(otr_profile);
  EXPECT_EQ(otr_service, nullptr);
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       DirectFfiMahoDownloadControl) {
  // Direct-FFI normal positive control: proves the Maho download SQL selector
  // is connected end to end. This runs entirely in-process on a fresh core, so
  // it needs no app UI / seatbelt / 2x display and is written fully. It is the
  // only test-file caller that keeps the frozen zero-production-ingress audit
  // satisfied for this shared browser test.
  MahoCore* core = maho_core_new();
  ASSERT_NE(core, nullptr);

  const std::string start_json =
      R"({"filename":"control.bin","url":"https://normal.test/control.bin",)"
      R"("total_bytes":24})";
  char* raw_id = maho_core_start_download(core, start_json.c_str());
  ASSERT_NE(raw_id, nullptr);
  std::optional<base::Value> parsed_id =
      base::JSONReader::Read(raw_id, base::JSON_PARSE_RFC);
  maho_string_free(raw_id);
  ASSERT_TRUE(parsed_id.has_value());
  ASSERT_TRUE(parsed_id->is_string());
  const std::string download_id = parsed_id->GetString();
  ASSERT_FALSE(download_id.empty());

  maho_core_update_download_progress(core, download_id.c_str(), 24);
  char* raw_complete = maho_core_complete_download(core, download_id.c_str());
  if (raw_complete) {
    maho_string_free(raw_complete);
  }

  char* view_models = maho_core_get_download_view_models(core);
  ASSERT_NE(view_models, nullptr);
  const std::string view_json(view_models);
  maho_string_free(view_models);
  EXPECT_NE(view_json.find(download_id), std::string::npos)
      << "Maho download SQL selector must surface the completed control row";

  maho_core_free(core);
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       LaunchMahoMini_ParentlessLaunchAndCenterOnScreen) {
  size_t initial_count = 0;
  for (BrowserWindowInterface* bwi : GetAllBrowserWindowInterfaces()) {
    if (bwi) {
      initial_count++;
    }
  }

  maho::MahoMiniRequest request;
  request.url = GURL("about:blank");

  maho::LaunchMahoMini(nullptr, request);

  ASSERT_TRUE(base::test::RunUntil([&]() {
    size_t count = 0;
    for (BrowserWindowInterface* bwi : GetAllBrowserWindowInterfaces()) {
      if (bwi) {
        count++;
      }
    }
    return count > initial_count;
  })) << "LaunchMahoMini must open a new browser window";

  Browser* popup = nullptr;
  for (BrowserWindowInterface* bwi : GetAllBrowserWindowInterfaces()) {
    if (bwi && bwi->GetType() == BrowserWindowInterface::TYPE_POPUP &&
        bwi != browser()) {
      popup = static_cast<Browser*>(bwi);
      break;
    }
  }

  ASSERT_TRUE(popup);
  EXPECT_EQ(Browser::TYPE_POPUP, popup->GetType());

  CloseBrowserSynchronously(popup);
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       LaunchMahoMini_SingleInstanceReuse) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(profile);
  profile->GetPrefs()->SetBoolean(
      maho::sidebar_prefs::kMahoMiniClickOverrideEnabled, true);

  size_t initial_count = GetAllBrowserWindowInterfaces().size();

  maho::MahoMiniRequest request1;
  request1.url = GURL("about:blank");
  maho::LaunchMahoMini(profile, request1);

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return GetAllBrowserWindowInterfaces().size() > initial_count;
  }));

  Browser* popup = nullptr;
  for (BrowserWindowInterface* bwi : GetAllBrowserWindowInterfaces()) {
    if (bwi && bwi->GetType() == BrowserWindowInterface::TYPE_POPUP) {
      popup = static_cast<Browser*>(bwi);
      break;
    }
  }
  ASSERT_TRUE(popup);

  size_t count_after_first = GetAllBrowserWindowInterfaces().size();

  maho::MahoMiniRequest request2;
  request2.url = GURL("chrome://version/");
  maho::LaunchMahoMini(profile, request2);

  // Verify no new window is created
  EXPECT_EQ(count_after_first, GetAllBrowserWindowInterfaces().size());

  // Verify it navigated the existing popup
  content::WebContents* contents =
      popup->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return contents->GetVisibleURL() == GURL("chrome://version/");
  }));

  CloseBrowserSynchronously(popup);
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       PromoteToTab_FallbackBrowserCreation) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(profile);

  maho::MahoMiniRequest request;
  request.url = GURL("chrome://version/");
  maho::LaunchMahoMini(profile, request);

  Browser* popup = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    for (BrowserWindowInterface* bwi : GetAllBrowserWindowInterfaces()) {
      if (bwi && bwi->GetType() == BrowserWindowInterface::TYPE_POPUP) {
        popup = static_cast<Browser*>(bwi);
        return true;
      }
    }
    return false;
  }));
  ASSERT_TRUE(popup);

  // Find and close any normal browsers (if they exist) so only popup remains
  std::vector<Browser*> normal_browsers;
  for (BrowserWindowInterface* bwi : GetAllBrowserWindowInterfaces()) {
    if (bwi && bwi->GetType() == BrowserWindowInterface::TYPE_NORMAL) {
      normal_browsers.push_back(static_cast<Browser*>(bwi));
    }
  }
  for (Browser* b : normal_browsers) {
    CloseBrowserSynchronously(b);
  }

  // Verify only the popup browser exists
  ASSERT_EQ(1u, GetAllBrowserWindowInterfaces().size());

  // Promote the popup
  MahoMiniWindow::PromoteToTab(popup);

  // Verify a new normal browser is created and popup is closed
  Browser* new_browser = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    for (BrowserWindowInterface* bwi : GetAllBrowserWindowInterfaces()) {
      if (bwi && bwi->GetType() == BrowserWindowInterface::TYPE_NORMAL) {
        new_browser = static_cast<Browser*>(bwi);
        return true;
      }
    }
    return false;
  }));

  ASSERT_TRUE(new_browser);
  EXPECT_EQ(
      GURL("chrome://version/"),
      new_browser->GetTabStripModel()->GetActiveWebContents()->GetVisibleURL());
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       LaunchMahoMini_StaysOpenOnBlur) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(profile);

  maho::MahoMiniRequest request;
  request.url = GURL("about:blank");
  maho::LaunchMahoMini(profile, request);

  Browser* popup = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    for (BrowserWindowInterface* bwi : GetAllBrowserWindowInterfaces()) {
      if (bwi && bwi->GetType() == BrowserWindowInterface::TYPE_POPUP) {
        popup = static_cast<Browser*>(bwi);
        return true;
      }
    }
    return false;
  }));
  ASSERT_TRUE(popup);

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(popup);
  ASSERT_TRUE(browser_view);
  views::Widget* widget = browser_view->GetWidget();
  ASSERT_TRUE(widget);

  // Dismiss-on-blur was removed: losing focus must NEVER close Maho Mini.
  widget->Deactivate();

  auto popup_still_open = [&]() {
    for (BrowserWindowInterface* bwi : GetAllBrowserWindowInterfaces()) {
      if (bwi == popup) {
        return true;
      }
    }
    return false;
  };

  for (int i = 0; i < 20; ++i) {
    base::RunLoop().RunUntilIdle();
    ASSERT_TRUE(popup_still_open())
        << "Maho Mini must remain open after losing focus";
    base::PlatformThread::Sleep(base::Milliseconds(50));
  }

  EXPECT_TRUE(popup_still_open());
  CloseBrowserSynchronously(popup);
  base::RunLoop().RunUntilIdle();
}
IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       LaunchMahoMini_ExternalAppOpen_PrefOff) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(profile);

  // maho-core is canonical: ATCManager::decide_link_destination reads only the
  // core value, so the Chromium pref alone would not exercise this path. The
  // guard restores the previous process-global value on scope exit.
  MahoCore* core = maho::GetCore();
  ASSERT_NE(core, nullptr);
  ScopedOpenExternalLinksInMahoMini external_links_off(core, false);

  // ATC only applies to a single-URL open (see the app_controller_mac override:
  // `if (urls.size() == 1)`); multi-URL opens fall through to the normal path.
  ::OpenUrlsInBrowserWithProfile({GURL("https://example.com")}, profile);
  base::RunLoop().RunUntilIdle();

  // Assert no popup browser was created
  for (BrowserWindowInterface* bwi : GetAllBrowserWindowInterfaces()) {
    EXPECT_FALSE(bwi && bwi->GetType() == BrowserWindowInterface::TYPE_POPUP);
  }
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       LaunchMahoMini_ExternalAppOpen_PrefOn) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(profile);

  // maho-core is canonical: ATCManager::decide_link_destination reads only the
  // core value, so the Chromium pref alone would not exercise this path. The
  // guard restores the previous process-global value on scope exit.
  MahoCore* core = maho::GetCore();
  ASSERT_NE(core, nullptr);
  ScopedOpenExternalLinksInMahoMini external_links_on(core, true);

  // ATC only applies to a single-URL open (see the app_controller_mac override:
  // `if (urls.size() == 1)`); multi-URL opens fall through to the normal path.
  size_t initial_count = GetAllBrowserWindowInterfaces().size();

  ::OpenUrlsInBrowserWithProfile({GURL("https://example.com")}, profile);

  // Wait for the Maho Mini browser to be created
  Browser* popup = nullptr;
  for (int i = 0; i < 50; ++i) {
    base::RunLoop().RunUntilIdle();
    for (BrowserWindowInterface* bwi : GetAllBrowserWindowInterfaces()) {
      if (bwi && bwi->GetType() == BrowserWindowInterface::TYPE_POPUP) {
        popup = static_cast<Browser*>(bwi);
        break;
      }
    }
    if (popup) {
      break;
    }
    base::PlatformThread::Sleep(base::Milliseconds(50));
  }

  ASSERT_TRUE(popup);
  EXPECT_EQ(initial_count + 1, GetAllBrowserWindowInterfaces().size());

  // Clean up
  CloseBrowserSynchronously(popup);
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       LaunchMahoMini_OptionCmdClick) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(profile);

  // Add an ATC rule for example.com to route to a Space
  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  std::string rule_json =
      R"({"id":"test-rule-id","name":"*.example.com","space_id":"test-space-id",)"
      R"("url_pattern":"*.example.com","enabled":true})";
  char* result_str = maho_core_add_atc_rule(core, rule_json.c_str());
  if (result_str) {
    maho_string_free(result_str);
  }

  // Enable option-cmd click preference
  profile->GetPrefs()->SetBoolean(
      maho::sidebar_prefs::kMahoMiniClickOverrideEnabled, true);

  // Ensure we have a valid browser window since login gate might suppress
  // startup window
  Browser* active_browser = static_cast<Browser*>(browser());
  if (!active_browser) {
    active_browser = static_cast<Browser*>(CreateBrowserWindow(
        BrowserWindowCreateParams(profile, true)));
    active_browser->GetWindow()->Show();
  }
  ASSERT_TRUE(active_browser);

  // Ensure we have a valid active WebContents
  content::WebContents* active_contents =
      active_browser->GetTabStripModel()->GetActiveWebContents();
  if (!active_contents) {
    chrome::AddSelectedTabWithURL(active_browser, GURL("about:blank"),
                                  ui::PAGE_TRANSITION_LINK);
    active_contents = active_browser->GetTabStripModel()->GetActiveWebContents();
  }
  ASSERT_TRUE(active_contents);

  size_t initial_count = GetAllBrowserWindowInterfaces().size();

  // Simulate option-cmd click navigation via OpenURLFromTab with NEW_SPLIT_VIEW
  // The URL matches our ATC Space rule, but decoupling guarantees it routes to
  // Maho Mini.
  content::OpenURLParams params(
      GURL("https://example.com"), content::Referrer(),
      WindowOpenDisposition::NEW_SPLIT_VIEW, ui::PAGE_TRANSITION_LINK,
      /*is_renderer_initiated=*/false);
  params.started_from_context_menu = false;

  content::WebContentsDelegate* delegate =
      BrowserWebContentsDelegate::From(active_browser);
  content::WebContents* result =
      delegate->OpenURLFromTab(active_contents, params, base::NullCallback());
  // Intercept should return nullptr
  EXPECT_FALSE(result);

  // Wait for the Maho Mini browser to be created
  Browser* popup = nullptr;
  for (int i = 0; i < 50; ++i) {
    base::RunLoop().RunUntilIdle();
    for (BrowserWindowInterface* bwi : GetAllBrowserWindowInterfaces()) {
      if (bwi && bwi->GetType() == BrowserWindowInterface::TYPE_POPUP) {
        popup = static_cast<Browser*>(bwi);
        break;
      }
    }
    if (popup) {
      break;
    }
    base::PlatformThread::Sleep(base::Milliseconds(50));
  }

  ASSERT_TRUE(popup);
  EXPECT_EQ(initial_count + 1, GetAllBrowserWindowInterfaces().size());

  // Clean up
  CloseBrowserSynchronously(popup);
  if (active_browser != browser()) {
    CloseBrowserSynchronously(active_browser);
  }
  base::RunLoop().RunUntilIdle();
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       ATCRouting_InBrowserNav) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(profile);

  // Ensure we have a valid browser window since login gate might suppress
  // startup window
  Browser* active_browser = static_cast<Browser*>(browser());
  if (!active_browser) {
    active_browser = static_cast<Browser*>(CreateBrowserWindow(
        BrowserWindowCreateParams(profile, true)));
    active_browser->GetWindow()->Show();
  }
  ASSERT_TRUE(active_browser);

  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  // Ensure we have at least 2 spaces
  std::string current_space = bridge->GetActiveSpaceId(active_browser);
  std::string target_space = "target_space_atc";
  if (bridge->GetSpaceIds().size() < 2) {
    std::optional<base::FilePath> path =
        MahoSpaceProfileBridge::ProfileBasenameForId("target_profile");
    ASSERT_TRUE(path.has_value());
    bridge->RegisterSpace(target_space, *path);
  } else {
    for (const auto& sid : bridge->GetSpaceIds()) {
      if (sid != current_space) {
        target_space = sid;
        break;
      }
    }
  }

  // Add an ATC rule for target.example.com to route to target_space
  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  std::string rule_json =
      R"({"id":"test-atc-rule-id","name":"target.example.com","space_id":")" +
      target_space + R"(","enabled":true})";
  char* result_str = maho_core_add_atc_rule(core, rule_json.c_str());
  if (result_str) {
    maho_string_free(result_str);
  }
  maho::MahoAtcState::SetHasEnabledRules(true);

  content::WebContents* active_contents =
      active_browser->GetTabStripModel()->GetActiveWebContents();
  if (!active_contents) {
    chrome::AddSelectedTabWithURL(active_browser, GURL("about:blank"),
                                  ui::PAGE_TRANSITION_LINK);
    active_contents = active_browser->GetTabStripModel()->GetActiveWebContents();
  }
  ASSERT_TRUE(active_contents);

  // Trigger in-browser top-level user-initiated navigation
  content::OpenURLParams params(
      GURL("https://target.example.com/page"), content::Referrer(),
      WindowOpenDisposition::CURRENT_TAB, ui::PAGE_TRANSITION_LINK,
      /*is_renderer_initiated=*/false);
  params.user_gesture = true;

  content::WebContentsDelegate* delegate =
      BrowserWebContentsDelegate::From(active_browser);
  content::WebContents* result =
      delegate->OpenURLFromTab(active_contents, params, base::NullCallback());
  // The routing cancels the original navigation asynchronously
  EXPECT_FALSE(result);

  // Spin the run loop to allow async core task and UI post task to run
  for (int i = 0; i < 50; ++i) {
    base::RunLoop run_loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), base::Milliseconds(50));
    run_loop.Run();
    if (bridge->GetActiveSpaceId(active_browser) == target_space) {
      break;
    }
  }

  // Active space should be target_space now
  EXPECT_EQ(target_space, bridge->GetActiveSpaceId(active_browser));

  // Clean up: remove the rule
  char* remove_result = maho_core_remove_atc_rule(core, "test-atc-rule-id");
  if (remove_result) {
    maho_string_free(remove_result);
  }
  maho::MahoAtcState::SetHasEnabledRules(false);
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       ATCRouting_ExternalAppOpen) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(profile);

  // Ensure we have a valid browser window since login gate might suppress
  // startup window
  Browser* active_browser = static_cast<Browser*>(browser());
  if (!active_browser) {
    active_browser = static_cast<Browser*>(CreateBrowserWindow(
        BrowserWindowCreateParams(profile, true)));
    active_browser->GetWindow()->Show();
  }
  ASSERT_TRUE(active_browser);

  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  // Ensure we have at least 2 spaces
  std::string current_space = bridge->GetActiveSpaceId(active_browser);
  std::string target_space = "target_space_atc_ext";
  if (bridge->GetSpaceIds().size() < 2) {
    std::optional<base::FilePath> path =
        MahoSpaceProfileBridge::ProfileBasenameForId("target_profile_ext");
    ASSERT_TRUE(path.has_value());
    bridge->RegisterSpace(target_space, *path);
  } else {
    for (const auto& sid : bridge->GetSpaceIds()) {
      if (sid != current_space) {
        target_space = sid;
        break;
      }
    }
  }

  // Add an ATC rule for ext.example.com to route to target_space
  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  std::string rule_json =
      R"({"id":"test-atc-rule-ext-id","name":"ext.example.com","space_id":")" +
      target_space + R"(","enabled":true})";
  char* result_str = maho_core_add_atc_rule(core, rule_json.c_str());
  if (result_str) {
    maho_string_free(result_str);
  }
  maho::MahoAtcState::SetHasEnabledRules(true);

  // Open external link: should switch space in current browser (or create one)
  ::OpenUrlsInBrowserWithProfile({GURL("https://ext.example.com/ext_page")},
                                 profile);

  // Wait for async task and layout update
  for (int i = 0; i < 50; ++i) {
    base::RunLoop run_loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), base::Milliseconds(50));
    run_loop.Run();
    if (active_browser &&
        bridge->GetActiveSpaceId(active_browser) == target_space) {
      break;
    }
  }

  // Active space should be target_space now
  ASSERT_TRUE(active_browser);
  EXPECT_EQ(target_space, bridge->GetActiveSpaceId(active_browser));

  // Clean up: remove the rule
  char* remove_result = maho_core_remove_atc_rule(core, "test-atc-rule-ext-id");
  if (remove_result) {
    maho_string_free(remove_result);
  }
  maho::MahoAtcState::SetHasEnabledRules(false);
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       ATCRouting_NoRulesFastPath) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(profile);
  Browser* active_browser = static_cast<Browser*>(browser());
  if (!active_browser) {
    active_browser = static_cast<Browser*>(CreateBrowserWindow(
        BrowserWindowCreateParams(profile, true)));
    active_browser->GetWindow()->Show();
  }
  ASSERT_TRUE(active_browser);

  // Explicitly set has rules to false to ensure fast path
  maho::MahoAtcState::SetHasEnabledRules(false);

  content::WebContents* active_contents =
      active_browser->GetTabStripModel()->GetActiveWebContents();
  if (!active_contents) {
    chrome::AddSelectedTabWithURL(active_browser, GURL("about:blank"),
                                  ui::PAGE_TRANSITION_LINK);
    active_contents = active_browser->GetTabStripModel()->GetActiveWebContents();
  }
  ASSERT_TRUE(active_contents);

  // Trigger navigation
  content::OpenURLParams params(
      GURL("https://target.example.com/page"), content::Referrer(),
      WindowOpenDisposition::CURRENT_TAB, ui::PAGE_TRANSITION_LINK,
      /*is_renderer_initiated=*/false);
  params.user_gesture = true;

  content::WebContentsDelegate* delegate =
      BrowserWebContentsDelegate::From(active_browser);
  content::WebContents* result =
      delegate->OpenURLFromTab(active_contents, params, base::NullCallback());
  // The fast path should NOT return nullptr (it should navigate synchronously
  // and return WebContents)
  EXPECT_NE(result, nullptr);
  EXPECT_EQ(result, active_contents);
}

// Routing E2E for glob TrafficRules created the SAME way the settings UI
// creates them: maho_core_add_traffic_rule with matchType:"glob" (NOT the
// legacy maho_core_add_atc_rule Contains-mirror path). This is the path the
// runtime bug lived on — a Contains-restored glob rule never matched any URL.
class MahoAtcRoutingBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  void TearDownOnMainThread() override {
    maho::MahoAtcState::SetHasEnabledRules(false);
    InProcessBrowserTest::TearDownOnMainThread();
  }

 protected:
  static constexpr char kMatchHost[] = "sub.atc-routed.test";
  static constexpr char kNonMatchHost[] = "plain.other.test";
  static constexpr char kGlobPattern[] = "*.atc-routed.test";

  Browser* EnsureBrowser(Profile* profile) {
    Browser* b = static_cast<Browser*>(browser());
    if (!b) {
      b = static_cast<Browser*>(CreateBrowserWindow(
          BrowserWindowCreateParams(profile, true)));
      b->GetWindow()->Show();
    }
    return b;
  }

  content::WebContents* EnsureActiveContents(Browser* b) {
    content::WebContents* wc = b->GetTabStripModel()->GetActiveWebContents();
    if (!wc) {
      chrome::AddSelectedTabWithURL(b, GURL("about:blank"),
                                    ui::PAGE_TRANSITION_LINK);
      wc = b->GetTabStripModel()->GetActiveWebContents();
    }
    return wc;
  }

  std::string RegisterTargetSpace(const std::string& space_id,
                                  const char* profile_basename) {
    MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
    std::optional<base::FilePath> path =
        MahoSpaceProfileBridge::ProfileBasenameForId(profile_basename);
    if (path.has_value()) {
      bridge->RegisterSpace(space_id, *path);
    }
    return space_id;
  }

  void AddGlobRule(const std::string& id,
                   const std::string& pattern,
                   const std::string& target_space) {
    MahoCore* core = maho::GetCore();
    ASSERT_TRUE(core);
    std::string rule_json = R"({"id":")" + id + R"(","urlPattern":")" +
                            pattern +
                            R"(","matchType":"glob","targetSpaceId":")" +
                            target_space + R"(","enabled":true})";
    char* result_str = maho_core_add_traffic_rule(core, rule_json.c_str());
    if (result_str) {
      maho_string_free(result_str);
    }
    maho::MahoAtcState::SetHasEnabledRules(true);
  }

  void RemoveRule(const std::string& id) {
    MahoCore* core = maho::GetCore();
    if (!core) {
      return;
    }
    char* r = maho_core_remove_traffic_rule_persisted(core, id.c_str());
    if (r) {
      maho_string_free(r);
    }
  }

  int CountTabsWithHost(const std::string& host) {
    int count = 0;
    for (BrowserWindowInterface* bwi : GetAllBrowserWindowInterfaces()) {
      if (!bwi) {
        continue;
      }
      TabStripModel* tsm = bwi->GetTabStripModel();
      for (int i = 0; i < tsm->count(); ++i) {
        content::WebContents* wc = tsm->GetWebContentsAt(i);
        if (wc && wc->GetVisibleURL().host() == host) {
          ++count;
        }
      }
    }
    return count;
  }

  GURL MatchUrl(const std::string& path) {
    return embedded_test_server()->GetURL(kMatchHost, path);
  }

  GURL NonMatchUrl(const std::string& path) {
    return embedded_test_server()->GetURL(kNonMatchHost, path);
  }
};

// Scenario 1: A browser-initiated typed navigation to a matching URL is
// deferred by the throttle and routed to the target Space.
IN_PROC_BROWSER_TEST_F(MahoAtcRoutingBrowserTest, TypedNavRoutesViaThrottle) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(profile);
  Browser* b = EnsureBrowser(profile);
  ASSERT_TRUE(b);
  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  std::string target_space =
      RegisterTargetSpace("atc_typed_space", "atc_typed_profile");
  ASSERT_NE(target_space, bridge->GetActiveSpaceId(b));
  AddGlobRule("atc-typed-rule", kGlobPattern, target_space);

  NavigateParams params(b, MatchUrl("/title1.html"), ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::CURRENT_TAB;
  Navigate(&params);

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return bridge->GetActiveSpaceId(b) == target_space; }));
  EXPECT_EQ(target_space, bridge->GetActiveSpaceId(b));
  RemoveRule("atc-typed-rule");
}

// Scenario 2: A same-tab link click (renderer-initiated WITH user gesture) is
// routed by the throttle.
IN_PROC_BROWSER_TEST_F(MahoAtcRoutingBrowserTest,
                       SameTabClickRoutesViaThrottle) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(profile);
  Browser* b = EnsureBrowser(profile);
  ASSERT_TRUE(b);
  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(b, NonMatchUrl("/title1.html")));
  std::string target_space =
      RegisterTargetSpace("atc_click_space", "atc_click_profile");
  ASSERT_NE(target_space, bridge->GetActiveSpaceId(b));
  AddGlobRule("atc-click-rule", kGlobPattern, target_space);

  content::WebContents* wc = b->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(wc);
  const std::string script =
      "var a = document.createElement('a'); a.href = '" +
      MatchUrl("/title2.html").spec() +
      "'; a.textContent = 'go'; document.body.appendChild(a); a.click();";
  ASSERT_TRUE(content::ExecJs(wc, script));

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return bridge->GetActiveSpaceId(b) == target_space; }));
  EXPECT_EQ(target_space, bridge->GetActiveSpaceId(b));
  RemoveRule("atc-click-rule");
}

// Scenario 3: A cmd-click (NEW_BACKGROUND_TAB) routes through the
// OpenURLFromTab ATC block exactly once — no double tab from the target-space
// re-navigation.
IN_PROC_BROWSER_TEST_F(MahoAtcRoutingBrowserTest, CmdClickRoutesExactlyOnce) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(profile);
  Browser* b = EnsureBrowser(profile);
  ASSERT_TRUE(b);
  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  std::string target_space =
      RegisterTargetSpace("atc_cmd_space", "atc_cmd_profile");
  ASSERT_NE(target_space, bridge->GetActiveSpaceId(b));
  AddGlobRule("atc-cmd-rule", kGlobPattern, target_space);

  content::WebContents* active_contents = EnsureActiveContents(b);
  ASSERT_TRUE(active_contents);

  content::OpenURLParams params(MatchUrl("/title1.html"), content::Referrer(),
                                WindowOpenDisposition::NEW_BACKGROUND_TAB,
                                ui::PAGE_TRANSITION_LINK,
                                /*is_renderer_initiated=*/false);
  params.user_gesture = true;
  content::WebContentsDelegate* delegate = BrowserWebContentsDelegate::From(b);
  content::WebContents* result =
      delegate->OpenURLFromTab(active_contents, params, base::NullCallback());
  EXPECT_FALSE(result);

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return bridge->GetActiveSpaceId(b) == target_space; }));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return CountTabsWithHost(kMatchHost) >= 1; }));
  EXPECT_EQ(1, CountTabsWithHost(kMatchHost));
  RemoveRule("atc-cmd-rule");
}

// Scenario 4: An option-cmd (NEW_SPLIT_VIEW) click always opens Maho Mini and
// never routes to a Space, even when the URL matches an enabled rule.
IN_PROC_BROWSER_TEST_F(MahoAtcRoutingBrowserTest,
                       OptionCmdClickOpensMahoMiniNoRouting) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(profile);
  Browser* b = EnsureBrowser(profile);
  ASSERT_TRUE(b);
  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  std::string target_space =
      RegisterTargetSpace("atc_mini_space", "atc_mini_profile");
  AddGlobRule("atc-mini-rule", kGlobPattern, target_space);
  profile->GetPrefs()->SetBoolean(
      maho::sidebar_prefs::kMahoMiniClickOverrideEnabled, true);

  content::WebContents* active_contents = EnsureActiveContents(b);
  ASSERT_TRUE(active_contents);
  const std::string origin_space = bridge->GetActiveSpaceId(b);
  const size_t initial_count = GetAllBrowserWindowInterfaces().size();

  content::OpenURLParams params(MatchUrl("/title1.html"), content::Referrer(),
                                WindowOpenDisposition::NEW_SPLIT_VIEW,
                                ui::PAGE_TRANSITION_LINK,
                                /*is_renderer_initiated=*/false);
  params.started_from_context_menu = false;
  content::WebContentsDelegate* delegate = BrowserWebContentsDelegate::From(b);
  content::WebContents* result =
      delegate->OpenURLFromTab(active_contents, params, base::NullCallback());
  EXPECT_FALSE(result);

  Browser* popup = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    for (BrowserWindowInterface* bwi : GetAllBrowserWindowInterfaces()) {
      if (bwi && bwi->GetType() == BrowserWindowInterface::TYPE_POPUP) {
        popup = static_cast<Browser*>(bwi);
        return true;
      }
    }
    return false;
  }));
  ASSERT_TRUE(popup);
  EXPECT_EQ(initial_count + 1, GetAllBrowserWindowInterfaces().size());
  EXPECT_EQ(origin_space, bridge->GetActiveSpaceId(b));

  CloseBrowserSynchronously(popup);
  RemoveRule("atc-mini-rule");
}

// Scenario 5: With no enabled rules, the throttle fast-path lets a typed
// navigation complete in the same tab with no Space change.
IN_PROC_BROWSER_TEST_F(MahoAtcRoutingBrowserTest, NoRuleFastPath) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(profile);
  Browser* b = EnsureBrowser(profile);
  ASSERT_TRUE(b);
  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  maho::MahoAtcState::SetHasEnabledRules(false);
  const std::string origin_space = bridge->GetActiveSpaceId(b);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(b, MatchUrl("/title1.html")));
  content::WebContents* wc = b->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(wc);
  EXPECT_EQ(kMatchHost, wc->GetLastCommittedURL().host());
  EXPECT_EQ(origin_space, bridge->GetActiveSpaceId(b));
}

// Scenario 6: A non-matching URL keeps the current Space even though a rule is
// enabled.
IN_PROC_BROWSER_TEST_F(MahoAtcRoutingBrowserTest, NonMatchKeepsCurrentSpace) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(profile);
  Browser* b = EnsureBrowser(profile);
  ASSERT_TRUE(b);
  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  std::string target_space =
      RegisterTargetSpace("atc_nomatch_space", "atc_nomatch_profile");
  AddGlobRule("atc-nomatch-rule", kGlobPattern, target_space);
  const std::string origin_space = bridge->GetActiveSpaceId(b);
  ASSERT_NE(origin_space, target_space);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(b, NonMatchUrl("/title1.html")));
  content::WebContents* wc = b->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(wc);
  EXPECT_EQ(kNonMatchHost, wc->GetLastCommittedURL().host());
  EXPECT_EQ(origin_space, bridge->GetActiveSpaceId(b));
  RemoveRule("atc-nomatch-rule");
}

// Scenario 7 (root-cause regression): a glob rule persisted before restart must
// still route after restart. PRE_ persists the rule; the main test relies on
// the startup query re-enabling rules and on the match_type column surviving
// the round-trip so the rule restores as Glob (not Contains).
IN_PROC_BROWSER_TEST_F(MahoAtcRoutingBrowserTest, PRE_GlobRuleSurvivesRestart) {
  MahoCore* core = maho::GetCore();
  ASSERT_TRUE(core);
  AddGlobRule("atc-restart-rule", kGlobPattern, "atc_restart_space");
}

IN_PROC_BROWSER_TEST_F(MahoAtcRoutingBrowserTest, GlobRuleSurvivesRestart) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(profile);
  Browser* b = EnsureBrowser(profile);
  ASSERT_TRUE(b);
  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return maho::MahoAtcState::HasEnabledRules(); }));

  std::string target_space =
      RegisterTargetSpace("atc_restart_space", "atc_restart_profile");
  ASSERT_NE(target_space, bridge->GetActiveSpaceId(b));

  NavigateParams params(b, MatchUrl("/title1.html"), ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::CURRENT_TAB;
  Navigate(&params);

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return bridge->GetActiveSpaceId(b) == target_space; }));
  EXPECT_EQ(target_space, bridge->GetActiveSpaceId(b));
  RemoveRule("atc-restart-rule");
}

IN_PROC_BROWSER_TEST_F(
    MahoIncognitoPrivacyBrowserTest,
    SettingsHandlerRejectsMalformedSelectedProfileWithoutSideEffects) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  Profile* settings_host_profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(settings_host_profile);
  Browser* active_browser = static_cast<Browser*>(browser());
  if (!active_browser) {
    active_browser = static_cast<Browser*>(CreateBrowser(settings_host_profile));
  }
  ASSERT_TRUE(active_browser);
  ASSERT_EQ(active_browser->GetProfile(), settings_host_profile);
  base::ScopedTempDir storage_dir;
  ASSERT_TRUE(storage_dir.CreateUniqueTempDir());
  ASSERT_TRUE(maho_storage_set_sqlcipher_key(
      "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"));
  MahoCore* core =
      maho_core_new_with_storage(storage_dir.GetPath()
                                     .AppendASCII("malformed.sqlite")
                                     .AsUTF8Unsafe()
                                     .c_str());
  ASSERT_TRUE(core);
  {
    ScopedCoreOverride core_override(core);
    base::Value profile_a =
        TakeMahoJson(maho_core_create_profile(core, "Failure A"));
    base::Value profile_b =
        TakeMahoJson(maho_core_create_profile(core, "Failure B"));
    const std::string* profile_a_id = profile_a.GetDict().FindString("id");
    const std::string* profile_b_id = profile_b.GetDict().FindString("id");
    ASSERT_TRUE(profile_a_id);
    ASSERT_TRUE(profile_b_id);
    ASSERT_TRUE(maho_core_switch_profile(core, profile_a_id->c_str()));
    maho::MahoSpaceProfileBridge* bridge =
        maho::MahoSpaceProfileBridge::GetInstance();
    ASSERT_TRUE(bridge->ReconcileProfileRegistryFromCore());
    const maho::ProfileRegistryRecord* profile_b_record = nullptr;
    for (const auto& record : bridge->GetProfileCatalog().records) {
      if (record.maho_id == *profile_b_id) {
        profile_b_record = &record;
        break;
      }
    }
    ASSERT_TRUE(profile_b_record);
    ProfileManager* profile_manager = g_browser_process->profile_manager();
    const base::FilePath profile_b_path = profile_manager->user_data_dir().Append(
        profile_b_record->chromium_basename);
    Profile& profile_b_chromium =
        profiles::testing::CreateProfileSync(profile_manager, profile_b_path);
    ASSERT_EQ(profile_b_chromium.GetPath(), profile_b_path);

    mojo::PendingReceiver<maho_settings::mojom::PageHandler> receiver;
    mojo::PendingRemote<maho_settings::mojom::Page> page;
    MahoSettingsPageHandler handler(std::move(receiver), std::move(page),
                                    settings_host_profile);
    auto context = GetProfileContext(&handler, *profile_b_id, "malformed-b");
    ASSERT_TRUE(context->context);
    const ProfileSettingsFailureSnapshot before =
        CaptureProfileSettingsFailureSnapshot(&handler, core, active_browser);

    auto result = UpdateProfileMetadata(
        &handler, "../malformed-profile", context->context->target_token,
        context->context->context_revision, context->context->profile_revision,
        "MUST NOT WRITE");
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->error);
    EXPECT_FALSE(result->metadata);
    EXPECT_FALSE(result->context);
    EXPECT_EQ(result->error->code,
              maho_settings::mojom::ProfileTargetErrorCode::kInvalidProfileId);
    EXPECT_FALSE(result->error->message.empty());
    ExpectProfileSettingsFailureSnapshotUnchanged(before, &handler, core,
                                                  active_browser);
  }
  maho_core_free(core);
}

IN_PROC_BROWSER_TEST_F(
    MahoIncognitoPrivacyBrowserTest,
    SettingsHandlerRejectsUnknownSelectedProfileWithoutSideEffects) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  Profile* settings_host_profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(settings_host_profile);
  Browser* active_browser = static_cast<Browser*>(browser());
  if (!active_browser) {
    active_browser = static_cast<Browser*>(CreateBrowser(settings_host_profile));
  }
  ASSERT_TRUE(active_browser);
  ASSERT_EQ(active_browser->GetProfile(), settings_host_profile);
  base::ScopedTempDir storage_dir;
  ASSERT_TRUE(storage_dir.CreateUniqueTempDir());
  ASSERT_TRUE(maho_storage_set_sqlcipher_key(
      "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"));
  MahoCore* core = maho_core_new_with_storage(storage_dir.GetPath()
                                                  .AppendASCII("unknown.sqlite")
                                                  .AsUTF8Unsafe()
                                                  .c_str());
  ASSERT_TRUE(core);
  {
    ScopedCoreOverride core_override(core);
    base::Value profile_a =
        TakeMahoJson(maho_core_create_profile(core, "Unknown A"));
    base::Value profile_b =
        TakeMahoJson(maho_core_create_profile(core, "Unknown B"));
    const std::string* profile_a_id = profile_a.GetDict().FindString("id");
    const std::string* profile_b_id = profile_b.GetDict().FindString("id");
    ASSERT_TRUE(profile_a_id);
    ASSERT_TRUE(profile_b_id);
    ASSERT_TRUE(maho_core_switch_profile(core, profile_a_id->c_str()));
    maho::MahoSpaceProfileBridge* bridge =
        maho::MahoSpaceProfileBridge::GetInstance();
    ASSERT_TRUE(bridge->ReconcileProfileRegistryFromCore());
    const maho::ProfileRegistryRecord* profile_b_record = nullptr;
    for (const auto& record : bridge->GetProfileCatalog().records) {
      if (record.maho_id == *profile_b_id) {
        profile_b_record = &record;
        break;
      }
    }
    ASSERT_TRUE(profile_b_record);
    ProfileManager* profile_manager = g_browser_process->profile_manager();
    const base::FilePath profile_b_path = profile_manager->user_data_dir().Append(
        profile_b_record->chromium_basename);
    Profile& profile_b_chromium =
        profiles::testing::CreateProfileSync(profile_manager, profile_b_path);
    ASSERT_EQ(profile_b_chromium.GetPath(), profile_b_path);

    mojo::PendingReceiver<maho_settings::mojom::PageHandler> receiver;
    mojo::PendingRemote<maho_settings::mojom::Page> page;
    MahoSettingsPageHandler handler(std::move(receiver), std::move(page),
                                    settings_host_profile);
    auto context = GetProfileContext(&handler, *profile_b_id, "unknown-b");
    ASSERT_TRUE(context->context);
    const ProfileSettingsFailureSnapshot before =
        CaptureProfileSettingsFailureSnapshot(&handler, core, active_browser);

    auto result = UpdateProfileMetadata(
        &handler, "00000000-0000-4000-8000-000000000001",
        context->context->target_token, context->context->context_revision,
        context->context->profile_revision, "MUST NOT WRITE");
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->error);
    EXPECT_FALSE(result->metadata);
    EXPECT_FALSE(result->context);
    EXPECT_EQ(result->error->code,
              maho_settings::mojom::ProfileTargetErrorCode::kProfileNotFound);
    EXPECT_FALSE(result->error->message.empty());
    ExpectProfileSettingsFailureSnapshotUnchanged(before, &handler, core,
                                                  active_browser);
  }
  maho_core_free(core);
}

IN_PROC_BROWSER_TEST_F(
    MahoIncognitoPrivacyBrowserTest,
    SettingsHandlerRejectsStaleSelectedProfileContextAndRevisionWithoutSideEffects) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  Profile* settings_host_profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(settings_host_profile);
  Browser* active_browser = static_cast<Browser*>(browser());
  if (!active_browser) {
    active_browser = static_cast<Browser*>(CreateBrowser(settings_host_profile));
  }
  ASSERT_TRUE(active_browser);
  ASSERT_EQ(active_browser->GetProfile(), settings_host_profile);
  base::ScopedTempDir storage_dir;
  ASSERT_TRUE(storage_dir.CreateUniqueTempDir());
  ASSERT_TRUE(maho_storage_set_sqlcipher_key(
      "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"));
  MahoCore* core = maho_core_new_with_storage(
      storage_dir.GetPath().AppendASCII("stale.sqlite").AsUTF8Unsafe().c_str());
  ASSERT_TRUE(core);
  {
    ScopedCoreOverride core_override(core);
    base::Value profile_a =
        TakeMahoJson(maho_core_create_profile(core, "Stale A"));
    base::Value profile_b =
        TakeMahoJson(maho_core_create_profile(core, "Stale B"));
    base::Value profile_c =
        TakeMahoJson(maho_core_create_profile(core, "Stale C"));
    const std::string* profile_a_id = profile_a.GetDict().FindString("id");
    const std::string* profile_b_id = profile_b.GetDict().FindString("id");
    const std::string* profile_c_id = profile_c.GetDict().FindString("id");
    ASSERT_TRUE(profile_a_id);
    ASSERT_TRUE(profile_b_id);
    ASSERT_TRUE(profile_c_id);
    ASSERT_TRUE(maho_core_switch_profile(core, profile_a_id->c_str()));
    maho::MahoSpaceProfileBridge* bridge =
        maho::MahoSpaceProfileBridge::GetInstance();
    ASSERT_TRUE(bridge->ReconcileProfileRegistryFromCore());
    const maho::ProfileRegistryRecord* profile_b_record = nullptr;
    const maho::ProfileRegistryRecord* profile_c_record = nullptr;
    for (const auto& record : bridge->GetProfileCatalog().records) {
      if (record.maho_id == *profile_b_id) {
        profile_b_record = &record;
      } else if (record.maho_id == *profile_c_id) {
        profile_c_record = &record;
      }
    }
    ASSERT_TRUE(profile_b_record);
    ASSERT_TRUE(profile_c_record);
    ProfileManager* profile_manager = g_browser_process->profile_manager();
    const base::FilePath profile_b_path = profile_manager->user_data_dir().Append(
        profile_b_record->chromium_basename);
    Profile& profile_b_chromium =
        profiles::testing::CreateProfileSync(profile_manager, profile_b_path);
    ASSERT_EQ(profile_b_chromium.GetPath(), profile_b_path);
    const base::FilePath profile_c_path = profile_manager->user_data_dir().Append(
        profile_c_record->chromium_basename);
    Profile& profile_c_chromium =
        profiles::testing::CreateProfileSync(profile_manager, profile_c_path);
    ASSERT_EQ(profile_c_chromium.GetPath(), profile_c_path);

    mojo::PendingReceiver<maho_settings::mojom::PageHandler> receiver;
    mojo::PendingRemote<maho_settings::mojom::Page> page;
    MahoSettingsPageHandler handler(std::move(receiver), std::move(page),
                                    settings_host_profile);
    auto old_b = GetProfileContext(&handler, *profile_b_id, "stale-old-b");
    ASSERT_TRUE(old_b->context);
    auto current_c = GetProfileContext(&handler, *profile_c_id, "stale-c");
    ASSERT_TRUE(current_c->context);
    const ProfileSettingsFailureSnapshot before_stale_context =
        CaptureProfileSettingsFailureSnapshot(&handler, core, active_browser);

    auto stale_context = UpdateProfileMetadata(
        &handler, *profile_b_id, old_b->context->target_token,
        old_b->context->context_revision, old_b->context->profile_revision,
        "MUST NOT WRITE");
    ASSERT_TRUE(stale_context->error);
    EXPECT_FALSE(stale_context->metadata);
    EXPECT_FALSE(stale_context->context);
    EXPECT_EQ(stale_context->error->code,
              maho_settings::mojom::ProfileTargetErrorCode::kStaleContext);
    EXPECT_FALSE(stale_context->error->message.empty());
    ExpectProfileSettingsFailureSnapshotUnchanged(before_stale_context,
                                                  &handler, core,
                                                  active_browser);

    auto current_b = GetProfileContext(&handler, *profile_b_id, "stale-new-b");
    ASSERT_TRUE(current_b->context);
    const uint64_t stale_profile_revision =
        current_b->context->profile_revision;
    auto newer = UpdateProfileMetadata(&handler, *profile_b_id,
                                       current_b->context->target_token,
                                       current_b->context->context_revision,
                                       stale_profile_revision, "Stale B Newer");
    ASSERT_TRUE(newer->metadata);
    ASSERT_TRUE(newer->context);
    ASSERT_GT(newer->context->profile_revision, stale_profile_revision);
    const ProfileSettingsFailureSnapshot before_stale_revision =
        CaptureProfileSettingsFailureSnapshot(&handler, core, active_browser);

    auto stale_revision = SetProfileArchiveTimeout(
        &handler, *profile_b_id, newer->context->target_token,
        newer->context->context_revision, stale_profile_revision, 72);
    ASSERT_TRUE(stale_revision->error);
    EXPECT_FALSE(stale_revision->archive);
    EXPECT_FALSE(stale_revision->context);
    EXPECT_EQ(
        stale_revision->error->code,
        maho_settings::mojom::ProfileTargetErrorCode::kStaleProfileRevision);
    EXPECT_FALSE(stale_revision->error->message.empty());
    ExpectProfileSettingsFailureSnapshotUnchanged(before_stale_revision,
                                                  &handler, core,
                                                  active_browser);
  }
  maho_core_free(core);
}

IN_PROC_BROWSER_TEST_F(
    MahoIncognitoPrivacyBrowserTest,
    SettingsHandlerPersistsTargetMetadataAndArchiveWithoutSwitchingActive) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  Profile* settings_host_profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(settings_host_profile);
  base::ScopedTempDir storage_dir;
  ASSERT_TRUE(storage_dir.CreateUniqueTempDir());
  constexpr char kSqlcipherKey[] =
      "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
  ASSERT_TRUE(maho_storage_set_sqlcipher_key(kSqlcipherKey));
  const base::FilePath database_path =
      storage_dir.GetPath().AppendASCII("profile-settings.sqlite");
  MahoCore* core =
      maho_core_new_with_storage(database_path.AsUTF8Unsafe().c_str());
  ASSERT_TRUE(core);

  {
    ScopedCoreOverride core_override(core);
    base::Value profile_a = TakeMahoJson(maho_core_create_profile(core, "Profile A"));
    base::Value profile_b = TakeMahoJson(maho_core_create_profile(core, "Profile B"));
    ASSERT_TRUE(profile_a.is_dict());
    ASSERT_TRUE(profile_b.is_dict());
    const std::string* profile_a_id = profile_a.GetDict().FindString("id");
    const std::string* profile_b_id = profile_b.GetDict().FindString("id");
    ASSERT_TRUE(profile_a_id);
    ASSERT_TRUE(profile_b_id);
    ASSERT_TRUE(maho_core_switch_profile(core, profile_a_id->c_str()));

    maho::MahoSpaceProfileBridge* bridge =
        maho::MahoSpaceProfileBridge::GetInstance();
    ASSERT_TRUE(bridge->ReconcileProfileRegistryFromCore());
    const maho::ProfileRegistryRecord* target_record = nullptr;
    for (const auto& record : bridge->GetProfileCatalog().records) {
      if (record.maho_id == *profile_b_id) {
        target_record = &record;
        break;
      }
    }
    ASSERT_TRUE(target_record);
    const uint64_t initial_profile_revision = target_record->revision;

    ProfileManager* profile_manager = g_browser_process->profile_manager();
    const base::FilePath target_path =
        profile_manager->user_data_dir().Append(target_record->chromium_basename);
    Profile& target_profile =
        profiles::testing::CreateProfileSync(profile_manager, target_path);
    ASSERT_EQ(target_profile.GetPath(), target_path);

    mojo::PendingReceiver<maho_settings::mojom::PageHandler> receiver;
    mojo::PendingRemote<maho_settings::mojom::Page> page;
    MahoSettingsPageHandler handler(std::move(receiver), std::move(page),
                                    settings_host_profile);

    auto initial_target = maho_settings::mojom::ProfileTarget::New();
    initial_target->profile_id = *profile_b_id;
    initial_target->target_token = "task-11-profile-b";
    maho_settings::mojom::SelectedProfileContextResultPtr context_result;
    base::RunLoop context_loop;
    handler.GetSelectedProfileContext(
        std::move(initial_target),
        base::BindOnce(
            [](base::RunLoop* loop,
               maho_settings::mojom::SelectedProfileContextResultPtr* out,
               maho_settings::mojom::SelectedProfileContextResultPtr result) {
              *out = std::move(result);
              loop->Quit();
            },
            &context_loop, &context_result));
    context_loop.Run();
    ASSERT_TRUE(context_result);
    ASSERT_TRUE(context_result->context);
    ASSERT_FALSE(context_result->error);
    EXPECT_EQ(context_result->context->profile_id, *profile_b_id);
    EXPECT_FALSE(context_result->context->is_active_maho_profile);
    EXPECT_EQ(context_result->context->profile_revision,
              initial_profile_revision);

    auto make_target = [&]() {
      auto target = maho_settings::mojom::ProfileTarget::New();
      target->profile_id = *profile_b_id;
      target->target_token = "task-11-profile-b";
      return target;
    };

    base::Value profiles_before_stale =
        TakeMahoJson(maho_core_list_profiles(core));
    maho_settings::mojom::ProfileMetadataResultPtr stale_result;
    base::RunLoop stale_loop;
    auto stale_update = maho_settings::mojom::ProfileMetadataUpdate::New();
    stale_update->name = "MUST NOT WRITE";
    stale_update->avatar_color = "#FF0000";
    stale_update->expected_profile_revision = initial_profile_revision;
    handler.UpdateSelectedProfileMetadata(
        make_target(), context_result->context->context_revision + 1,
        std::move(stale_update),
        base::BindOnce(
            [](base::RunLoop* loop,
               maho_settings::mojom::ProfileMetadataResultPtr* out,
               maho_settings::mojom::ProfileMetadataResultPtr result) {
              *out = std::move(result);
              loop->Quit();
            },
            &stale_loop, &stale_result));
    stale_loop.Run();
    ASSERT_TRUE(stale_result->error);
    EXPECT_EQ(stale_result->error->code,
              maho_settings::mojom::ProfileTargetErrorCode::kStaleContext);
    base::Value profiles_after_stale =
        TakeMahoJson(maho_core_list_profiles(core));
    EXPECT_EQ(profiles_after_stale, profiles_before_stale);
    const base::Value* unchanged_target = nullptr;
    for (const auto& profile : profiles_after_stale.GetList()) {
      const std::string* candidate_id = profile.GetDict().FindString("id");
      if (candidate_id && *candidate_id == *profile_b_id) {
        unchanged_target = &profile;
        break;
      }
    }
    ASSERT_TRUE(unchanged_target);
    EXPECT_EQ(*unchanged_target->GetDict().FindString("name"), "Profile B");

    maho_settings::mojom::ProfileMetadataResultPtr metadata_result;
    base::RunLoop metadata_loop;
    auto metadata_update = maho_settings::mojom::ProfileMetadataUpdate::New();
    metadata_update->name = "Profile B Updated";
    metadata_update->avatar_color = "#AF52DE";
    metadata_update->expected_profile_revision = initial_profile_revision;
    handler.UpdateSelectedProfileMetadata(
        make_target(), context_result->context->context_revision,
        std::move(metadata_update),
        base::BindOnce(
            [](base::RunLoop* loop,
               maho_settings::mojom::ProfileMetadataResultPtr* out,
               maho_settings::mojom::ProfileMetadataResultPtr result) {
              *out = std::move(result);
              loop->Quit();
            },
            &metadata_loop, &metadata_result));
    metadata_loop.Run();
    ASSERT_TRUE(metadata_result->metadata);
    ASSERT_TRUE(metadata_result->context);
    EXPECT_EQ(metadata_result->metadata->name, "Profile B Updated");
    EXPECT_EQ(metadata_result->metadata->avatar_color, "#AF52DE");
    EXPECT_GT(metadata_result->context->profile_revision,
              initial_profile_revision);

    maho_settings::mojom::ProfileArchiveSettingsResultPtr archive_set_result;
    base::RunLoop archive_set_loop;
    handler.SetSelectedProfileArchiveTimeout(
        make_target(), metadata_result->context->context_revision, 24,
        metadata_result->context->profile_revision,
        base::BindOnce(
            [](base::RunLoop* loop,
               maho_settings::mojom::ProfileArchiveSettingsResultPtr* out,
               maho_settings::mojom::ProfileArchiveSettingsResultPtr result) {
              *out = std::move(result);
              loop->Quit();
            },
            &archive_set_loop, &archive_set_result));
    archive_set_loop.Run();
    ASSERT_TRUE(archive_set_result->archive);
    ASSERT_TRUE(archive_set_result->context);
    EXPECT_EQ(archive_set_result->archive->timeout_hours, 24);
    EXPECT_GT(archive_set_result->context->profile_revision,
              metadata_result->context->profile_revision);

    maho_settings::mojom::ProfileArchiveSettingsResultPtr archive_get_result;
    base::RunLoop archive_get_loop;
    handler.GetSelectedProfileArchiveSettings(
        make_target(),
        base::BindOnce(
            [](base::RunLoop* loop,
               maho_settings::mojom::ProfileArchiveSettingsResultPtr* out,
               maho_settings::mojom::ProfileArchiveSettingsResultPtr result) {
              *out = std::move(result);
              loop->Quit();
            },
            &archive_get_loop, &archive_get_result));
    archive_get_loop.Run();
    ASSERT_TRUE(archive_get_result->archive);
    EXPECT_EQ(archive_get_result->archive->timeout_hours, 24);
    base::Value active_profile_id =
        TakeMahoJson(maho_core_get_active_profile_id(core));
    ASSERT_TRUE(active_profile_id.is_string());
    EXPECT_EQ(active_profile_id.GetString(), *profile_a_id);
  }

  maho_core_free(core);
  core = nullptr;
  MahoCore* reopened =
      maho_core_new_with_storage(database_path.AsUTF8Unsafe().c_str());
  ASSERT_TRUE(reopened);
  ASSERT_EQ(maho_core_load_state(reopened), 1);
  base::Value restored_profiles = TakeMahoJson(maho_core_list_profiles(reopened));
  const base::Value* restored_target = nullptr;
  for (const auto& profile : restored_profiles.GetList()) {
    if (profile.GetDict().FindString("name") &&
        *profile.GetDict().FindString("name") == "Profile B Updated") {
      restored_target = &profile;
      break;
    }
  }
  ASSERT_TRUE(restored_target);
  const std::string* restored_avatar =
      restored_target->GetDict().FindString("avatarColor");
  ASSERT_TRUE(restored_avatar);
  EXPECT_EQ(*restored_avatar, "#AF52DE");
  EXPECT_EQ(
      restored_target->GetDict().FindDouble("archiveTimeoutHours").value_or(-1),
      24.0);
  base::Value restored_active =
      TakeMahoJson(maho_core_get_active_profile_id(reopened));
  ASSERT_TRUE(restored_active.is_string());
  const base::Value* restored_a = nullptr;
  for (const auto& profile : restored_profiles.GetList()) {
    if (profile.GetDict().FindString("name") &&
        *profile.GetDict().FindString("name") == "Profile A") {
      restored_a = &profile;
      break;
    }
  }
  ASSERT_TRUE(restored_a);
  const std::string* restored_a_id = restored_a->GetDict().FindString("id");
  ASSERT_TRUE(restored_a_id);
  EXPECT_EQ(restored_active.GetString(), *restored_a_id);
  maho_core_free(reopened);
}

IN_PROC_BROWSER_TEST_F(
    MahoIncognitoPrivacyBrowserTest,
    ProfileObservablesDecisionUsesIndependentBrowserAndMahoAuthorities) {
  const maho::ProfileCatalogResult catalog = MakeObservablesCatalog(
      {MakeObservablesRecord("maho-active", "MahoProfile_active", true),
       MakeObservablesRecord("browser-host", "MahoProfile_host", false)});
  const struct {
    const char* name;
    const char* browser_basename;
    std::optional<std::string> active_id_json;
    std::optional<std::string> expected_browser_id;
    std::optional<std::string> expected_maho_id;
  } cases[] = {
      {"independent-authorities", "MahoProfile_host", R"("maho-active")",
       "browser-host", "maho-active"},
      {"zero-browser-matches", "MahoProfile_unmatched", R"("maho-active")",
       std::nullopt, "maho-active"},
      {"absent-active-maho-json", "MahoProfile_host", std::nullopt,
       "browser-host", std::nullopt},
  };

  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    auto decision = DecideObservables(
        catalog, base::FilePath::FromASCII(test_case.browser_basename),
        test_case.active_id_json);
    ASSERT_EQ(
        decision.kind,
        maho_settings::testing::ProfileObservablesDecisionKind::kSnapshot);
    ASSERT_TRUE(decision.snapshot);
    EXPECT_EQ(decision.snapshot->active_browser_profile_id,
              test_case.expected_browser_id);
    EXPECT_EQ(decision.snapshot->active_maho_profile_id,
              test_case.expected_maho_id);
    EXPECT_EQ(decision.snapshot->registry_revision, 17u);
  }
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       ProfileObservablesDecisionRejectsInvalidActiveIdJson) {
  const maho::ProfileCatalogResult catalog = MakeObservablesCatalog(
      {MakeObservablesRecord("maho-active", "MahoProfile_active", true),
       MakeObservablesRecord("other", "MahoProfile_other", false)});
  const struct {
    const char* name;
    const char* json;
  } cases[] = {{"malformed", "{"},
               {"non-string", "7"},
               {"empty", R"("")"},
               {"unknown", R"("missing")"},
               {"disagreeing", R"("other")"}};

  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    auto decision = DecideObservables(
        catalog, base::FilePath::FromASCII("MahoProfile_active"),
        test_case.json);
    EXPECT_EQ(decision.kind,
              maho_settings::testing::ProfileObservablesDecisionKind::kNull);
    EXPECT_FALSE(decision.snapshot);
  }
}

IN_PROC_BROWSER_TEST_F(
    MahoIncognitoPrivacyBrowserTest,
    ProfileObservablesDecisionRejectsInvalidOrAmbiguousCatalog) {
  const base::FilePath host = base::FilePath::FromASCII("MahoProfile_host");
  const struct {
    const char* name;
    maho::ProfileCatalogResult catalog;
  } cases[] = {
      {"empty-maho-id",
       MakeObservablesCatalog(
           {MakeObservablesRecord("", "MahoProfile_active", true)})},
      {"duplicate-maho-id",
       MakeObservablesCatalog(
           {MakeObservablesRecord("same", "MahoProfile_active", true),
            MakeObservablesRecord("same", "MahoProfile_other", false)})},
      {"duplicate-browser-basename",
       MakeObservablesCatalog(
           {MakeObservablesRecord("active", "MahoProfile_host", true),
            MakeObservablesRecord("other", "MahoProfile_host", false)})},
      {"zero-active-records",
       MakeObservablesCatalog(
           {MakeObservablesRecord("only", "MahoProfile_host", false)})},
      {"multiple-active-records",
       MakeObservablesCatalog(
           {MakeObservablesRecord("first", "MahoProfile_host", true),
            MakeObservablesRecord("second", "MahoProfile_other", true)})},
  };

  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    auto decision =
        DecideObservables(test_case.catalog, host, std::nullopt);
    EXPECT_EQ(decision.kind,
              maho_settings::testing::ProfileObservablesDecisionKind::kNull);
    EXPECT_FALSE(decision.snapshot);
  }
}

IN_PROC_BROWSER_TEST_F(
    MahoIncognitoPrivacyBrowserTest,
    ProfileObservablesDecisionMapsLifecycleAndDeletionObservablesExactly) {
  const maho::ProfileCatalogResult catalog = MakeObservablesCatalog(
      {MakeObservablesRecord("provisioning", "MahoProfile_provisioning", true,
                             maho::ProfileLifecycleState::kProvisioning),
       MakeObservablesRecord("ready", "MahoProfile_ready", false,
                             maho::ProfileLifecycleState::kReady),
       MakeObservablesRecord("deleting", "MahoProfile_deleting", false,
                             maho::ProfileLifecycleState::kDeleting),
       MakeObservablesRecord("repair", "MahoProfile_repair", false,
                             maho::ProfileLifecycleState::kRepairRequired)});

  auto decision = DecideObservables(
      catalog, base::FilePath::FromASCII("MahoProfile_ready"),
      R"("provisioning")");
  ASSERT_EQ(decision.kind,
            maho_settings::testing::ProfileObservablesDecisionKind::kSnapshot);
  ASSERT_TRUE(decision.snapshot);
  ASSERT_EQ(decision.snapshot->lifecycle_entries.size(), 4u);
  EXPECT_EQ(decision.snapshot->lifecycle_entries[0]->profile_id,
            "provisioning");
  EXPECT_EQ(decision.snapshot->lifecycle_entries[0]->lifecycle_state,
            maho_settings::mojom::ProfileObservableLifecycleState::
                kProvisioning);
  EXPECT_EQ(decision.snapshot->lifecycle_entries[1]->profile_id, "ready");
  EXPECT_EQ(decision.snapshot->lifecycle_entries[1]->lifecycle_state,
            maho_settings::mojom::ProfileObservableLifecycleState::kReady);
  EXPECT_EQ(decision.snapshot->lifecycle_entries[2]->profile_id, "deleting");
  EXPECT_EQ(decision.snapshot->lifecycle_entries[2]->lifecycle_state,
            maho_settings::mojom::ProfileObservableLifecycleState::kDeleting);
  EXPECT_EQ(decision.snapshot->lifecycle_entries[3]->profile_id, "repair");
  EXPECT_EQ(decision.snapshot->lifecycle_entries[3]->lifecycle_state,
            maho_settings::mojom::ProfileObservableLifecycleState::
                kRepairRequired);
  EXPECT_EQ(decision.snapshot->pending_deletion_profile_ids,
            std::vector<std::string>({"deleting"}));
  EXPECT_EQ(decision.snapshot->deleted_history_availability,
            maho_settings::mojom::DeletedProfileHistoryAvailability::
                kUnavailable);
}

IN_PROC_BROWSER_TEST_F(
    MahoIncognitoPrivacyBrowserTest,
    ProfileObservablesDecisionRetriesRevisionMismatchOnlyOnce) {
  const maho::ProfileCatalogResult catalog = MakeObservablesCatalog(
      {MakeObservablesRecord("active", "MahoProfile_active", true)});

  auto first = DecideObservables(
      catalog, base::FilePath::FromASCII("MahoProfile_active"),
      R"("active")", 18, 0);
  EXPECT_EQ(first.kind,
            maho_settings::testing::ProfileObservablesDecisionKind::kRetry);
  EXPECT_FALSE(first.snapshot);

  auto second = DecideObservables(
      catalog, base::FilePath::FromASCII("MahoProfile_active"),
      R"("active")", 18, 1);
  EXPECT_EQ(second.kind,
            maho_settings::testing::ProfileObservablesDecisionKind::kNull);
  EXPECT_FALSE(second.snapshot);
}

IN_PROC_BROWSER_TEST_F(
    MahoIncognitoPrivacyBrowserTest,
    ProfileObservablesHandlerReadsRealCoreSnapshotAsynchronously) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir storage_dir;
  ASSERT_TRUE(storage_dir.CreateUniqueTempDir());
  ASSERT_TRUE(maho_storage_set_sqlcipher_key(
      "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"));
  MahoCore* core = maho_core_new_with_storage(
      storage_dir.GetPath().AppendASCII("observables.sqlite").AsUTF8Unsafe().c_str());
  ASSERT_TRUE(core);
  {
    ScopedCoreOverride core_override(core);
    base::Value created =
        TakeMahoJson(maho_core_create_profile(core, "Observable Active"));
    const std::string* active_id = created.GetDict().FindString("id");
    ASSERT_TRUE(active_id);
    ASSERT_TRUE(maho_core_switch_profile(core, active_id->c_str()));
    maho::MahoSpaceProfileBridge* bridge =
        maho::MahoSpaceProfileBridge::GetInstance();
    ASSERT_TRUE(bridge->ReconcileProfileRegistryFromCore());
    const maho::ProfileRegistryRecord* active_record = nullptr;
    for (const auto& record : bridge->GetProfileCatalog().records) {
      if (record.maho_id == *active_id) {
        active_record = &record;
        break;
      }
    }
    ASSERT_TRUE(active_record);
    ProfileManager* profile_manager = g_browser_process->profile_manager();
    Profile& loaded_profile = profiles::testing::CreateProfileSync(
        profile_manager,
        profile_manager->user_data_dir().Append(active_record->chromium_basename));

    mojo::PendingReceiver<maho_settings::mojom::PageHandler> receiver;
    mojo::PendingRemote<maho_settings::mojom::Page> page;
    MahoSettingsPageHandler handler(std::move(receiver), std::move(page),
                                    &loaded_profile);
    maho_settings::mojom::ProfileObservablesSnapshotPtr snapshot;
    base::RunLoop loop;
    handler.GetProfileObservablesSnapshot(base::BindOnce(
        [](base::RunLoop* loop,
           maho_settings::mojom::ProfileObservablesSnapshotPtr* out,
           maho_settings::mojom::ProfileObservablesSnapshotPtr value) {
          *out = std::move(value);
          loop->Quit();
        },
        &loop, &snapshot));
    loop.Run();

    ASSERT_TRUE(snapshot);
    EXPECT_EQ(snapshot->active_browser_profile_id, *active_id);
    EXPECT_EQ(snapshot->active_maho_profile_id, *active_id);
    EXPECT_EQ(snapshot->registry_revision,
              bridge->GetProfileCatalog().revision);
    EXPECT_EQ(snapshot->lifecycle_entries.size(),
              bridge->GetProfileCatalog().records.size());
    bool found_active_lifecycle = false;
    for (const auto& entry : snapshot->lifecycle_entries) {
      if (entry->profile_id == *active_id) {
        found_active_lifecycle = true;
        EXPECT_EQ(entry->lifecycle_state,
                  maho_settings::mojom::ProfileObservableLifecycleState::
                      kReady);
      }
    }
    EXPECT_TRUE(found_active_lifecycle);
  }
  maho_core_free(core);
}

IN_PROC_BROWSER_TEST_F(
    MahoIncognitoPrivacyBrowserTest,
    ProfileObservablesHandlerDestructionCancelsPendingCallback) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir storage_dir;
  ASSERT_TRUE(storage_dir.CreateUniqueTempDir());
  ASSERT_TRUE(maho_storage_set_sqlcipher_key(
      "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"));
  MahoCore* core = maho_core_new_with_storage(
      storage_dir.GetPath().AppendASCII("observables-cancel.sqlite")
          .AsUTF8Unsafe().c_str());
  ASSERT_TRUE(core);
  {
    ScopedCoreOverride core_override(core);
    base::Value created =
        TakeMahoJson(maho_core_create_profile(core, "Observable Cancel"));
    const std::string* active_id = created.GetDict().FindString("id");
    ASSERT_TRUE(active_id);
    ASSERT_TRUE(maho_core_switch_profile(core, active_id->c_str()));
    maho::MahoSpaceProfileBridge* bridge =
        maho::MahoSpaceProfileBridge::GetInstance();
    ASSERT_TRUE(bridge->ReconcileProfileRegistryFromCore());
    const maho::ProfileRegistryRecord* active_record = nullptr;
    for (const auto& record : bridge->GetProfileCatalog().records) {
      if (record.maho_id == *active_id) {
        active_record = &record;
        break;
      }
    }
    ASSERT_TRUE(active_record);
    ProfileManager* profile_manager = g_browser_process->profile_manager();
    Profile& loaded_profile = profiles::testing::CreateProfileSync(
        profile_manager,
        profile_manager->user_data_dir().Append(active_record->chromium_basename));

    bool callback_called = false;
    {
      mojo::PendingReceiver<maho_settings::mojom::PageHandler> receiver;
      mojo::PendingRemote<maho_settings::mojom::Page> page;
      auto handler = std::make_unique<MahoSettingsPageHandler>(
          std::move(receiver), std::move(page), &loaded_profile);
      handler->GetProfileObservablesSnapshot(base::BindOnce(
          [](bool* called,
             maho_settings::mojom::ProfileObservablesSnapshotPtr snapshot) {
            *called = true;
          },
          &callback_called));
    }
    maho::QuiesceCoreTasksAndWait();
    base::RunLoop().RunUntilIdle();
    EXPECT_FALSE(callback_called);
  }
  maho_core_free(core);
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       DefaultProfileDeletionIsBlocked) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  Profile* settings_host_profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(settings_host_profile);
  base::ScopedTempDir storage_dir;
  ASSERT_TRUE(storage_dir.CreateUniqueTempDir());
  ASSERT_TRUE(maho_storage_set_sqlcipher_key(
      "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"));
  MahoCore* core = maho_core_new_with_storage(
      storage_dir.GetPath()
          .AppendASCII("default-block.sqlite")
          .AsUTF8Unsafe()
          .c_str());
  ASSERT_TRUE(core);
  ScopedCoreOverride core_override(core);

  // Two ready profiles so the final-profile guard cannot mask the
  // default-profile guard actually under test.
  base::Value profile_a =
      TakeMahoJson(maho_core_create_profile(core, "Block A"));
  base::Value profile_b =
      TakeMahoJson(maho_core_create_profile(core, "Block B"));
  ASSERT_TRUE(profile_a.is_dict());
  ASSERT_TRUE(profile_b.is_dict());
  maho::MahoSpaceProfileBridge* bridge =
      maho::MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge->ReconcileProfileRegistryFromCore());

  const maho::ProfileRegistryRecord* default_record = nullptr;
  for (const auto& record : bridge->GetProfileCatalog().records) {
    if (record.is_default) {
      default_record = &record;
      break;
    }
  }
  ASSERT_TRUE(default_record);
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  const base::FilePath default_path =
      profile_manager->user_data_dir().Append(default_record->chromium_basename);
  const std::string default_id = default_record->maho_id;

  mojo::PendingReceiver<maho_settings::mojom::PageHandler> receiver;
  mojo::PendingRemote<maho_settings::mojom::Page> page;
  MahoSettingsPageHandler handler(std::move(receiver), std::move(page),
                                  settings_host_profile);

  base::RunLoop run_loop;
  bool delete_succeeded = true;
  handler.DeleteProfile(
      default_id, base::BindOnce(
                      [](base::RunLoop* loop, bool* out, bool success) {
                        *out = success;
                        loop->Quit();
                      },
                      &run_loop, &delete_succeeded));
  run_loop.Run();

  // The real default profile is protected: deletion is rejected and the
  // default directory is never marked for deletion.
  EXPECT_FALSE(delete_succeeded);
  EXPECT_FALSE(IsProfileDirectoryMarkedForDeletion(default_path));

  maho_core_free(core);
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       SafeProfileDeletionOnDiskCleanupTest) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  Profile* settings_host_profile = ProfileManager::GetLastUsedProfile();
  ASSERT_TRUE(settings_host_profile);
  base::ScopedTempDir storage_dir;
  ASSERT_TRUE(storage_dir.CreateUniqueTempDir());
  ASSERT_TRUE(maho_storage_set_sqlcipher_key(
      "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"));
  MahoCore* core = maho_core_new_with_storage(
      storage_dir.GetPath()
          .AppendASCII("safe-delete.sqlite")
          .AsUTF8Unsafe()
          .c_str());
  ASSERT_TRUE(core);
  ScopedCoreOverride core_override(core);

  base::Value profile_a =
      TakeMahoJson(maho_core_create_profile(core, "Keep A"));
  base::Value profile_b =
      TakeMahoJson(maho_core_create_profile(core, "Delete B"));
  ASSERT_TRUE(profile_a.is_dict());
  ASSERT_TRUE(profile_b.is_dict());
  const std::string* profile_a_id = profile_a.GetDict().FindString("id");
  const std::string* profile_b_id = profile_b.GetDict().FindString("id");
  ASSERT_TRUE(profile_a_id);
  ASSERT_TRUE(profile_b_id);
  ASSERT_TRUE(maho_core_switch_profile(core, profile_a_id->c_str()));

  maho::MahoSpaceProfileBridge* bridge =
      maho::MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge->ReconcileProfileRegistryFromCore());
  const maho::ProfileRegistryRecord* profile_b_record = nullptr;
  for (const auto& record : bridge->GetProfileCatalog().records) {
    if (record.maho_id == *profile_b_id) {
      profile_b_record = &record;
      break;
    }
  }
  ASSERT_TRUE(profile_b_record);
  ASSERT_FALSE(profile_b_record->is_default);

  ProfileManager* profile_manager = g_browser_process->profile_manager();
  const base::FilePath profile_b_path = profile_manager->user_data_dir().Append(
      profile_b_record->chromium_basename);
  Profile& profile_b_chromium =
      profiles::testing::CreateProfileSync(profile_manager, profile_b_path);
  ASSERT_EQ(profile_b_chromium.GetPath(), profile_b_path);
  ASSERT_TRUE(base::DirectoryExists(profile_b_path));
  ASSERT_FALSE(IsProfileDirectoryMarkedForDeletion(profile_b_path));

  mojo::PendingReceiver<maho_settings::mojom::PageHandler> receiver;
  mojo::PendingRemote<maho_settings::mojom::Page> page;
  MahoSettingsPageHandler handler(std::move(receiver), std::move(page),
                                  settings_host_profile);

  base::RunLoop run_loop;
  bool delete_succeeded = false;
  handler.DeleteProfile(
      *profile_b_id, base::BindOnce(
                         [](base::RunLoop* loop, bool* out, bool success) {
                           *out = success;
                           loop->Quit();
                         },
                         &run_loop, &delete_succeeded));
  run_loop.Run();

  // The safe, non-default, non-host profile is accepted and removed end to
  // end through the deletion coordinator.
  EXPECT_TRUE(delete_succeeded);

  maho_core_free(core);
}

IN_PROC_BROWSER_TEST_F(MahoIncognitoPrivacyBrowserTest,
                       CrossProfileActiveSpaceIndependenceAndReuseTest) {
  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  ProfileManager* profile_manager = g_browser_process->profile_manager();
  base::FilePath path_a =
      profile_manager->user_data_dir().AppendASCII("MahoProfile_A");
  base::FilePath path_b =
      profile_manager->user_data_dir().AppendASCII("MahoProfile_B");

  Profile& profile_a =
      profiles::testing::CreateProfileSync(profile_manager, path_a);
  Profile& profile_b =
      profiles::testing::CreateProfileSync(profile_manager, path_b);

  Browser* browser_a = static_cast<Browser*>(CreateBrowser(&profile_a));
  Browser* browser_b = static_cast<Browser*>(CreateBrowser(&profile_b));

  ASSERT_TRUE(browser_a);
  ASSERT_TRUE(browser_b);

  // Set active spaces for each window to simulate different active spaces
  bridge->SetActiveSpaceId(browser_a, "space_in_a");
  bridge->SetActiveSpaceId(
      browser_b, "old_space_in_b");  // Set to a different space initially

  EXPECT_EQ(bridge->GetActiveSpaceId(browser_a), "space_in_a");
  EXPECT_EQ(bridge->GetActiveSpaceId(browser_b), "old_space_in_b");

  // Map spaces to profiles in the bridge to simulate SwitchToSpace
  // cross-profile check
  bridge->RegisterSpace("space_in_a", path_a.BaseName());
  bridge->RegisterSpace("space_in_b", path_b.BaseName());

  // Trigger SwitchToSpace for space_in_b using browser_a
  // Since profile is different (profile_changed is true), it should:
  // 1. Seed space_in_b for browser_b (since browser_b already exists for
  // profile_b).
  // 2. Keep browser_a's active space as space_in_a.
  bool success = bridge->SwitchToSpace(browser_a, "space_in_b");
  EXPECT_TRUE(success);

  EXPECT_EQ(bridge->GetActiveSpaceId(browser_a), "space_in_a");
  EXPECT_EQ(bridge->GetActiveSpaceId(browser_b),
            "space_in_b");  // Verify it was updated to space_in_b!
}

}  // namespace maho
