// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_service.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "maho/browser/mail_helper/maho_mail_helper.mojom.h"
#include "maho/browser/mail_helper/maho_mail_helper.mojom-test-utils.h"
#include "maho/browser/mail_helper/maho_mail_helper_launcher.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

constexpr char kUnavailable[] = "mail helper unavailable";

// Fake helper that never answers a read request: each read callback is parked
// so the reply stays pending until the test drops the connection, reproducing
// a helper that disconnects / is reset / shuts down mid-flight.
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

  int read_calls() const { return read_calls_; }
  void set_inject_keys_result(bool success) { inject_keys_result_ = success; }

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
  void OAuthStartUrl(const std::string&, const std::string&, const std::string&, OAuthStartUrlCallback callback) override {
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
    std::move(callback).Run(true);
  }

  void OAuthLoopbackSignIn(const std::string&, OAuthLoopbackSignInCallback callback) override {
    std::move(callback).Run(true, "{}");
  }

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
  PrepareShutdownCallback parked_prepare_shutdown_;
  ShutdownCallback parked_shutdown_;
  mojo::Remote<mojom::MahoMailHelperClient> client_;
  bool inject_keys_result_ = true;
};

std::unique_ptr<MahoMailHelperLauncher> MakeReadyLauncher(
    ParkingMailHelper* helper) {
  auto launcher = std::make_unique<MahoMailHelperLauncher>();
  launcher->BindHelperForTesting(helper->BindNewPipe(), /*ready=*/true);
  return launcher;
}

class MahoMailServiceTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

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
  EXPECT_NE(nullptr, launcher.GetHelper());
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

}  // namespace
}  // namespace maho
