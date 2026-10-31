// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_service.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/strings/stringprintf.h"
#include "maho/browser/mail_helper/maho_mail_helper.mojom.h"
#include "maho/browser/mail_helper/maho_mail_helper.mojom-test-utils.h"
#include "maho/browser/mail_helper/maho_mail_helper_launcher.h"
#include "maho/browser/mail_helper/maho_mail_integration_test_support.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

constexpr char kUnavailable[] = "mail helper unavailable";

// Fake helper that parks selected operation callbacks so replies stay pending
// until the test answers them or drops the connection, reproducing a helper
// that disconnects / is reset / shuts down mid-flight.
class ParkingMailHelper : public mojom::MahoMailHelperInterceptorForTesting {
 public:
  ParkingMailHelper() = default;
  ~ParkingMailHelper() override = default;

  mojo::PendingRemote<mojom::MahoMailHelper> BindNewPipe() {
    mojo::PendingRemote<mojom::MahoMailHelper> remote;
    receiver_.Bind(remote.InitWithNewPipeAndPassReceiver());
    return remote;
  }

  void DropConnection() { receiver_.reset(); }
  void ReplyPrepareShutdown() {
    ASSERT_TRUE(parked_prepare_shutdown_);
    std::move(parked_prepare_shutdown_).Run();
  }
  void ReplyShutdown() {
    ASSERT_TRUE(parked_shutdown_);
    std::move(parked_shutdown_).Run();
  }

  int read_calls() const { return read_calls_; }
  void ReplyListAccounts(bool ok, const std::string& result_json) {
    std::move(parked_list_accounts_).Run(ok, result_json);
  }
  void ReplyListFolders(bool ok, const std::string& result_json) {
    ASSERT_TRUE(parked_list_folders_);
    std::move(parked_list_folders_).Run(ok, result_json);
  }
  void ReplyDeleteAccount(bool ok, const std::string& result_json) {
    ASSERT_TRUE(parked_delete_account_);
    std::move(parked_delete_account_).Run(ok, result_json);
  }
  void ReplyOAuthCancel(bool accepted) {
    ASSERT_TRUE(parked_oauth_cancel_);
    std::move(parked_oauth_cancel_).Run(accepted);
  }
  void set_inject_keys_result(bool success) { inject_keys_result_ = success; }
  void set_app_setting(std::string value) { app_setting_ = std::move(value); }
  void set_app_setting_accepts(bool accepts) { app_setting_accepts_ = accepts; }
  int set_app_setting_calls() const { return set_app_setting_calls_; }
  const std::string& app_setting() const { return app_setting_; }

  // mojom::MahoMailHelper: non-read methods are unused by these tests.
  void Initialize(const std::string&,
                  const std::string&,
                  InitializeCallback callback) override {
    std::move(callback).Run(true);
  }
  void InjectKeys(const std::string&,
                  const std::string&,
                  InjectKeysCallback callback) override {
    std::move(callback).Run(inject_keys_result_);
  }
  void StartSync(const std::string&, StartSyncCallback callback) override {
    std::move(callback).Run(true);
  }
  void StopSync(StopSyncCallback callback) override {
    std::move(callback).Run(true);
  }
  void StartBackfill(const std::string&,
                     StartBackfillCallback callback) override {
    std::move(callback).Run(true);
  }
  void GetVersion(GetVersionCallback callback) override {
    std::move(callback).Run("test");
  }
  void PrepareShutdown(PrepareShutdownCallback callback) override {
    parked_prepare_shutdown_ = std::move(callback);
  }
  void Shutdown(ShutdownCallback callback) override {
    parked_shutdown_ = std::move(callback);
  }

  // Read methods: park the reply callback so it stays outstanding.
  void ListAccounts(ListAccountsCallback callback) override {
    ++read_calls_;
    parked_list_accounts_ = std::move(callback);
  }
  void ListFolders(const std::string&,
                   ListFoldersCallback callback) override {
    ++read_calls_;
    parked_list_folders_ = std::move(callback);
  }
  void ListEmails(const std::string&,
                  const std::string&,
                  int64_t,
                  int64_t,
                  ListEmailsCallback callback) override {
    ++read_calls_;
    parked_list_emails_ = std::move(callback);
  }
  void GetEmail(const std::string&, GetEmailCallback callback) override {
    ++read_calls_;
    parked_get_email_ = std::move(callback);
  }
  void SearchEmails(const std::string&,
                    SearchEmailsCallback callback) override {
    ++read_calls_;
    parked_search_emails_ = std::move(callback);
  }
  void ListThread(const std::string&,
                  const std::string&,
                  ListThreadCallback callback) override {
    ++read_calls_;
    parked_list_thread_ = std::move(callback);
  }

  void AddAccount(const std::string&, AddAccountCallback callback) override {
    ++read_calls_;
    parked_add_account_ = std::move(callback);
  }
  void TestConnection(const std::string&, TestConnectionCallback callback) override {
    ++read_calls_;
    parked_test_connection_ = std::move(callback);
  }
  void DeleteAccount(const std::string&, DeleteAccountCallback callback) override {
    ++read_calls_;
    parked_delete_account_ = std::move(callback);
  }
  void OAuthStartUrl(const std::string&, const std::string&,
                     const std::string&, const std::string&,
                     OAuthStartUrlCallback callback) override {
    ++read_calls_;
    parked_oauth_start_url_ = std::move(callback);
  }
  void OAuthComplete(const std::string&, const std::string&, OAuthCompleteCallback callback) override {
    ++read_calls_;
    parked_oauth_complete_ = std::move(callback);
  }
  void ReconnectAccount(const std::string&, ReconnectAccountCallback callback) override {
    ++read_calls_;
    parked_reconnect_account_ = std::move(callback);
  }
  void ImportMigrationArchive(const std::string&, ImportMigrationArchiveCallback callback) override {
    ++read_calls_;
    parked_import_migration_archive_ = std::move(callback);
  }

  void OAuthCancel(const std::string&, OAuthCancelCallback callback) override {
    parked_oauth_cancel_ = std::move(callback);
  }

  void OAuthLoopbackSignIn(const std::string&, const std::string&,
                           OAuthLoopbackSignInCallback callback) override {
    std::move(callback).Run(true, "{}");
  }

