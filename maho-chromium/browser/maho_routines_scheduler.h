// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAHO_ROUTINES_SCHEDULER_H_
#define MAHO_BROWSER_MAHO_ROUTINES_SCHEDULER_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"

struct MahoCore;

namespace maho {

class MahoRoutinesScheduler {
 public:
  static constexpr base::TimeDelta kStatusTickInterval = base::Seconds(30);

  explicit MahoRoutinesScheduler(MahoCore* core);
  ~MahoRoutinesScheduler();

  MahoRoutinesScheduler(const MahoRoutinesScheduler&) = delete;
  MahoRoutinesScheduler& operator=(const MahoRoutinesScheduler&) = delete;

  void Start();
  void Stop();

  // Drives exactly one scheduler pass synchronously, as the repeating timer
  // would. Tests use this to exercise the Vault auto-lock evaluation and the
  // lock-state broadcast without waiting on wall-clock time.
  void RunTickForTesting(int64_t now_sec);

  // Parses the `lockState` token out of a maho_vault_status_json payload.
  // Anything other than "unlocked" (including unparsable JSON) reports locked,
  // so a malformed status fails closed. Public so tests can pin the parse.
  static bool IsLockedVaultStatusJson(const std::string& status_json);

 private:
  void OnTick();
  void RunTick(int64_t now_sec);

  // Reads the Vault lock state from the core and publishes it through
  // maho::NotifyVaultLockStateChanged when it differs from the last broadcast.
  void PublishVaultLockState();

  raw_ptr<MahoCore> core_;
  base::RepeatingTimer timer_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_MAHO_ROUTINES_SCHEDULER_H_
