// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_HELPER_LAUNCHER_H_
#define MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_HELPER_LAUNCHER_H_

#include <string>

#include "base/files/file_path.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "maho/browser/mail_helper/maho_mail_helper.mojom.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace maho {

// Owns the out-of-process mail helper: spawns it, verifies a version handshake,
// respawns it with exponential backoff on crash (bounded by a crash-loop cap),
// and force-closes it on shutdown (best-effort, non-blocking).
class MahoMailHelperLauncher {
 public:
  MahoMailHelperLauncher();
  ~MahoMailHelperLauncher();

  MahoMailHelperLauncher(const MahoMailHelperLauncher&) = delete;
  MahoMailHelperLauncher& operator=(const MahoMailHelperLauncher&) = delete;

  // Starts the helper. `expected_version` is compared against the version the
  // helper reports; a mismatch triggers kill + relaunch (bounded by the cap).
  // `crashpad_database` is forwarded so the helper's Crashpad handler shares the
  // browser's crash database; may be empty.
  void Launch(const std::string& expected_version,
              const std::string& profile_path,
              const base::FilePath& crashpad_database = base::FilePath());

  void InjectDatabaseKeys(const std::string& sqlcipher_key,
                          const std::string& credential_key);

  // Graceful drain: PrepareShutdown + Shutdown over Mojo, wait for the ack up to
  // a timeout, then force-terminate.
  void Shutdown();

  // Returns the bound helper interface only after the version + Initialize
  // handshake has marked the launcher ready. Before readiness (process spawned
  // but handshake incomplete), during a crash-triggered rebind, or after a
  // disconnect this returns nullptr so callers cannot dispatch reads to a
  // helper that has not yet proven itself. No request queueing is performed.
  mojom::MahoMailHelper* GetHelper() const {
    return (ready_ && helper_.is_bound()) ? helper_.get() : nullptr;
  }

  // True once the crash-loop cap was exceeded and the launcher gave up.
  bool has_given_up() const { return gave_up_; }

  // Test-only: binds `remote` as the helper and forces the ready state,
  // bypassing the process spawn + version/Initialize handshake. Lets unit
  // tests exercise the readiness gate and the service read proxies against a
  // fake helper without launching a real process.
  void BindHelperForTesting(mojo::PendingRemote<mojom::MahoMailHelper> remote,
                            bool ready);

  private:
   void LaunchInternal();
   void OnVersionReported(const std::string& helper_version);
   void OnInitialized(bool success);
   void OnConnectionError();
   void HandleFailureAndMaybeRelaunch();
   void ReinjectKeysIfPresent();
   void ForceClose();
   base::TimeDelta ComputeBackoff() const;

  // Crash-loop policy: at most kMaxRespawns respawns within kCrashWindow before
  // the launcher gives up and surfaces an error state.
  static constexpr int kMaxRespawns = 5;
  static constexpr base::TimeDelta kCrashWindow = base::Seconds(60);
   static constexpr base::TimeDelta kBaseBackoff = base::Milliseconds(500);
   static constexpr base::TimeDelta kMaxBackoff = base::Seconds(30);

  std::string expected_version_;
  std::string profile_path_;
  base::FilePath crashpad_database_;

  bool has_keys_ = false;
  std::string sqlcipher_key_;
  std::string credential_key_;

   bool shutting_down_ = false;
   bool gave_up_ = false;
   bool ready_ = false;

  int respawn_count_ = 0;
  base::TimeTicks crash_window_start_;

  base::Process process_;
   mojo::Remote<mojom::MahoMailHelper> helper_;

   base::OneShotTimer relaunch_timer_;

   base::WeakPtrFactory<MahoMailHelperLauncher> weak_ptr_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_HELPER_LAUNCHER_H_