  void GetAppSetting(const std::string&, GetAppSettingCallback callback) override {
    std::move(callback).Run(true, app_setting_);
  }
  void SetAppSetting(const std::string&, const std::string& value,
                     SetAppSettingCallback callback) override {
    ++set_app_setting_calls_;
    if (app_setting_accepts_) {
      app_setting_ = value;
    }
    std::move(callback).Run(app_setting_accepts_,
                            app_setting_accepts_ ? value : "rejected");
  }
  void UpdateBehaviorSetting(uint64_t expected_revision,
                             const std::string& key,
                             const std::string& value_json,
                             UpdateBehaviorSettingCallback callback) override {
    const auto current = ParseMailBehaviorSnapshot(app_setting_);
    if (!current) {
      std::move(callback).Run(false, "invalid snapshot");
      return;
    }
    MailBehaviorUpdateResult result = ApplyMailBehaviorUpdate(
        *current, expected_revision, key, value_json);
    if (result.status == MailBehaviorUpdateStatus::kApplied &&
        app_setting_accepts_) {
      app_setting_ = SerializeMailBehaviorSnapshot(result.snapshot);
      ++set_app_setting_calls_;
    } else if (result.status == MailBehaviorUpdateStatus::kApplied) {
      ++set_app_setting_calls_;
      std::move(callback).Run(false, "rejected");
      return;
    }
    const std::string snapshot_json =
        SerializeMailBehaviorSnapshot(result.snapshot);
    const char* status = result.status == MailBehaviorUpdateStatus::kApplied
                             ? "applied"
                             : result.status == MailBehaviorUpdateStatus::kConflict
                                   ? "conflict"
                                   : "invalid";
    std::move(callback).Run(
        true, base::StringPrintf(R"json({"status":"%s","snapshot":%s})json",
                                 status, snapshot_json.c_str()));
  }
  void MuteThread(const std::string&, const std::string&,
                  MuteThreadCallback callback) override {
    std::move(callback).Run(mute_accepts_,
                            mute_accepts_ ? "{}" : "rejected");
  }
  void UnmuteThread(const std::string&, const std::string&,
                    UnmuteThreadCallback callback) override {
    std::move(callback).Run(mute_accepts_,
                            mute_accepts_ ? "{}" : "rejected");
  }
  void set_mute_accepts(bool accepts) { mute_accepts_ = accepts; }

  void BindClient(mojo::PendingRemote<mojom::MahoMailHelperClient> client) override {
    client_.reset();
    client_.Bind(std::move(client));
  }

 private:
  mojom::MahoMailHelper* GetForwardingInterface() override { return this; }

  mojo::Receiver<mojom::MahoMailHelper> receiver_{this};
  int read_calls_ = 0;
  ListAccountsCallback parked_list_accounts_;
  ListFoldersCallback parked_list_folders_;
  ListEmailsCallback parked_list_emails_;
  GetEmailCallback parked_get_email_;
  SearchEmailsCallback parked_search_emails_;
  ListThreadCallback parked_list_thread_;
  AddAccountCallback parked_add_account_;
  TestConnectionCallback parked_test_connection_;
  DeleteAccountCallback parked_delete_account_;
  OAuthStartUrlCallback parked_oauth_start_url_;
  OAuthCompleteCallback parked_oauth_complete_;
  ReconnectAccountCallback parked_reconnect_account_;
  ImportMigrationArchiveCallback parked_import_migration_archive_;
  OAuthCancelCallback parked_oauth_cancel_;
  PrepareShutdownCallback parked_prepare_shutdown_;
  ShutdownCallback parked_shutdown_;
  mojo::Remote<mojom::MahoMailHelperClient> client_;
  bool inject_keys_result_ = true;
  bool app_setting_accepts_ = true;
  bool mute_accepts_ = true;
  int set_app_setting_calls_ = 0;
  std::string app_setting_ = "{}";
};

std::unique_ptr<MahoMailHelperLauncher> MakeReadyLauncher(
    ParkingMailHelper* helper) {
  auto launcher = std::make_unique<MahoMailHelperLauncher>();
  launcher->BindHelperForTesting(helper->BindNewPipe(), /*ready=*/true);
  return launcher;
}

class MahoMailServiceTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

// Parks actual Mojo handshake replies, not launcher state transitions.
class StartupMailHelper : public ParkingMailHelper {
 public:
  void GetVersion(GetVersionCallback callback) override {
    version_reply = std::move(callback);
  }
  void Initialize(const std::string&, const std::string&,
                  InitializeCallback callback) override {
    initialize_reply = std::move(callback);
  }
  void InjectKeys(const std::string&, const std::string&,
                  InjectKeysCallback callback) override {
    keys_reply = std::move(callback);
  }

  GetVersionCallback version_reply;
  InitializeCallback initialize_reply;
  InjectKeysCallback keys_reply;
};

class MahoMailStartupTest : public MahoMailServiceTest {
 protected:
  void SetUp() override {
    service_.SetLauncherFactoryForTesting(base::BindLambdaForTesting([&] {
      auto launcher = std::make_unique<MahoMailHelperLauncher>();
      launcher_ = launcher.get();
      launcher->SetHelperFactoryForTesting(base::BindLambdaForTesting([&] {
        auto helper = std::make_unique<StartupMailHelper>();
        auto remote = helper->BindNewPipe();
        helpers_.push_back(std::move(helper));
        return remote;
      }));
      return launcher;
    }));
    service_.EnsureHelperLaunched("test", "/profile", base::FilePath());
    task_environment_.RunUntilIdle();
    ASSERT_EQ(1u, helpers_.size());
    ASSERT_TRUE(helper().version_reply);
  }

  void TearDown() override {
    // Close the receiver endpoints before derived helper members destroy their
    // parked responders; the base-class receiver otherwise outlives them.
    for (auto& helper : helpers_) {
      helper->DropConnection();
    }
    launcher_ = nullptr;
  }

  StartupMailHelper& helper() { return *helpers_.back(); }
  void ReplyVersion() {
    ASSERT_TRUE(helper().version_reply);
    std::move(helper().version_reply).Run("test");
    task_environment_.RunUntilIdle();
    ASSERT_TRUE(helper().initialize_reply);
  }
  void ReplyInitialize() {
    ASSERT_TRUE(helper().initialize_reply);
    std::move(helper().initialize_reply).Run(true);
    task_environment_.RunUntilIdle();
    ASSERT_TRUE(helper().keys_reply);
  }
  void ReplyKeys() {
    ASSERT_TRUE(helper().keys_reply);
    std::move(helper().keys_reply).Run(true);
    task_environment_.RunUntilIdle();
    ASSERT_EQ(MahoMailService::LifecycleState::kReady,
              service_.lifecycle_state_for_testing());
  }
  void ExpectStarting() {
    EXPECT_EQ(MahoMailService::LifecycleState::kStarting,
              service_.lifecycle_state_for_testing());
    EXPECT_EQ(nullptr, launcher_->GetHelper());
  }
  void ExpectRetryAtDeadline() {
    task_environment_.FastForwardBy(base::Seconds(5));
    ExpectStarting();
    EXPECT_EQ(1u, helpers_.size());
    task_environment_.FastForwardBy(base::Milliseconds(499));
    EXPECT_EQ(1u, helpers_.size());
    task_environment_.FastForwardBy(base::Milliseconds(1));
    ASSERT_EQ(2u, helpers_.size());
    EXPECT_TRUE(helper().version_reply);
  }

  std::vector<std::unique_ptr<StartupMailHelper>> helpers_;
  raw_ptr<MahoMailHelperLauncher> launcher_ = nullptr;
  MahoMailService service_;
};

