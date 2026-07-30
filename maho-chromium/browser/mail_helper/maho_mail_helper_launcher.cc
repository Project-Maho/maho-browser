// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_helper_launcher.h"

#include <utility>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "build/build_config.h"
#if BUILDFLAG(IS_MAC)
#include "base/apple/foundation_util.h"
#endif
#include "mojo/public/cpp/platform/platform_channel.h"
#include "mojo/public/cpp/system/invitation.h"

namespace maho {

namespace {
#if BUILDFLAG(IS_MAC)
constexpr char kBaseBundleIdSwitch[] = "maho-mail-base-bundle-id";
#endif
constexpr char kCrashpadDatabaseSwitch[] = "maho-mail-crashpad-database";

#if BUILDFLAG(IS_WIN)
constexpr char kHelperExecutableName[] = "maho_mail_helper.exe";
#else
constexpr char kHelperExecutableName[] = "maho_mail_helper";
#endif
}  // namespace

MahoMailHelperLauncher::MahoMailHelperLauncher() = default;

MahoMailHelperLauncher::~MahoMailHelperLauncher() {
  ForceClose();
}

void MahoMailHelperLauncher::Launch(const std::string& expected_version,
                                    const std::string& profile_path,
                                    const base::FilePath& crashpad_database) {
  if (process_.IsValid()) {
    return;
  }
  expected_version_ = expected_version;
  profile_path_ = profile_path;
  crashpad_database_ = crashpad_database;
  shutting_down_ = false;
  gave_up_ = false;
  respawn_count_ = 0;
  crash_window_start_ = base::TimeTicks::Now();
  LaunchInternal();
}

void MahoMailHelperLauncher::LaunchInternal() {
  if (shutting_down_ || gave_up_) {
    return;
  }

  base::FilePath exe_dir;
  if (!base::PathService::Get(base::DIR_EXE, &exe_dir)) {
    LOG(ERROR) << "[MahoMailHelperLauncher] Failed to resolve DIR_EXE.";
    HandleFailureAndMaybeRelaunch();
    return;
  }
  base::FilePath helper_path = exe_dir.AppendASCII(kHelperExecutableName);

  base::CommandLine cmd_line(helper_path);
  if (!crashpad_database_.empty()) {
    cmd_line.AppendSwitchPath(kCrashpadDatabaseSwitch, crashpad_database_);
  }
#if BUILDFLAG(IS_MAC)
  cmd_line.AppendSwitchASCII(kBaseBundleIdSwitch,
                             std::string(base::apple::BaseBundleID()));
#endif

  mojo::PlatformChannel channel;
  mojo::OutgoingInvitation invitation;
  mojo::ScopedMessagePipeHandle pipe =
      invitation.AttachMessagePipe("maho_mail_pipe");

  base::LaunchOptions options;
  channel.PrepareToPassRemoteEndpoint(&options, &cmd_line);

  process_ = base::LaunchProcess(cmd_line, options);
  channel.RemoteProcessLaunchAttempted();

  if (!process_.IsValid()) {
    LOG(ERROR) << "[MahoMailHelperLauncher] Failed to launch helper process.";
    HandleFailureAndMaybeRelaunch();
    return;
  }

  mojo::OutgoingInvitation::Send(std::move(invitation), process_.Handle(),
                                 channel.TakeLocalEndpoint());

  ready_ = false;
  helper_.Bind(mojo::PendingRemote<mojom::MahoMailHelper>(std::move(pipe), 0));
  helper_.set_disconnect_handler(
      base::BindOnce(&MahoMailHelperLauncher::OnConnectionError,
                     weak_ptr_factory_.GetWeakPtr()));

  // Verify the version handshake before trusting the helper with any work.
  helper_->GetVersion(
      base::BindOnce(&MahoMailHelperLauncher::OnVersionReported,
                     weak_ptr_factory_.GetWeakPtr()));
}

void MahoMailHelperLauncher::OnVersionReported(
    const std::string& helper_version) {
  if (shutting_down_) {
    return;
  }
  if (helper_version != expected_version_) {
    LOG(ERROR) << "[MahoMailHelperLauncher] Version mismatch: helper reported '"
               << helper_version << "', expected '" << expected_version_
               << "'. Killing and relaunching.";
    HandleFailureAndMaybeRelaunch();
    return;
  }

  LOG(INFO) << "[MahoMailHelperLauncher] Version handshake verified ("
            << helper_version << ").";

  helper_->Initialize(
      expected_version_, profile_path_,
      base::BindOnce(&MahoMailHelperLauncher::OnInitialized,
                     weak_ptr_factory_.GetWeakPtr()));
}

void MahoMailHelperLauncher::OnInitialized(bool success) {
  if (shutting_down_) {
    return;
  }

  if (!success) {
    LOG(ERROR) << "[MahoMailHelperLauncher] Helper initialization failed; "
                  "relaunching helper.";
    HandleFailureAndMaybeRelaunch();
    return;
  }

  LOG(INFO) << "[MahoMailHelperLauncher] Helper initialized successfully.";

  mojo::PendingRemote<mojom::MahoMailHelperClient> client_remote;
  client_receiver_.reset();
  client_receiver_.Bind(client_remote.InitWithNewPipeAndPassReceiver());
  helper_->BindClient(std::move(client_remote));

  ReinjectKeysIfPresent();
}

void MahoMailHelperLauncher::OnSyncEvent(const std::string& event_type, const std::string& payload) {
  LOG(INFO) << "[MahoMailHelperLauncher] OnSyncEvent: " << event_type;
  if (observer_) {
    observer_->OnSyncEvent(event_type, payload);
  }
}

void MahoMailHelperLauncher::OnBackfillEvent(const std::string& event_type, const std::string& payload) {
  LOG(INFO) << "[MahoMailHelperLauncher] OnBackfillEvent: " << event_type;
  if (observer_) {
    observer_->OnBackfillEvent(event_type, payload);
  }
}

void MahoMailHelperLauncher::OnStatusChanged(const std::string& account_id, bool connected) {
  LOG(INFO) << "[MahoMailHelperLauncher] OnStatusChanged for " << account_id << ": " << connected;
  if (observer_) {
    observer_->OnStatusChanged(account_id, connected);
  }
}

void MahoMailHelperLauncher::OnAuthRequired(const std::string& account_id,
                                            const std::string& provider,
                                            const std::string& reason) {
  LOG(WARNING) << "[MahoMailHelperLauncher] OnAuthRequired for " << account_id << " (provider: " << provider << "): " << reason;
  if (observer_) {
    observer_->OnAuthRequired(account_id, provider, reason);
  }
}

void MahoMailHelperLauncher::OnAuthRefreshSucceeded(const std::string& account_id) {
  LOG(INFO) << "[MahoMailHelperLauncher] OnAuthRefreshSucceeded for " << account_id;
  if (observer_) {
    observer_->OnAuthRefreshSucceeded(account_id);
  }
}

void MahoMailHelperLauncher::OnAccountsChanged() {
  LOG(INFO) << "[MahoMailHelperLauncher] OnAccountsChanged";
  if (observer_) {
    observer_->OnAccountsChanged();
  }
}

void MahoMailHelperLauncher::OnNewMail(const std::string& account_id) {
  LOG(INFO) << "[MahoMailHelperLauncher] OnNewMail for " << account_id;
  if (observer_) {
    observer_->OnNewMail(account_id);
  }
}

void MahoMailHelperLauncher::OnMutation(const std::string& account_id, const std::string& payload) {
  LOG(INFO) << "[MahoMailHelperLauncher] OnMutation for " << account_id;
  if (observer_) {
    observer_->OnMutation(account_id, payload);
  }
}

void MahoMailHelperLauncher::OnOutbox(const std::string& account_id, const std::string& payload) {
  LOG(INFO) << "[MahoMailHelperLauncher] OnOutbox for " << account_id;
  if (observer_) {
    observer_->OnOutbox(account_id, payload);
  }
}

void MahoMailHelperLauncher::OnScheduler(const std::string& account_id, const std::string& payload) {
  LOG(INFO) << "[MahoMailHelperLauncher] OnScheduler for " << account_id;
  if (observer_) {
    observer_->OnScheduler(account_id, payload);
  }
}

void MahoMailHelperLauncher::OnAgentStream(const std::string& session_id, const std::string& chunk) {
  if (observer_) {
    observer_->OnAgentStream(session_id, chunk);
  }
}

void MahoMailHelperLauncher::OnCalendar(const std::string& account_id, const std::string& payload) {
  LOG(INFO) << "[MahoMailHelperLauncher] OnCalendar for " << account_id;
  if (observer_) {
    observer_->OnCalendar(account_id, payload);
  }
}

void MahoMailHelperLauncher::OnImport(const std::string& payload) {
  LOG(INFO) << "[MahoMailHelperLauncher] OnImport";
  if (observer_) {
    observer_->OnImport(payload);
  }
}

void MahoMailHelperLauncher::ReinjectKeysIfPresent() {
  if (!has_keys_ || !helper_.is_bound()) {
    return;
  }
  helper_->InjectKeys(
      sqlcipher_key_, credential_key_,
      base::BindOnce(&MahoMailHelperLauncher::OnKeysInjected,
                     weak_ptr_factory_.GetWeakPtr()));
}

void MahoMailHelperLauncher::OnKeysInjected(bool success) {
  if (shutting_down_) {
    return;
  }
  if (success) {
    ready_ = true;
    LOG(INFO) << "[MahoMailHelperLauncher] Key injection/re-injection result: SUCCESS. Launcher is now ready.";
  } else {
    LOG(ERROR) << "[MahoMailHelperLauncher] Key injection/re-injection failed; relaunching helper.";
    HandleFailureAndMaybeRelaunch();
  }
}

void MahoMailHelperLauncher::InjectDatabaseKeys(
    const std::string& sqlcipher_key,
    const std::string& credential_key) {
  // Cache the keys so a post-crash respawn can re-inject them without waiting
  // for the caller to notice the restart.
  has_keys_ = true;
  sqlcipher_key_ = sqlcipher_key;
  credential_key_ = credential_key;

  if (!helper_.is_bound()) {
    LOG(WARNING) << "[MahoMailHelperLauncher] Helper not bound; keys cached "
                    "for initialization or respawn.";
    return;
  }
  helper_->InjectKeys(
      sqlcipher_key, credential_key,
      base::BindOnce(&MahoMailHelperLauncher::OnKeysInjected,
                     weak_ptr_factory_.GetWeakPtr()));
}

void MahoMailHelperLauncher::BindHelperForTesting(
    mojo::PendingRemote<mojom::MahoMailHelper> remote,
    bool ready) {
  helper_.reset();
  helper_.Bind(std::move(remote));
  ready_ = ready;
}

void MahoMailHelperLauncher::OnConnectionError() {
  if (shutting_down_) {
    LOG(INFO) << "[MahoMailHelperLauncher] Helper disconnected during shutdown.";
    return;
  }
  LOG(ERROR) << "[MahoMailHelperLauncher] Connection error to helper process.";
  HandleFailureAndMaybeRelaunch();
}

void MahoMailHelperLauncher::HandleFailureAndMaybeRelaunch() {
  if (shutting_down_) {
    return;
  }

  ready_ = false;
  helper_.reset();
  if (process_.IsValid()) {
    process_.Terminate(/*exit_code=*/0, /*wait=*/false);
    process_.Close();
  }

  const base::TimeTicks now = base::TimeTicks::Now();
  if (now - crash_window_start_ > kCrashWindow) {
    crash_window_start_ = now;
    respawn_count_ = 0;
  }

  if (respawn_count_ >= kMaxRespawns) {
    gave_up_ = true;
    LOG(ERROR) << "[MahoMailHelperLauncher] Crash-loop cap reached ("
               << kMaxRespawns << " respawns within " << kCrashWindow.InSeconds()
               << "s). Giving up; mail helper is unavailable.";
    return;
  }

  const base::TimeDelta backoff = ComputeBackoff();
  ++respawn_count_;
  LOG(WARNING) << "[MahoMailHelperLauncher] Scheduling respawn #"
               << respawn_count_ << " in " << backoff.InMilliseconds() << "ms.";
  relaunch_timer_.Start(FROM_HERE, backoff,
                        base::BindOnce(&MahoMailHelperLauncher::LaunchInternal,
                                       weak_ptr_factory_.GetWeakPtr()));
}

base::TimeDelta MahoMailHelperLauncher::ComputeBackoff() const {
  // Exponential: kBaseBackoff * 2^respawn_count_, capped at kMaxBackoff.
  base::TimeDelta backoff = kBaseBackoff;
  for (int i = 0; i < respawn_count_ && backoff < kMaxBackoff; ++i) {
    backoff *= 2;
  }
  return backoff > kMaxBackoff ? kMaxBackoff : backoff;
}

void MahoMailHelperLauncher::Shutdown() {
  // Best-effort, non-blocking teardown. KeyedService::Shutdown() is synchronous
  // and this launcher is destroyed immediately afterward, so we must not spin a
  // nested UI-thread RunLoop nor capture stack pointers across async Mojo
  // replies to await a graceful ack. Dropping the remote signals disconnect to
  // the helper (letting it begin its own drain) and the process is then force-
  // terminated. FOLLOW-UP RISK: the graceful PrepareShutdown -> Shutdown Mojo
  // drain (IMAP IDLE / in-flight SMTP) is no longer awaited here.
  ForceClose();
}

void MahoMailHelperLauncher::ForceClose() {
  shutting_down_ = true;
  relaunch_timer_.Stop();
  ready_ = false;
  helper_.reset();
  if (process_.IsValid()) {
    process_.Terminate(/*exit_code=*/0, /*wait=*/false);
    process_.Close();
  }
}

}  // namespace maho
