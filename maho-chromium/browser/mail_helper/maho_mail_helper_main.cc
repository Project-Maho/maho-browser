// Copyright 2026 Maho Browser. All rights reserved.

#include <map>
#include <string>
#include <vector>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "build/build_config.h"
#include "maho/browser/mail_helper/maho_mail_helper.mojom.h"
#include "maho/browser/mail_helper/maho_mail_helper_version.h"
#include "maho/browser/mail_helper/maho_mail_read_bridge.h"
#include "mojo/core/embedder/embedder.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/platform/platform_channel.h"
#include "mojo/public/cpp/system/invitation.h"
#include "third_party/crashpad/crashpad/client/crashpad_client.h"

namespace {

// Command-line switch through which the host launcher passes the Crashpad
// database directory (shared with the browser's crash pipeline).
constexpr char kCrashpadDatabaseSwitch[] = "maho-mail-crashpad-database";

// Name of the shared Crashpad handler executable shipped alongside the browser.
#if BUILDFLAG(IS_WIN)
constexpr char kCrashpadHandlerName[] = "chrome_crashpad_handler.exe";
#else
constexpr char kCrashpadHandlerName[] = "chrome_crashpad_handler";
#endif

// Installs the Crashpad handler so native crashes AND Rust panics/aborts
// (which lower to SIGABRT / structured exceptions) in this helper feed the
// same crash pipeline as the browser. Best-effort: logs and continues if the
// handler or database cannot be resolved so a crash-reporting failure never
// prevents the mail helper from starting.
void InstallCrashpadHandler(const base::CommandLine& cmd_line) {
  base::FilePath exe_dir;
  if (!base::PathService::Get(base::DIR_EXE, &exe_dir)) {
    LOG(ERROR) << "[MahoMailHelper] Crashpad: failed to resolve DIR_EXE.";
    return;
  }

  base::FilePath handler_path = exe_dir.AppendASCII(kCrashpadHandlerName);

  base::FilePath database_path =
      cmd_line.GetSwitchValuePath(kCrashpadDatabaseSwitch);
  if (database_path.empty()) {
    database_path = exe_dir.AppendASCII("Crashpad");
  }

  crashpad::CrashpadClient client;
  const std::map<std::string, std::string> annotations = {
      {"ptype", "maho-mail-helper"},
      {"maho-mail-helper-version", maho::mail_helper::kMahoMailHelperVersion},
  };
  const std::vector<std::string> arguments = {"--no-rate-limit"};

  if (!client.StartHandler(handler_path, database_path,
                           /*metrics_dir=*/base::FilePath(),
                           /*url=*/std::string(), annotations, arguments,
                           /*restartable=*/true,
                           /*asynchronous_start=*/false)) {
    LOG(ERROR) << "[MahoMailHelper] Crashpad handler failed to start; "
                  "continuing without crash reporting.";
    return;
  }
  LOG(INFO) << "[MahoMailHelper] Crashpad handler installed.";
}

}  // namespace

// FFI Declarations linked to maho_mail_ffi (Rust backend)
extern "C" {
bool MahoMailInitialize(const char* version_token, const char* profile_path);
bool MahoMailInjectKeys(const char* sqlcipher_key, const char* credential_key);
bool MahoMailStartSync(const char* account_id);
bool MahoMailStopSync();
bool MahoMailStartBackfill(const char* account_id);
}

namespace maho {

class MahoMailHelperImpl : public mojom::MahoMailHelper {
 public:
  MahoMailHelperImpl(mojo::PendingReceiver<mojom::MahoMailHelper> receiver,
                     base::OnceClosure quit_closure)
      : receiver_(this, std::move(receiver)),
        quit_closure_(std::move(quit_closure)) {}

  ~MahoMailHelperImpl() override = default;

  // mojom::MahoMailHelper:
  void Initialize(const std::string& version_token,
                  const std::string& profile_path,
                  InitializeCallback callback) override {
    LOG(INFO) << "[MahoMailHelper] Initializing. Version=" << version_token
              << " Profile=" << profile_path;
    bool success = MahoMailInitialize(version_token.c_str(), profile_path.c_str());
    std::move(callback).Run(success);
  }

  void InjectKeys(const std::string& sqlcipher_key,
                  const std::string& credential_key,
                  InjectKeysCallback callback) override {
    LOG(INFO) << "[MahoMailHelper] Injecting DB keys.";
    bool success = MahoMailInjectKeys(sqlcipher_key.c_str(), credential_key.c_str());
    std::move(callback).Run(success);
  }

  void StartSync(const std::string& account_id, StartSyncCallback callback) override {
    LOG(INFO) << "[MahoMailHelper] Starting sync for account: " << account_id;
    bool success = MahoMailStartSync(account_id.c_str());
    std::move(callback).Run(success);
  }