TEST_F(MahoMailStartupTest, SilentVersionRetriesThenReachesCrashCap) {
  constexpr auto kBackoffMs = std::to_array<int>({500, 1000, 2000, 4000, 8000});
  for (size_t attempt = 0; attempt < 6; ++attempt) {
    ASSERT_EQ(attempt + 1, helpers_.size());
    ASSERT_TRUE(helper().version_reply);
    task_environment_.FastForwardBy(base::Seconds(5));
    if (attempt == 5) {
      EXPECT_TRUE(launcher_->has_given_up());
      EXPECT_EQ(MahoMailService::LifecycleState::kFailed,
                service_.lifecycle_state_for_testing());
      break;
    }
    ExpectStarting();
    task_environment_.FastForwardBy(
        base::Milliseconds(kBackoffMs.at(attempt) - 1));
    EXPECT_EQ(attempt + 1, helpers_.size());
    task_environment_.FastForwardBy(base::Milliseconds(1));
  }
  task_environment_.FastForwardBy(base::Minutes(2));
  EXPECT_EQ(6u, helpers_.size());
}

TEST_F(MahoMailStartupTest, SilentInitializeRetries) {
  ReplyVersion();
  ExpectRetryAtDeadline();
}

TEST_F(MahoMailStartupTest, SilentKeyInjectionRetries) {
  ReplyVersion();
  ReplyInitialize();
  ExpectRetryAtDeadline();
}

TEST_F(MahoMailStartupTest, DeadlineCoversWholeStartupNotEachStage) {
  task_environment_.FastForwardBy(base::Seconds(4));
  ReplyVersion();
  task_environment_.FastForwardBy(base::Seconds(1));
  task_environment_.FastForwardBy(base::Milliseconds(500));
  ASSERT_EQ(2u, helpers_.size());
  ExpectStarting();
}

TEST_F(MahoMailStartupTest, ReadinessCancelsDeadlineWithoutTimingOutMailReads) {
  ReplyVersion();
  ReplyInitialize();
  ReplyKeys();
  // OnHelperReady issues ListAccounts; it remains parked beyond the startup
  // deadline. Startup recovery must not impose a deadline on ordinary reads.
  ASSERT_EQ(1, helper().read_calls());
  task_environment_.FastForwardBy(base::Minutes(2));
  EXPECT_EQ(1u, helpers_.size());
  EXPECT_EQ(MahoMailService::LifecycleState::kReady,
            service_.lifecycle_state_for_testing());
  helper().ReplyListAccounts(true, "[]");
  task_environment_.RunUntilIdle();
  EXPECT_EQ(MahoMailService::LifecycleState::kReady,
            service_.lifecycle_state_for_testing());
}

TEST_F(MahoMailStartupTest, ShutdownCancelsStartupAndLateReply) {
  launcher_ = nullptr;
  task_environment_.FastForwardBy(base::Seconds(4));
  service_.SetEnabledForTesting(false);
  task_environment_.RunUntilIdle();
  std::move(helper().version_reply).Run("test");
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(helper().initialize_reply);
  helper().ReplyPrepareShutdown();
  task_environment_.RunUntilIdle();
  helper().ReplyShutdown();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(MahoMailService::LifecycleState::kDisabled,
            service_.lifecycle_state_for_testing());
  task_environment_.FastForwardBy(base::Minutes(2));
  EXPECT_EQ(1u, helpers_.size());
}

TEST_F(MahoMailStartupTest, RelaunchCancelsOldDeadlineAndIgnoresLateInitialize) {
  ReplyVersion();
  task_environment_.FastForwardBy(base::Seconds(4));
  helper().DropConnection();
  task_environment_.RunUntilIdle();
  task_environment_.FastForwardBy(base::Milliseconds(500));
  ASSERT_EQ(2u, helpers_.size());
  std::move(helpers_.front()->initialize_reply).Run(true);
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(helper().keys_reply);
  task_environment_.FastForwardBy(base::Milliseconds(500));
  EXPECT_EQ(2u, helpers_.size());
  ReplyVersion();
  ReplyInitialize();
  ReplyKeys();
  task_environment_.FastForwardBy(base::Minutes(2));
  EXPECT_EQ(2u, helpers_.size());
  EXPECT_EQ(MahoMailService::LifecycleState::kReady,
            service_.lifecycle_state_for_testing());
}

TEST_F(MahoMailServiceTest, AttachmentFilenameRejectsPlatformPathSyntax) {
  MahoMailService service;
  for (const std::string& filename :
       {"report.pdf:stream", "report\n.pdf", "report.pdf.", "report.pdf ",
        "CON.txt", "NUL", "report?.pdf", "report|.pdf", "aux.txt",
        "COM1.pdf", "lpt9.txt", "COM\xc2\xb9.txt", "CON .txt",
        "report\x7f.pdf", "report*.pdf", "report<.pdf"}) {
    SCOPED_TRACE(filename);
    bool replied = false;
    service.DownloadAttachment(
        "account", 42, "inbox", "1.2", filename,
        base::BindLambdaForTesting([&](bool ok, std::string result) {
          replied = true;
          EXPECT_FALSE(ok);
          EXPECT_EQ("invalid mail identifier", result);
        }));
    EXPECT_TRUE(replied);
  }
}

TEST_F(MahoMailServiceTest, AttachmentFilenameAllowsOrdinaryExtensions) {
  MahoMailService service;
  for (const std::string& filename :
       {"report.pdf", "photo.JPG", "archive.tar.gz", "my report.txt",
        "COM10.txt", "console.pdf"}) {
    SCOPED_TRACE(filename);
    bool replied = false;
    service.DownloadAttachment(
        "account", 42, "inbox", "1.2", filename,
        base::BindLambdaForTesting([&](bool ok, std::string result) {
          replied = true;
          EXPECT_FALSE(ok);
          EXPECT_EQ(kUnavailable, result);
        }));
    EXPECT_TRUE(replied);
  }
}

TEST(MahoMailAppSettingDecodeTest, MissingRowDecodesToNulloptNotLiteralNull) {
  // The helper answers an app-setting read with the JSON encoding of
  // `Option<String>`: a row that was never written arrives as the four
  // characters `null`, and a stored row arrives as a JSON string. Handing the
  // raw payload to a JSON parser is what put a literal null into the settings
  // calendar pane and crashed it, so decoding is mandatory before use.
  EXPECT_EQ(std::nullopt, DecodeAppSettingRead("null"));
  EXPECT_EQ(std::nullopt, DecodeAppSettingRead(std::string()));
  EXPECT_EQ(std::nullopt, DecodeAppSettingRead("{\"hide_weekends\":true}"));
  EXPECT_EQ(std::nullopt, DecodeAppSettingRead("not json"));
  EXPECT_EQ("18:00", DecodeAppSettingRead("\"18:00\""));
  EXPECT_EQ("{\"hide_weekends\":true}",
            DecodeAppSettingRead("\"{\\\"hide_weekends\\\":true}\""));
}

class MahoMailUnreadAggregationTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(MahoMailUnreadAggregationTest,
       DisableClearsSynchronouslyAndStaleFolderReplyCannotRepopulate) {
  ParkingMailHelper helper;
  MahoMailService service;
  service.SetLauncherForTesting(MakeReadyLauncher(&helper));

  std::move(service.StartFolderBadgeRefreshForTesting("account-a"))
      .Run(true,
           R"json([{"id":"inbox","folder_type":"Inbox","unread_count":7}])json");
  ASSERT_EQ(7, service.unread_count_for_testing());
  auto stale_reply = service.StartFolderBadgeRefreshForTesting("account-a");

  service.SetEnabledForTesting(false);
  EXPECT_EQ(0, service.unread_count_for_testing());

  std::move(stale_reply).Run(
      true,
      R"json([{"id":"inbox","folder_type":"Inbox","unread_count":11}])json");
  EXPECT_EQ(0, service.unread_count_for_testing());
}

TEST_F(MahoMailUnreadAggregationTest,
       AccountRemovalClearsSynchronouslyAndStaleFolderReplyCannotRepopulate) {
  ParkingMailHelper helper;
  MahoMailService service;
  service.SetLauncherForTesting(MakeReadyLauncher(&helper));

  std::move(service.StartFolderBadgeRefreshForTesting("account-a"))
      .Run(true,
           R"json([{"id":"inbox","folder_type":"Inbox","unread_count":5}])json");
  ASSERT_EQ(5, service.unread_count_for_testing());
  auto stale_reply = service.StartFolderBadgeRefreshForTesting("account-a");

  bool delete_replied = false;
  service.DeleteAccount(
      "account-a", base::BindLambdaForTesting(
                       [&](bool ok, std::string) { delete_replied = ok; }));
  task_environment_.RunUntilIdle();
  helper.ReplyDeleteAccount(true, "{}");
  task_environment_.RunUntilIdle();
  ASSERT_TRUE(delete_replied);
  EXPECT_EQ(0, service.unread_count_for_testing());

  std::move(stale_reply).Run(
      true,
      R"json([{"id":"inbox","folder_type":"Inbox","unread_count":13}])json");
  EXPECT_EQ(0, service.unread_count_for_testing());
}

TEST_F(MahoMailUnreadAggregationTest,
       NewHelperGenerationClearsSynchronouslyAndOldReplyCannotRepopulate) {
  ParkingMailHelper first_helper;
  MahoMailService service;
  service.SetLauncherForTesting(MakeReadyLauncher(&first_helper));

  std::move(service.StartFolderBadgeRefreshForTesting("account-a"))
      .Run(true,
           R"json([{"id":"inbox","folder_type":"Inbox","unread_count":3}])json");
  ASSERT_EQ(3, service.unread_count_for_testing());
  auto old_generation_reply =
      service.StartFolderBadgeRefreshForTesting("account-a");
  const uint64_t old_generation = service.generation_for_testing();

  ParkingMailHelper second_helper;
  service.SetLauncherForTesting(MakeReadyLauncher(&second_helper));
  ASSERT_GT(service.generation_for_testing(), old_generation);
  EXPECT_EQ(0, service.unread_count_for_testing());

  std::move(old_generation_reply).Run(
      true,
      R"json([{"id":"inbox","folder_type":"Inbox","unread_count":17}])json");
  EXPECT_EQ(0, service.unread_count_for_testing());
}

class ControllableLauncher : public MahoMailHelperLauncher {
 public:
  void Launch(const std::string&,
              const std::string&,
              const base::FilePath&) override {
    ++launch_calls_;
  }

  void InjectDatabaseKeys(const std::string&, const std::string&) override {}

  void Shutdown(base::OnceClosure drained_callback) override {
    ++shutdown_calls_;
    drained_callback_ = std::move(drained_callback);
  }

  void CompleteDrain() {
    CHECK(drained_callback_);
    std::move(drained_callback_).Run();
  }

  int launch_calls() const { return launch_calls_; }
  int shutdown_calls() const { return shutdown_calls_; }

 private:
  int launch_calls_ = 0;
  int shutdown_calls_ = 0;
  base::OnceClosure drained_callback_;
};

class LifecycleHarness {
 public:
  LifecycleHarness() {
    service_.SetLauncherFactoryForTesting(base::BindRepeating(
        [](LifecycleHarness* self) {
          auto launcher = std::make_unique<ControllableLauncher>();
          self->launchers_.push_back(launcher.get());
          return std::unique_ptr<MahoMailHelperLauncher>(std::move(launcher));
        },
        base::Unretained(this)));
  }

  MahoMailService* service() { return &service_; }
  ControllableLauncher* launcher(size_t generation) {
    return launchers_.at(generation - 1);
  }
  size_t launcher_count() const { return launchers_.size(); }

 private:
  MahoMailService service_;
  std::vector<raw_ptr<ControllableLauncher>> launchers_;
};

TEST_F(MahoMailServiceTest, QueuedListAccountsDrainedOnTerminalFailure) {
  LifecycleHarness harness;
  harness.service()->EnsureHelperLaunched("v", "/profile", base::FilePath());
  int calls = 0;
  for (int i = 0; i < 3; ++i) {
    harness.service()->ListAccounts(base::BindLambdaForTesting(
        [&](bool ok, std::string result) {
          ++calls;
          EXPECT_FALSE(ok);
          EXPECT_EQ(kUnavailable, result);
        }));
  }
  harness.launcher(1)->NotifyFailureForTesting(true);
  EXPECT_EQ(0, calls);
  harness.launcher(1)->NotifyFailureForTesting(false);
  EXPECT_EQ(3, calls);
  EXPECT_EQ(1u, harness.launcher_count());
}

TEST_F(MahoMailServiceTest, ListAccountsOnFailedRelaunchesOnce) {
  LifecycleHarness harness;
  harness.service()->EnsureHelperLaunched("v", "/profile", base::FilePath());
  harness.launcher(1)->NotifyFailureForTesting(false);
  for (int i = 0; i < 3; ++i) {
    harness.service()->ListAccounts(base::BindOnce([](bool, std::string) {}));
  }
  ASSERT_EQ(2u, harness.launcher_count());
  EXPECT_EQ(1, harness.launcher(2)->launch_calls());
  harness.launcher(2)->NotifyFailureForTesting(false);
  harness.service()->ListAccounts(base::BindOnce([](bool, std::string) {}));
  EXPECT_EQ(2u, harness.launcher_count());
  task_environment_.FastForwardBy(base::Seconds(60));
  harness.service()->ListAccounts(base::BindOnce([](bool, std::string) {}));
  EXPECT_EQ(3u, harness.launcher_count());
}

