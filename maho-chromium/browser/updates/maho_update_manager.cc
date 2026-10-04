// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/maho_update_manager.h"

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/metrics/histogram_functions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "components/version_info/version_info.h"
#include "maho/browser/updates/crash_loop_sentinel.h"
#include "maho/browser/updates/maho_product_version.h"
#include "maho/browser/updates/maho_update_pref_names.h"
#include "maho/browser/updates/maho_update_server_url.h"
#include "maho/browser/updates/platform_updater_delegate.h"
#include "maho/browser/updates/rollout_bucket.h"

namespace maho {

namespace {

constexpr base::TimeDelta kInitialAutoCheckDelay = base::Seconds(30);
constexpr base::TimeDelta kAutoCheckInterval = base::Hours(4);

constexpr char kSimulateUpdateStateSwitch[] = "maho-simulate-update-state";

struct SimulateMapping {
  std::string_view name;
  UpdateState state;
  UpdateError error;
};

constexpr SimulateMapping kSimulateMappings[] = {
    {"idle", UpdateState::kIdle, UpdateError::kNone},
    {"checking", UpdateState::kChecking, UpdateError::kNone},
    {"update_available", UpdateState::kUpdateAvailable, UpdateError::kNone},
    {"downloading", UpdateState::kDownloading, UpdateError::kNone},
    {"verifying", UpdateState::kVerifying, UpdateError::kNone},
    {"ready_to_install", UpdateState::kReadyToInstall, UpdateError::kNone},
    {"up_to_date", UpdateState::kUpToDate, UpdateError::kNone},
    {"error_generic", UpdateState::kError, UpdateError::kUnknown},
    {"error_crash_loop", UpdateState::kError, UpdateError::kCrashLoop},
};

const SimulateMapping* FindSimulateMapping(std::string_view name) {
  for (const auto& m : kSimulateMappings) {
    if (m.name == name) {
      return &m;
    }
  }
  return nullptr;
}

// Stub delegate used as a fallback if no platform delegate is provided.
class StubPlatformUpdaterDelegate : public PlatformUpdaterDelegate {
 public:
  StubPlatformUpdaterDelegate() = default;
  ~StubPlatformUpdaterDelegate() override = default;

  void Initialize() override {
    LOG(INFO) << "Stub update delegate initialized.";
  }

  void Check(bool manual_check) override {
    LOG(INFO) << "Stub update check initiated (manual: " << manual_check << ").";
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce([]() {
          MahoUpdateManager::GetInstance()->TransitionToState(UpdateState::kChecking);
        }));
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce([]() {
          MahoUpdateManager::GetInstance()->TransitionToState(UpdateState::kUpToDate);
        }),
        base::Milliseconds(300));
  }

  void SetChannel(const std::string& channel_name) override {
    LOG(INFO) << "Stub update delegate channel set to: " << channel_name;
  }

  void ApplyUpdateAndRestart() override {
    LOG(INFO) << "Stub update delegate apply and restart triggered.";
  }

  std::string GetUpdateGuidance() const override { return std::string(); }
};

std::string ChannelToString(UpdateChannel channel) {
  switch (channel) {
    case UpdateChannel::kStable: return "stable";
    case UpdateChannel::kBeta: return "beta";
    case UpdateChannel::kDev: return "dev";
    case UpdateChannel::kCanary: return "canary";
  }
}

UpdateChannel StringToChannel(const std::string& str) {
  if (str == "beta") return UpdateChannel::kBeta;
  if (str == "dev") return UpdateChannel::kDev;
  if (str == "canary") return UpdateChannel::kCanary;
  return UpdateChannel::kStable;
}

}  // namespace

// Factory function implemented in platform-specific delegates.
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
std::unique_ptr<PlatformUpdaterDelegate> CreatePlatformUpdaterDelegate();
#endif

// static
MahoUpdateManager* MahoUpdateManager::GetInstance() {
  return base::Singleton<MahoUpdateManager>::get();
}

