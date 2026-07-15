// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_service.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "maho/browser/mail_helper/maho_mail_helper.mojom.h"
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
class ParkingMailHelper : public mojom::MahoMailHelper {
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

  // mojom::MahoMailHelper: non-read methods are unused by these tests.
  void Initialize(const std::string&,
                  const std::string&,
                  InitializeCallback callback) override {
    std::move(callback).Run(true);
  }
  void InjectKeys(const std::string&,
                  const std::string&,
                  InjectKeysCallback callback) override {
    std::move(callback).Run(true);
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

 private:
  mojo::Receiver<mojom::MahoMailHelper> receiver_{this};
  int read_calls_ = 0;
  ListAccountsCallback parked_list_accounts_;
  ListFoldersCallback parked_list_folders_;
  ListEmailsCallback parked_list_emails_;
  GetEmailCallback parked_get_email_;
  SearchEmailsCallback parked_search_emails_;
  ListThreadCallback parked_list_thread_;
  PrepareShutdownCallback parked_prepare_shutdown_;
  ShutdownCallback parked_shutdown_;
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

}  // namespace
}  // namespace maho
