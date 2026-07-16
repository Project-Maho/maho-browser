// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UPDATES_MAHO_UPDATE_MANAGER_H_
#define MAHO_BROWSER_UPDATES_MAHO_UPDATE_MANAGER_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/singleton.h"
#include "base/observer_list.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"

#include "base/component_export.h"

class Profile;
class PrefService;

namespace maho {

enum class UpdateState {
  kIdle,
  kChecking,
  kUpdateAvailable,
  kDownloading,
  kVerifying,        // signature/hash verification of the downloaded payload
  kReadyToInstall,
  kUpToDate,
  kError,
  kMaxValue = kError,
};

enum class UpdateChannel {
  kStable,
  kBeta,
  kDev,
  kCanary,
};

enum class UpdateError {
  kNone,
  kCrashLoop,
  kSignatureMismatch,
  kManifestUnsigned,
  kHashMismatch,
  kAuthenticodeFailed,
  kConnectionFailed,
  kDownloadFailed,
  kUnknown,
  kMaxValue = kUnknown,
};

class MahoUpdateObserver : public base::CheckedObserver {
 public:
  ~MahoUpdateObserver() override = default;
  virtual void OnUpdateStateChanged(UpdateState state) = 0;
  virtual void OnUpdateProgress(double percent) = 0;
};

class PlatformUpdaterDelegate;

class COMPONENT_EXPORT(MAHO_UPDATES) MahoUpdateManager {
 public:
  static MahoUpdateManager* GetInstance();

  // Idempotent. Safe to call from PostProfileInit on every profile init.
  // |local_state| is the browser-process local state (owned by the caller,
  // i.e. g_browser_process->local_state()); passed in so this component does
  // not reference the framework-level g_browser_process global directly.
  void Initialize(::Profile* profile, PrefService* local_state);

  // |manual_check| surfaces user-visible UI on "no updates"; background
  // checks stay silent on that path.
  void CheckForUpdates(bool manual_check);

  // Applies a payload that has already reached kReadyToInstall and
  // restarts the browser. No-op in any other state.
  void ApplyUpdateAndRestart();

  // Channel selection is persisted in local prefs and reflected in the
  // appcast / feed URL on the next check.
  void SetChannel(UpdateChannel channel);
  UpdateChannel GetChannel() const;

  void AddObserver(MahoUpdateObserver* observer);
  void RemoveObserver(MahoUpdateObserver* observer);

  UpdateState GetState() const;
  UpdateError GetLastError() const;

  // Returns the resolved update server base URL with no trailing slash.
  // Cached during Initialize from env var > pref > compile-time default.
  const std::string& GetServerBaseUrl() const;

  // Rollout bucket [0,100) derived from the install id during Initialize.
  // Platform delegates read this instead of the framework local state so the
  // updates component does not reference g_browser_process directly.
  int GetRolloutBucket() const;

  // Internal state transition helpers. Called by delegates.
  void TransitionToState(UpdateState new_state);
  void TransitionToError(UpdateError error);
  void NotifyProgress(double percent);

  // Sets a mock delegate for unit testing.
  void SetDelegateForTesting(std::unique_ptr<PlatformUpdaterDelegate> delegate);

  // Resets all internal state to defaults. Tests must call this in SetUp()
  // because GetInstance() returns a process-wide singleton whose state would
  // otherwise leak across tests.
  void ResetForTesting();

  // Overrides the auto-check cadence for tests. Must be called BEFORE
  // Initialize(). Pass base::TimeDelta() to disable auto-checks entirely.
  void set_auto_check_interval_for_testing(base::TimeDelta interval);

  // Returns true if the manager was forced into a state by the
  // --maho-simulate-update-state command-line switch. Production code does
  // not need to read this; tests use it to assert the parser worked.
  bool is_simulating_for_testing() const;

 private:
  MahoUpdateManager();
  ~MahoUpdateManager();
  friend struct base::DefaultSingletonTraits<MahoUpdateManager>;

  void InitializePlatformUpdater();
  void RecordCheckLatencyIfNeeded(UpdateState new_state);
  void StartAutoCheckTimer(base::TimeDelta delay);
  void OnAutoCheckTimerFired();

  SEQUENCE_CHECKER(sequence_checker_);
  bool initialized_ = false;
  bool is_crash_loop_tripped_ = false;
  int rollout_bucket_ = 0;
  bool is_disabled_ = false;
  bool simulate_mode_ = false;
  UpdateState state_ = UpdateState::kIdle;
  UpdateError last_error_ = UpdateError::kNone;
  UpdateChannel channel_ = UpdateChannel::kStable;
  base::TimeTicks check_started_at_;
  std::string server_base_url_;
  base::OneShotTimer auto_check_timer_;
  base::TimeDelta auto_check_interval_for_testing_;
  base::ObserverList<MahoUpdateObserver> observers_;
  std::unique_ptr<PlatformUpdaterDelegate> delegate_;
  raw_ptr<::Profile> profile_ = nullptr;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UPDATES_MAHO_UPDATE_MANAGER_H_
