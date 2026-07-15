// Copyright 2026 Maho Browser. All rights reserved.

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "mojo/core/embedder/embedder.h"
#include "mojo/public/cpp/platform/platform_channel.h"
#include "mojo/public/cpp/system/invitation.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "maho/browser/mail_helper/maho_mail_helper.mojom.h"

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

  void Shutdown(ShutdownCallback callback) override {
    LOG(INFO) << "[MahoMailHelper] Shutdown requested.";
    std::move(callback).Run();
    if (quit_closure_) {
      std::move(quit_closure_).Run();
    }
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