MahoUpdateManager::MahoUpdateManager() {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

MahoUpdateManager::~MahoUpdateManager() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void MahoUpdateManager::Initialize(Profile* profile, PrefService* local_state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (initialized_) {
    return;
  }
  initialized_ = true;
  profile_ = profile;

  std::string current_version(updates::kMahoProductVersion);

  // Kill-switch: when maho.update.enabled is false (default), the entire
  // update subsystem stays inert. No delegate is created, no checks run,
  // no UMA is recorded. This is the staged-rollout safety net described in
  // the implementation plan §7.
  if (local_state && !local_state->GetBoolean(prefs::kMahoUpdateEnabled)) {
    is_disabled_ = true;
    LOG(INFO) << "Maho: Auto-update is disabled by kill-switch pref.";
    return;
  }

  server_base_url_ = updates::ResolveUpdateServerBaseUrl(profile_);
  if (server_base_url_ != std::string(updates::kMahoUpdateServerDefaultUrl)) {
    LOG(WARNING) << "Maho: update server base URL is overridden to "
                 << server_base_url_;
  }

  // Generate install_id if empty
  if (local_state) {
    std::string install_id = local_state->GetString(prefs::kMahoUpdateInstallId);
    if (install_id.empty()) {
      install_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
      local_state->SetString(prefs::kMahoUpdateInstallId, install_id);
    }
    int bucket = updates::RolloutBucket::Compute(install_id);
    rollout_bucket_ = bucket;
    base::UmaHistogramExactLinear("Maho.Update.RolloutBucket", bucket, 101);
  }

  // 1. Crash loop detection
  if (local_state) {
    if (updates::CrashLoopSentinel::CheckAndMarkStart(local_state, current_version)) {
      is_crash_loop_tripped_ = true;
      local_state->SetBoolean(prefs::kMahoUpdateRollbackRequested, true);
      TransitionToError(UpdateError::kCrashLoop);
      LOG(WARNING) << "Maho: Auto-update disabled due to crash loop.";
    }

    // Schedule stable runtime sentinel clearing.
    // SAFETY: This task is posted to the current sequence (UI thread) via
    // GetCurrentDefault(), and Initialize() is guarded by
    // DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_) above. |local_state|
    // is the caller-provided BrowserProcess-lifetime local state, guaranteed
    // to outlive any 60-second delayed task posted here at browser startup.
    // No cross-thread hazard; same-sequence access only.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&updates::CrashLoopSentinel::MarkStable,
                       base::Unretained(local_state), current_version),
        base::Seconds(60));
  }

  // 2. Initialize channel configuration
  std::string channel_str = "stable";
  if (profile_ && profile_->GetPrefs()) {
    channel_str = profile_->GetPrefs()->GetString(prefs::kMahoUpdateChannel);
  }
  channel_ = StringToChannel(channel_str);

  // 3. Initialize delegate if not in a crash loop
  if (!is_crash_loop_tripped_ && !delegate_) {
    InitializePlatformUpdater();
  }

  // 4. --maho-simulate-update-state debug switch. Forces a state for
  //    visual QA without a relay or fixture release. Suppresses both the
  //    auto-check timer and any future delegate->Check() calls so the
  //    forced state is stable until the user quits.
  const auto* cmdline = base::CommandLine::ForCurrentProcess();
  if (cmdline->HasSwitch(kSimulateUpdateStateSwitch)) {
    std::string name =
        cmdline->GetSwitchValueASCII(kSimulateUpdateStateSwitch);
    const SimulateMapping* mapping = FindSimulateMapping(name);
    if (mapping) {
      simulate_mode_ = true;
      LOG(WARNING) << "Maho: simulating update state '" << name
                   << "'; auto-check timer suppressed.";
      if (mapping->state == UpdateState::kError) {
        last_error_ = mapping->error;
      }
      TransitionToState(mapping->state);
    } else {
      LOG(ERROR) << "Maho: unknown --" << kSimulateUpdateStateSwitch
                 << " value '" << name
                 << "'. Valid values: idle, checking, update_available, "
                    "downloading, verifying, ready_to_install, up_to_date, "
                    "error_generic, error_crash_loop.";
    }
  }

  // 5. Schedule the first auto-check unless simulating.
  if (!is_crash_loop_tripped_ && !simulate_mode_) {
    StartAutoCheckTimer(kInitialAutoCheckDelay);
  }
}

void MahoUpdateManager::InitializePlatformUpdater() {
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
  delegate_ = CreatePlatformUpdaterDelegate();
#endif
  if (!delegate_) {
    delegate_ = std::make_unique<StubPlatformUpdaterDelegate>();
  }
  delegate_->Initialize();
  delegate_->SetChannel(ChannelToString(channel_));
  if (version_history_handler_) {
    delegate_->SetVersionHistoryHandler(version_history_handler_);
  }
}

void MahoUpdateManager::CheckForUpdates(bool manual_check) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_disabled_ || simulate_mode_) {
    return;
  }
  if (is_crash_loop_tripped_) {
    LOG(WARNING) << "Maho: Cannot check for updates; crash loop is tripped.";
    return;
  }
  if (!delegate_) {
    return;
  }

  check_started_at_ = base::TimeTicks::Now();
  delegate_->Check(manual_check);
}

void MahoUpdateManager::ApplyUpdateAndRestart() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_disabled_ || is_crash_loop_tripped_ || simulate_mode_) {
    return;
  }
  if (state_ != UpdateState::kReadyToInstall) {
    return;
  }
  if (delegate_) {
    delegate_->ApplyUpdateAndRestart();
  }
}

void MahoUpdateManager::SetVersionHistoryHandler(
    base::RepeatingClosure handler) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  version_history_handler_ = std::move(handler);
  if (delegate_) {
    delegate_->SetVersionHistoryHandler(version_history_handler_);
  }
}