  void StopSync(StopSyncCallback callback) override {
    LOG(INFO) << "[MahoMailHelper] Stopping sync.";
    bool success = MahoMailStopSync();
    std::move(callback).Run(success);
  }

  void StartBackfill(const std::string& account_id, StartBackfillCallback callback) override {
    LOG(INFO) << "[MahoMailHelper] Starting backfill for account: " << account_id;
    bool success = MahoMailStartBackfill(account_id.c_str());
    std::move(callback).Run(success);
  }

  void GetVersion(GetVersionCallback callback) override {
    std::move(callback).Run(maho::mail_helper::kMahoMailHelperVersion);
  }

  void PrepareShutdown(PrepareShutdownCallback callback) override {
    LOG(INFO) << "[MahoMailHelper] PrepareShutdown requested; draining backend.";
    std::move(callback).Run();
  }

  void Shutdown(ShutdownCallback callback) override {
    LOG(INFO) << "[MahoMailHelper] Shutdown requested.";
    std::move(callback).Run();
    if (quit_closure_) {
      std::move(quit_closure_).Run();
    }
  }

  void ListAccounts(ListAccountsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(&MahoMailListAccounts),
        base::BindOnce(
            [](ListAccountsCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void ListFolders(const std::string& account_id,
                   ListFoldersCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string id, MahoMailReadCallback cb, void* ud) {
              return MahoMailListFolders(id.c_str(), cb, ud);
            },
            account_id),
        base::BindOnce(
            [](ListFoldersCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void ListEmails(const std::string& account_id,
                  const std::string& folder_id,
                  int64_t limit,
                  int64_t offset,
                  ListEmailsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string aid, std::string fid, int64_t lim, int64_t off,
               MahoMailReadCallback cb, void* ud) {
              return MahoMailListEmails(aid.c_str(), fid.c_str(), lim, off, cb,
                                       ud);
            },
            account_id, folder_id, limit, offset),
        base::BindOnce(
            [](ListEmailsCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void GetEmail(const std::string& email_id,
                GetEmailCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string id, MahoMailReadCallback cb, void* ud) {
              return MahoMailGetEmail(id.c_str(), cb, ud);
            },
            email_id),
        base::BindOnce(
            [](GetEmailCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void SearchEmails(const std::string& query_json,
                    SearchEmailsCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string qj, MahoMailReadCallback cb, void* ud) {
              return MahoMailSearchEmails(qj.c_str(), cb, ud);
            },
            query_json),
        base::BindOnce(
            [](SearchEmailsCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

  void ListThread(const std::string& account_id,
                  const std::string& message_id,
                  ListThreadCallback callback) override {
    MahoMailReadBridge::Start(
        base::BindOnce(
            [](std::string aid, std::string mid, MahoMailReadCallback cb,
               void* ud) { return MahoMailListThread(aid.c_str(), mid.c_str(), cb, ud); },
            account_id, message_id),
        base::BindOnce(
            [](ListThreadCallback cb, bool ok, std::string json) {
              std::move(cb).Run(ok, std::move(json));
            },
            std::move(callback)));
  }

 private:
  mojo::Receiver<mojom::MahoMailHelper> receiver_;
  base::OnceClosure quit_closure_;
};

}  // namespace maho

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);

  LOG(INFO) << "[MahoMailHelper] Starting helper process...";

  InstallCrashpadHandler(*base::CommandLine::ForCurrentProcess());

  // Initialize Mojo
  mojo::core::Init();

  base::SingleThreadTaskExecutor main_task_executor;
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("maho_mail_helper_thread_pool");

  base::RunLoop run_loop;

  auto endpoint = mojo::PlatformChannel::RecoverPassedEndpointFromCommandLine(
      *base::CommandLine::ForCurrentProcess());

  std::unique_ptr<maho::MahoMailHelperImpl> helper_impl;

  if (endpoint.is_valid()) {
    mojo::IncomingInvitation invitation = mojo::IncomingInvitation::Accept(std::move(endpoint));
    mojo::ScopedMessagePipeHandle pipe = invitation.ExtractMessagePipe("maho_mail_pipe");

    helper_impl = std::make_unique<maho::MahoMailHelperImpl>(
        mojo::PendingReceiver<maho::mojom::MahoMailHelper>(std::move(pipe)),
        run_loop.QuitClosure());
    LOG(INFO) << "[MahoMailHelper] Mojo communication established.";
  } else {
    LOG(ERROR) << "[MahoMailHelper] Invalid PlatformChannel endpoint.";
    return 1;
  }

  run_loop.Run();
  return 0;
}
