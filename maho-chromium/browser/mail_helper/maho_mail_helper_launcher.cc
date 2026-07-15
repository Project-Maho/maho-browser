// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_helper_launcher.h"

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "mojo/public/cpp/platform/platform_channel.h"
#include "mojo/public/cpp/system/invitation.h"

namespace maho {

MahoMailHelperLauncher::MahoMailHelperLauncher() = default;

MahoMailHelperLauncher::~MahoMailHelperLauncher() {
  Shutdown();
}

void MahoMailHelperLauncher::Launch(const std::string& version_token,
                                    const std::string& profile_path) {
  if (process_.IsValid()) {
    return;
  }

  // Resolve helper executable path
  base::FilePath exe_dir;
  if (!base::PathService::Get(base::DIR_EXE, &exe_dir)) {
    LOG(ERROR) << "[MahoMailHelperLauncher] Failed to resolve DIR_EXE.";
    return;
  }
  base::FilePath helper_path = exe_dir.AppendASCII("maho_mail_helper");

  base::CommandLine cmd_line(helper_path);

  mojo::PlatformChannel channel;
  mojo::OutgoingInvitation invitation;
  mojo::ScopedMessagePipeHandle pipe = invitation.AttachMessagePipe("maho_mail_pipe");

  base::LaunchOptions options;
  channel.PrepareToPassRemoteEndpoint(&options, &cmd_line);

  process_ = base::LaunchProcess(cmd_line, options);
  channel.RemoteProcessLaunchAttempted();

  if (!process_.IsValid()) {
    LOG(ERROR) << "[MahoMailHelperLauncher] Failed to launch helper process.";
    return;
  }

  mojo::OutgoingInvitation::Send(std::move(invitation), process_.Handle(),
                                 channel.TakeLocalEndpoint());

  helper_.Bind(mojo::PendingRemote<mojom::MahoMailHelper>(std::move(pipe), 0));
  helper_.set_disconnect_handler(
      base::BindOnce(&MahoMailHelperLauncher::OnConnectionError,
                     weak_ptr_factory_.GetWeakPtr()));

  // Call Initialize via Mojo
  helper_->Initialize(version_token, profile_path,
                      base::BindOnce([](bool success) {
                        LOG(INFO) << "[MahoMailHelperLauncher] Helper initialization result: "
                                  << (success ? "SUCCESS" : "FAILURE");
                      }));
}

void MahoMailHelperLauncher::InjectDatabaseKeys(const std::string& sqlcipher_key,
                                                const std::string& credential_key) {
  if (!helper_.is_bound()) {
    LOG(WARNING) << "[MahoMailHelperLauncher] Cannot inject keys; helper not bound.";
    return;
  }
  helper_->InjectKeys(sqlcipher_key, credential_key,
                      base::BindOnce([](bool success) {
                        LOG(INFO) << "[MahoMailHelperLauncher] Key injection result: "
                                  << (success ? "SUCCESS" : "FAILURE");
                      }));
}

void MahoMailHelperLauncher::Shutdown() {
  if (helper_.is_bound()) {
    helper_->Shutdown(base::BindOnce([]() {
      LOG(INFO) << "[MahoMailHelperLauncher] Helper shutdown acknowledged.";
    }));
    helper_.reset();
  }

  if (process_.IsValid()) {
    process_.Close();
  }
}

void MahoMailHelperLauncher::OnConnectionError() {
  LOG(ERROR) << "[MahoMailHelperLauncher] Connection error to helper process.";
  Shutdown();
}

}  // namespace maho