void MahoUpdateManager::SetChannel(UpdateChannel channel) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  channel_ = channel;
  std::string channel_str = ChannelToString(channel);

  if (profile_ && profile_->GetPrefs()) {
    profile_->GetPrefs()->SetString(prefs::kMahoUpdateChannel, channel_str);
  }

  // Re-resolve the base URL so a runtime pref change to
  // maho.update.server_url is picked up at the next Check. The env-var
  // override still wins; if it is unset, this call lets the new pref value
  // take effect without a process restart.
  server_base_url_ = updates::ResolveUpdateServerBaseUrl(profile_);

  if (delegate_) {
    delegate_->SetChannel(channel_str);
  }

  TransitionToState(UpdateState::kIdle);
}

UpdateChannel MahoUpdateManager::GetChannel() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return channel_;
}

bool MahoUpdateManager::IsEnabled() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return !is_disabled_;
}

void MahoUpdateManager::AddObserver(MahoUpdateObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.AddObserver(observer);
}

void MahoUpdateManager::RemoveObserver(MahoUpdateObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observers_.RemoveObserver(observer);
}

UpdateState MahoUpdateManager::GetState() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return state_;
}

UpdateError MahoUpdateManager::GetLastError() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return last_error_;
}

std::string MahoUpdateManager::GetUpdateGuidance() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return delegate_ ? delegate_->GetUpdateGuidance() : std::string();
}

const std::string& MahoUpdateManager::GetServerBaseUrl() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return server_base_url_;
}

int MahoUpdateManager::GetRolloutBucket() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return rollout_bucket_;
}

void MahoUpdateManager::TransitionToState(UpdateState new_state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ == new_state) {
    return;
  }
  RecordCheckLatencyIfNeeded(new_state);
  state_ = new_state;
  base::UmaHistogramEnumeration("Maho.Update.State", state_);
  if (state_ != UpdateState::kError) {
    last_error_ = UpdateError::kNone;
  }
  for (auto& observer : observers_) {
    observer.OnUpdateStateChanged(state_);
  }
}

void MahoUpdateManager::RecordCheckLatencyIfNeeded(UpdateState new_state) {
  if (state_ != UpdateState::kChecking) {
    return;
  }
  if (check_started_at_.is_null()) {
    return;
  }
  base::TimeDelta latency = base::TimeTicks::Now() - check_started_at_;
  base::UmaHistogramTimes("Maho.Update.CheckLatency", latency);
  check_started_at_ = base::TimeTicks();
}

void MahoUpdateManager::TransitionToError(UpdateError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  last_error_ = error;
  base::UmaHistogramEnumeration("Maho.Update.LastError", last_error_);
  TransitionToState(UpdateState::kError);
}

void MahoUpdateManager::NotifyProgress(double percent) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& observer : observers_) {
    observer.OnUpdateProgress(percent);
  }
}

void MahoUpdateManager::SetDelegateForTesting(std::unique_ptr<PlatformUpdaterDelegate> delegate) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  delegate_ = std::move(delegate);
  if (delegate_) {
    delegate_->Initialize();
    delegate_->SetChannel(ChannelToString(channel_));
    if (version_history_handler_) {
      delegate_->SetVersionHistoryHandler(version_history_handler_);
    }
  }
}

void MahoUpdateManager::ResetForTesting() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  initialized_ = false;
  is_crash_loop_tripped_ = false;
  is_disabled_ = false;
  simulate_mode_ = false;
  state_ = UpdateState::kIdle;
  last_error_ = UpdateError::kNone;
  channel_ = UpdateChannel::kStable;
  check_started_at_ = base::TimeTicks();
  server_base_url_.clear();
  auto_check_timer_.Stop();
  auto_check_interval_for_testing_ = base::TimeDelta();
  version_history_handler_ = base::RepeatingClosure();
  delegate_.reset();
  profile_ = nullptr;
  while (!observers_.empty()) {
    observers_.RemoveObserver(&*observers_.begin());
  }
}

void MahoUpdateManager::set_auto_check_interval_for_testing(
    base::TimeDelta interval) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto_check_interval_for_testing_ = interval;
}

bool MahoUpdateManager::is_simulating_for_testing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return simulate_mode_;
}

void MahoUpdateManager::StartAutoCheckTimer(base::TimeDelta delay) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!auto_check_interval_for_testing_.is_zero()) {
    delay = auto_check_interval_for_testing_;
  } else if (delay.is_zero()) {
    return;
  }
  auto_check_timer_.Start(
      FROM_HERE, delay,
      base::BindOnce(&MahoUpdateManager::OnAutoCheckTimerFired,
                     base::Unretained(this)));
}

void MahoUpdateManager::OnAutoCheckTimerFired() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_disabled_ || is_crash_loop_tripped_) {
    return;
  }
  CheckForUpdates(false);
  base::TimeDelta next_interval = auto_check_interval_for_testing_.is_zero()
                                      ? kAutoCheckInterval
                                      : auto_check_interval_for_testing_;
  StartAutoCheckTimer(next_interval);
}

}  // namespace maho