TEST_F(MahoMailServiceTest, ListAccountsOnStoppedDoesNotUndoBrowserShutdown) {
  LifecycleHarness harness;
  harness.service()->EnsureHelperLaunched("v", "/profile", base::FilePath());
  harness.service()->Shutdown();
  harness.launcher(1)->CompleteDrain();
  ASSERT_EQ(MahoMailService::LifecycleState::kStopped,
            harness.service()->lifecycle_state_for_testing());
  int calls = 0;
  harness.service()->ListAccounts(base::BindLambdaForTesting(
      [&](bool ok, std::string result) {
        ++calls;
        EXPECT_FALSE(ok);
        EXPECT_EQ(kUnavailable, result);
      }));
  EXPECT_EQ(1, calls);
  EXPECT_EQ(1u, harness.launcher_count());
}

TEST_F(MahoMailServiceTest, IntegrationReplyBarrierSubscribesBeforeRelease) {
  test::MailReplyBarrier barrier;
  int replies = 0;
  barrier.Park(base::BindOnce([](int* replies) { ++*replies; }, &replies));
  EXPECT_EQ(0, replies);
  barrier.Release();
  EXPECT_EQ(1, replies);
}

TEST_F(MahoMailServiceTest, LifecycleOffOnReadyOffDrainedOnReady) {
  LifecycleHarness harness;
  harness.service()->SetEnabledForTesting(false);
  harness.service()->EnsureHelperLaunched("v", "/profile", base::FilePath());
  EXPECT_EQ(MahoMailService::LifecycleState::kDisabled,
            harness.service()->lifecycle_state_for_testing());

  harness.service()->SetEnabledForTesting(true);
  ASSERT_EQ(1u, harness.launcher_count());
  EXPECT_EQ(1, harness.launcher(1)->launch_calls());
  harness.launcher(1)->NotifyReadyForTesting();
  EXPECT_EQ(MahoMailService::LifecycleState::kReady,
            harness.service()->lifecycle_state_for_testing());

  harness.service()->SetEnabledForTesting(false);
  EXPECT_EQ(MahoMailService::LifecycleState::kDraining,
            harness.service()->lifecycle_state_for_testing());
  EXPECT_EQ(1, harness.launcher(1)->shutdown_calls());
  harness.launcher(1)->CompleteDrain();
  EXPECT_EQ(MahoMailService::LifecycleState::kDisabled,
            harness.service()->lifecycle_state_for_testing());

  harness.service()->SetEnabledForTesting(true);
  ASSERT_EQ(2u, harness.launcher_count());
  EXPECT_EQ(2u, harness.service()->generation_for_testing());
  harness.launcher(2)->NotifyReadyForTesting();
  EXPECT_EQ(MahoMailService::LifecycleState::kReady,
            harness.service()->lifecycle_state_for_testing());
}

TEST_F(MahoMailServiceTest, LifecycleDuplicateEnableAndDisableWhileStarting) {
  LifecycleHarness harness;
  harness.service()->EnsureHelperLaunched("v", "/profile", base::FilePath());
  ASSERT_EQ(1u, harness.launcher_count());
  harness.service()->SetEnabledForTesting(true);
  EXPECT_EQ(1u, harness.launcher_count());

  int calls = 0;
  harness.service()->ListAccounts(base::BindLambdaForTesting(
      [&](bool ok, std::string result) {
        ++calls;
        EXPECT_FALSE(ok);
        EXPECT_EQ(kUnavailable, result);
      }));
  harness.service()->SetEnabledForTesting(false);
  EXPECT_EQ(1, calls);
  harness.launcher(1)->CompleteDrain();
  EXPECT_EQ(MahoMailService::LifecycleState::kDisabled,
            harness.service()->lifecycle_state_for_testing());
}

TEST_F(MahoMailServiceTest, LifecycleHelperCrashAndBrowserShutdownUseDrain) {
  LifecycleHarness harness;
  harness.service()->EnsureHelperLaunched("v", "/profile", base::FilePath());
  harness.launcher(1)->NotifyReadyForTesting();
  harness.launcher(1)->NotifyFailureForTesting(/*will_retry=*/true);
  EXPECT_EQ(MahoMailService::LifecycleState::kStarting,
            harness.service()->lifecycle_state_for_testing());
  harness.launcher(1)->NotifyFailureForTesting(/*will_retry=*/false);
  EXPECT_EQ(MahoMailService::LifecycleState::kFailed,
            harness.service()->lifecycle_state_for_testing());

  harness.service()->Shutdown();
  EXPECT_EQ(MahoMailService::LifecycleState::kDraining,
            harness.service()->lifecycle_state_for_testing());
  harness.launcher(1)->CompleteDrain();
  EXPECT_EQ(MahoMailService::LifecycleState::kStopped,
            harness.service()->lifecycle_state_for_testing());
}

TEST_F(MahoMailServiceTest, LauncherDrainAcknowledgedAndTimeoutAreBounded) {
  {
    ParkingMailHelper helper;
    MahoMailHelperLauncher launcher;
    launcher.BindHelperForTesting(helper.BindNewPipe(), /*ready=*/true);
    int drained = 0;
    launcher.Shutdown(base::BindLambdaForTesting([&] { ++drained; }));
    task_environment_.RunUntilIdle();
    EXPECT_EQ(0, drained);
    helper.ReplyPrepareShutdown();
    task_environment_.RunUntilIdle();
    EXPECT_EQ(0, drained);
    helper.ReplyShutdown();
    task_environment_.RunUntilIdle();
    EXPECT_EQ(1, drained);
  }

  ParkingMailHelper helper;
  MahoMailHelperLauncher launcher;
  launcher.BindHelperForTesting(helper.BindNewPipe(), /*ready=*/true);
  launcher.SetShutdownTimeoutForTesting(base::Milliseconds(1));
  int drained = 0;
  launcher.Shutdown(base::BindLambdaForTesting([&] { ++drained; }));
  task_environment_.FastForwardBy(base::Milliseconds(1));
  EXPECT_EQ(1, drained);
}

TEST_F(MahoMailServiceTest, DisableDrainsInFlightOperationExactlyOnce) {
  ParkingMailHelper helper;
  auto launcher = MakeReadyLauncher(&helper);
  MahoMailService service;
  service.SetLauncherForTesting(std::move(launcher));

  int calls = 0;
  service.ListAccounts(base::BindLambdaForTesting(
      [&](bool ok, std::string result) {
        ++calls;
        EXPECT_TRUE(ok);
        EXPECT_EQ("[]", result);
      }));
  task_environment_.RunUntilIdle();
  ASSERT_EQ(1, helper.read_calls());

  service.SetEnabledForTesting(false);
  EXPECT_EQ(MahoMailService::LifecycleState::kDraining,
            service.lifecycle_state_for_testing());
  task_environment_.RunUntilIdle();
  helper.ReplyListAccounts(true, "[]");
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1, calls);
  EXPECT_EQ(MahoMailService::LifecycleState::kDraining,
            service.lifecycle_state_for_testing());

  helper.ReplyPrepareShutdown();
  task_environment_.RunUntilIdle();
  helper.ReplyShutdown();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(MahoMailService::LifecycleState::kDisabled,
            service.lifecycle_state_for_testing());
}

TEST_F(MahoMailServiceTest,
       DisableDropsOAuthCancelExactlyOnceAndWaitsForTrackedReply) {
  ParkingMailHelper helper;
  MahoMailService service;
  service.SetLauncherForTesting(MakeReadyLauncher(&helper));

  int calls = 0;
  bool accepted = true;
  service.OAuthCancel(
      "state",
      base::BindLambdaForTesting([&](bool result) {
        ++calls;
        accepted = result;
      }));
  task_environment_.RunUntilIdle();
  EXPECT_EQ(0, calls);

  service.SetEnabledForTesting(false);
  EXPECT_EQ(MahoMailService::LifecycleState::kDraining,
            service.lifecycle_state_for_testing());
  task_environment_.RunUntilIdle();
  helper.ReplyPrepareShutdown();
  task_environment_.RunUntilIdle();
  helper.ReplyShutdown();
  task_environment_.RunUntilIdle();

  EXPECT_EQ(1, calls);
  EXPECT_FALSE(accepted);
  EXPECT_EQ(MahoMailService::LifecycleState::kDisabled,
            service.lifecycle_state_for_testing());

  helper.ReplyOAuthCancel(true);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1, calls);
  EXPECT_FALSE(accepted);
}

TEST_F(MahoMailServiceTest, GetHelperGatedUntilReady) {
  ParkingMailHelper helper;
  MahoMailHelperLauncher launcher;
  launcher.BindHelperForTesting(helper.BindNewPipe(), /*ready=*/false);
  EXPECT_EQ(nullptr, launcher.GetHelper());

  ParkingMailHelper ready_helper;
  MahoMailHelperLauncher ready_launcher;
  ready_launcher.BindHelperForTesting(ready_helper.BindNewPipe(),
                                      /*ready=*/true);
  EXPECT_NE(nullptr, ready_launcher.GetHelper());
}

TEST_F(MahoMailServiceTest, InjectKeysTransitionsToReady) {
  ParkingMailHelper helper;
  MahoMailHelperLauncher launcher;
  launcher.BindHelperForTesting(helper.BindNewPipe(), /*ready=*/false);
  EXPECT_EQ(nullptr, launcher.GetHelper());

  launcher.InjectDatabaseKeys("sql_key", "cred_key");
  task_environment_.RunUntilIdle();
  // Keys injected before the Initialize handshake completes must be cached
  // without flipping readiness (the H-4 fix); readiness arrives only after
  // the handshake and the deferred key injection both succeed.
  EXPECT_EQ(nullptr, launcher.GetHelper());

  launcher.CompleteHandshakeForTesting();
  task_environment_.RunUntilIdle();
  EXPECT_NE(nullptr, launcher.GetHelper());
}

TEST_F(MahoMailServiceTest, HelperReadyRefreshesRestoredAccounts) {
  ParkingMailHelper helper;
  auto launcher = std::make_unique<MahoMailHelperLauncher>();
  MahoMailHelperLauncher* launcher_ptr = launcher.get();
  launcher->BindHelperForTesting(helper.BindNewPipe(), false);

  MahoMailService service;
  service.SetLauncherForTesting(std::move(launcher));
  EXPECT_EQ(helper.read_calls(), 0);

  launcher_ptr->InjectDatabaseKeys("sql_key", "cred_key");
  launcher_ptr->CompleteHandshakeForTesting();
  task_environment_.RunUntilIdle();

  EXPECT_EQ(helper.read_calls(), 1);
  helper.ReplyListAccounts(true, "[]");
  task_environment_.RunUntilIdle();
}

// Each read proxy must resolve its callback exactly once with the
// deterministic error string when the Mojo reply is dropped by disconnect.
class DroppedReplyTest : public MahoMailServiceTest {
 protected:
  void RunDropCase(base::OnceCallback<void(MahoMailService*,
                                           MahoMailService::ReadCallback)> issue) {
    auto helper = std::make_unique<ParkingMailHelper>();
    ParkingMailHelper* helper_ptr = helper.get();
    MahoMailService service;
    service.SetLauncherForTesting(MakeReadyLauncher(helper_ptr));

    int calls = 0;
    bool ok = true;
    std::string result;
    std::move(issue).Run(
        &service,
        base::BindLambdaForTesting([&](bool o, std::string r) {
          ++calls;
          ok = o;
          result = std::move(r);
        }));

    task_environment_.RunUntilIdle();
    ASSERT_EQ(1, helper_ptr->read_calls());
    EXPECT_EQ(0, calls);

    helper_ptr->DropConnection();
    task_environment_.RunUntilIdle();

    EXPECT_EQ(1, calls);
    EXPECT_FALSE(ok);
    EXPECT_EQ(kUnavailable, result);
  }
};

TEST_F(DroppedReplyTest, ListAccounts) {
  RunDropCase(base::BindOnce([](MahoMailService* s,
                                MahoMailService::ReadCallback cb) {
    s->ListAccounts(std::move(cb));
  }));
}

TEST_F(DroppedReplyTest, ListFolders) {
  RunDropCase(base::BindOnce([](MahoMailService* s,
                                MahoMailService::ReadCallback cb) {
    s->ListFolders("acct", std::move(cb));
  }));
}

TEST_F(DroppedReplyTest, ListEmails) {
  RunDropCase(base::BindOnce([](MahoMailService* s,
                                MahoMailService::ReadCallback cb) {
    s->ListEmails("acct", "folder", 10, 0, std::move(cb));
  }));
}

TEST_F(DroppedReplyTest, GetEmail) {
  RunDropCase(base::BindOnce([](MahoMailService* s,
                                MahoMailService::ReadCallback cb) {
    s->GetEmail("email", std::move(cb));
  }));
}

TEST_F(DroppedReplyTest, SearchEmails) {
  RunDropCase(base::BindOnce([](MahoMailService* s,
                                MahoMailService::ReadCallback cb) {
    s->SearchEmails("{}", std::move(cb));
  }));
}

TEST_F(DroppedReplyTest, ListThread) {
  RunDropCase(base::BindOnce([](MahoMailService* s,
                                MahoMailService::ReadCallback cb) {
    s->ListThread("acct", "msg", std::move(cb));
  }));
}

TEST_F(DroppedReplyTest, AddAccount) {
  RunDropCase(base::BindOnce([](MahoMailService* s,
                                MahoMailService::ReadCallback cb) {
    s->AddAccount("{}", std::move(cb));
  }));
}

TEST_F(DroppedReplyTest, OAuthComplete) {
  RunDropCase(base::BindOnce([](MahoMailService* s,
                                MahoMailService::ReadCallback cb) {
    s->OAuthComplete("state", "code", std::move(cb));
  }));
}

TEST_F(MahoMailServiceTest, UnavailableHelperResolvesImmediately) {
  MahoMailService service;
  int calls = 0;
  bool ok = true;
  std::string result;
  service.ListAccounts(base::BindLambdaForTesting([&](bool o, std::string r) {
    ++calls;
    ok = o;
    result = std::move(r);
  }));
  EXPECT_EQ(1, calls);
  EXPECT_FALSE(ok);
  EXPECT_EQ(kUnavailable, result);
}

TEST_F(MahoMailServiceTest, ShutdownIsNonBlockingWithUnacknowledgedHelper) {
  auto helper = std::make_unique<ParkingMailHelper>();
  ParkingMailHelper* helper_ptr = helper.get();
  MahoMailService service;
  service.SetLauncherForTesting(MakeReadyLauncher(helper_ptr));

  service.Shutdown();
  SUCCEED();
}

TEST_F(MahoMailServiceTest, RepeatedShutdownIsSafe) {
  auto helper = std::make_unique<ParkingMailHelper>();
  ParkingMailHelper* helper_ptr = helper.get();
  auto launcher = MakeReadyLauncher(helper_ptr);
  MahoMailHelperLauncher* launcher_ptr = launcher.get();
  MahoMailService service;
  service.SetLauncherForTesting(std::move(launcher));

  launcher_ptr->Shutdown();
  launcher_ptr->Shutdown();
  service.Shutdown();
  SUCCEED();
}

class MockServiceObserver : public MahoMailService::Observer {
 public:
  void OnAccountsChanged() override { ++calls_; }
  int calls() const { return calls_; }
 private:
  int calls_ = 0;
};

TEST_F(MahoMailServiceTest, AccountsChangedEventFansOut) {
  MahoMailService service;
  MockServiceObserver observer;
  service.AddObserver(&observer);

  service.OnAccountsChanged();
  EXPECT_EQ(1, observer.calls());

  service.RemoveObserver(&observer);
}

TEST_F(MahoMailServiceTest, OAuthCancelRunsCallbackWithAcceptedValue) {
  // Case A: Helper is available
  auto helper = std::make_unique<ParkingMailHelper>();
  ParkingMailHelper* helper_ptr = helper.get();
  MahoMailService service;
  service.SetLauncherForTesting(MakeReadyLauncher(helper_ptr));

  int calls = 0;
  bool accepted = false;
  service.OAuthCancel("state_xyz", base::BindLambdaForTesting([&](bool acc) {
    ++calls;
    accepted = acc;
  }));
  task_environment_.RunUntilIdle();
  EXPECT_EQ(0, calls);
  helper_ptr->ReplyOAuthCancel(true);
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1, calls);
  EXPECT_TRUE(accepted);

  // Case B: Helper is unavailable
  MahoMailService service_no_helper;
  int calls_no_helper = 0;
  bool accepted_no_helper = true;
  service_no_helper.OAuthCancel("state_xyz", base::BindLambdaForTesting([&](bool acc) {
    ++calls_no_helper;
    accepted_no_helper = acc;
  }));
  EXPECT_EQ(1, calls_no_helper);
  EXPECT_FALSE(accepted_no_helper);
}

TEST_F(MahoMailServiceTest, InjectKeysFailureDoesNotTransitionToReady) {
  ParkingMailHelper helper;
  helper.set_inject_keys_result(false);
  MahoMailHelperLauncher launcher;
  launcher.BindHelperForTesting(helper.BindNewPipe(), /*ready=*/false);
  EXPECT_EQ(nullptr, launcher.GetHelper());

  launcher.InjectDatabaseKeys("sql_key", "cred_key");
  task_environment_.RunUntilIdle();
  EXPECT_EQ(nullptr, launcher.GetHelper());
}

// Badge unread computation (pure function; no Profile needed).
TEST(MahoMailBadgeComputeTest, SumsInboxUnreadAndPicksFirstInboxId) {
  std::string inbox_id;
  const int64_t unread = ComputeInboxUnreadForBadge(
      R"json([
        {"id":"f1","folder_type":"Sent","unread_count":3},
        {"id":"f2","folder_type":"Inbox","unread_count":5},
        {"id":"f3","folder_type":"Inbox","unread_count":2}
      ])json",
      &inbox_id);
  EXPECT_EQ(unread, 7);
  EXPECT_EQ(inbox_id, "f2");
}

TEST(MahoMailBadgeComputeTest, ZeroWhenNoInboxOrInvalid) {
  std::string id = "stale";
  EXPECT_EQ(ComputeInboxUnreadForBadge("not json", &id), 0);
  EXPECT_TRUE(id.empty());

  id = "stale";
  EXPECT_EQ(
      ComputeInboxUnreadForBadge(
          R"json([{"id":"f1","folder_type":"Sent","unread_count":4}])json", &id),
      0);
  EXPECT_TRUE(id.empty());

  // Missing unread_count defaults to 0 but the Inbox id is still captured.
  id.clear();
  EXPECT_EQ(ComputeInboxUnreadForBadge(
                R"json([{"id":"in","folder_type":"Inbox"}])json", &id),
            0);
  EXPECT_EQ(id, "in");
}

TEST(MahoMailBadgeStateTest, PrunesAccountsMissingFromLatestAccountList) {
  MailUnreadBadgeState state;
  const uint64_t account_a = state.BeginRefresh("a");
  const uint64_t account_b = state.BeginRefresh("b");
  EXPECT_TRUE(state.CompleteRefresh("a", account_a, 4));
  EXPECT_TRUE(state.CompleteRefresh("b", account_b, 7));
  EXPECT_EQ(state.total(), 11);

  EXPECT_TRUE(state.RetainAccounts({"b"}));
  EXPECT_EQ(state.total(), 7);
  EXPECT_FALSE(state.CompleteRefresh("a", account_a, 99));
  EXPECT_EQ(state.total(), 7);
}

TEST(MahoMailBadgeStateTest, IgnoresLateCallbackFromOlderRefresh) {
  MailUnreadBadgeState state;
  const uint64_t older = state.BeginRefresh("account");
  const uint64_t newer = state.BeginRefresh("account");

  EXPECT_TRUE(state.CompleteRefresh("account", newer, 2));
  EXPECT_FALSE(state.CompleteRefresh("account", older, 50));
  EXPECT_EQ(state.total(), 2);
}

TEST(MahoMailPreferenceTest, ParsesDesktopNotificationPreference) {
  EXPECT_EQ(GetDesktopNotificationPreference(
                R"json({"desktop_notifications":true})json"),
            std::optional<bool>(true));
  EXPECT_EQ(GetDesktopNotificationPreference(
                R"json({"desktop_notifications":false})json"),
            std::optional<bool>(false));
  EXPECT_EQ(GetDesktopNotificationPreference("{}"), std::nullopt);
  EXPECT_EQ(GetDesktopNotificationPreference(
                R"json({"desktop_notifications":"false"})json"),
            std::nullopt);
  EXPECT_EQ(GetDesktopNotificationPreference("not json"), std::nullopt);
}

TEST(MahoMailPreferenceTest, ParsesUnreadBadgePreference) {
  EXPECT_EQ(GetUnreadBadgePreference(
                R"json({"unread_badge_enabled":true})json"),
            std::optional<bool>(true));
  EXPECT_EQ(GetUnreadBadgePreference(
                R"json({"unread_badge_enabled":false})json"),
            std::optional<bool>(false));
  EXPECT_EQ(GetUnreadBadgePreference("{}"), std::nullopt);
  EXPECT_EQ(GetUnreadBadgePreference(
                R"json({"unread_badge_enabled":"false"})json"),
            std::nullopt);
  EXPECT_EQ(GetUnreadBadgePreference("not json"), std::nullopt);
}

TEST(MahoMailBehaviorContractTest, MigratesDefaultsAndPreservesExplicitFalse) {
  const auto defaults = ParseMailBehaviorSnapshot("{}", 11);
  ASSERT_TRUE(defaults.has_value());
  EXPECT_EQ(defaults->version, 1u);
  EXPECT_EQ(defaults->revision, 11u);
  EXPECT_TRUE(defaults->desktop_notifications);
  EXPECT_EQ(defaults->notification_preview,
            MailNotificationPreview::kSenderSubject);
  EXPECT_TRUE(defaults->unread_badge_enabled);

  const auto explicit_off = ParseMailBehaviorSnapshot(
      R"json({"desktop_notifications":false,"unread_badge_enabled":false})json",
      3);
  ASSERT_TRUE(explicit_off.has_value());
  EXPECT_FALSE(explicit_off->desktop_notifications);
  EXPECT_FALSE(explicit_off->unread_badge_enabled);
}

TEST(MahoMailBehaviorContractTest, AcceptsAllPreviewModes) {
  EXPECT_EQ(ParseMailBehaviorSnapshot(
                R"json({"notification_preview":"sender_subject"})json", 1)
                ->notification_preview,
            MailNotificationPreview::kSenderSubject);
  EXPECT_EQ(ParseMailBehaviorSnapshot(
                R"json({"notification_preview":"sender_only"})json", 1)
                ->notification_preview,
            MailNotificationPreview::kSenderOnly);
  EXPECT_EQ(ParseMailBehaviorSnapshot(
                R"json({"notification_preview":"generic"})json", 1)
                ->notification_preview,
            MailNotificationPreview::kGeneric);
}

TEST(MahoMailBehaviorContractTest, CompareAndSwapRejectsConflict) {
  MailBehaviorSnapshot current;
  current.revision = 6;
  const MailBehaviorUpdateResult result = ApplyMailBehaviorUpdate(
      current, /*expected_revision=*/5, "desktop_notifications", "false");
  EXPECT_EQ(result.status, MailBehaviorUpdateStatus::kConflict);
  EXPECT_EQ(result.snapshot, current);
}

TEST(MahoMailBehaviorContractTest, OsPermissionMapsDeniedAndUnsupported) {
  EXPECT_EQ(MapMailNotificationPermissionForTesting(
                /*platform_supported=*/true, /*permission_denied=*/true),
            MailOsNotificationPermission::kDenied);
  EXPECT_EQ(MapMailNotificationPermissionForTesting(
                /*platform_supported=*/false, /*permission_denied=*/false),
            MailOsNotificationPermission::kUnsupported);
}

TEST_F(MahoMailServiceTest, BehaviorHelperRejectionRollsBackMirror) {
  ParkingMailHelper helper;
  helper.set_app_setting(
      R"json({"revision":4,"desktop_notifications":true})json");
  helper.set_app_setting_accepts(false);
  MahoMailService service;
  service.SetLauncherForTesting(MakeReadyLauncher(&helper));

  int mirror_calls = 0;
  service.SetBehaviorMirrorCallbackForTesting(base::BindLambdaForTesting(
      [&](const MailBehaviorSnapshot&) { ++mirror_calls; }));
  std::optional<MailBehaviorUpdateResult> reply;
  service.UpdateBehavior(
      4, "desktop_notifications", "false",
      base::BindLambdaForTesting(
          [&](MailBehaviorUpdateResult result) { reply = std::move(result); }));
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->status, MailBehaviorUpdateStatus::kRejected);
  EXPECT_TRUE(reply->snapshot.desktop_notifications);
  EXPECT_EQ(mirror_calls, 0);
  EXPECT_EQ(helper.set_app_setting_calls(), 1);
}

TEST_F(MahoMailServiceTest, BehaviorSuccessfulCommitConvergesMirror) {
  ParkingMailHelper helper;
  helper.set_app_setting(
      R"json({"revision":2,"unread_badge_enabled":false})json");
  MahoMailService service;
  service.SetLauncherForTesting(MakeReadyLauncher(&helper));

  std::optional<MailBehaviorSnapshot> mirrored;
  service.SetBehaviorMirrorCallbackForTesting(base::BindLambdaForTesting(
      [&](const MailBehaviorSnapshot& snapshot) { mirrored = snapshot; }));
  std::optional<MailBehaviorUpdateResult> reply;
  service.UpdateBehavior(
      2, "unread_badge_enabled", "true",
      base::BindLambdaForTesting(
          [&](MailBehaviorUpdateResult result) { reply = std::move(result); }));
  task_environment_.RunUntilIdle();

  ASSERT_TRUE(reply.has_value());
  ASSERT_TRUE(mirrored.has_value());
  EXPECT_EQ(reply->status, MailBehaviorUpdateStatus::kApplied);
  EXPECT_EQ(reply->snapshot.revision, 3u);
  EXPECT_TRUE(reply->snapshot.unread_badge_enabled);
  EXPECT_EQ(*mirrored, reply->snapshot);
}

TEST_F(MahoMailServiceTest, PersistedMuteReplyPublishesOnlyOnSuccess) {
  class MuteObserver : public MahoMailService::Observer {
   public:
    void OnThreadMuteChanged(const std::string& account_id,
                             const std::string& message_id,
                             bool muted) override {
      events.emplace_back(account_id, message_id, muted);
    }
    std::vector<std::tuple<std::string, std::string, bool>> events;
  } observer;

  ParkingMailHelper helper;
  MahoMailService service;
  service.SetLauncherForTesting(MakeReadyLauncher(&helper));
  service.AddObserver(&observer);

  bool replied = false;
  service.MuteThread(
      "account-a", "thread-a",
      base::BindLambdaForTesting([&](bool ok, std::string) { replied = ok; }));
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(replied);
  EXPECT_EQ(observer.events,
            (std::vector<std::tuple<std::string, std::string, bool>>{
                {"account-a", "thread-a", true}}));

  helper.set_mute_accepts(false);
  service.UnmuteThread(
      "account-a", "thread-a",
      base::BindLambdaForTesting([&](bool ok, std::string) { replied = ok; }));
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(replied);
  EXPECT_EQ(observer.events.size(), 1u)
      << "rejected persistence must not diverge coordinator policy";
  service.RemoveObserver(&observer);
}

}  // namespace
}  // namespace maho
