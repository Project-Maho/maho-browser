// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_HELPER_LAUNCHER_H_
#define MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_HELPER_LAUNCHER_H_

#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "maho/browser/mail_helper/maho_mail_helper.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace maho {

// Owns the out-of-process mail helper: spawns it, verifies a version handshake,
// respawns it with exponential backoff on crash (bounded by a crash-loop cap),
// and force-closes it on shutdown (best-effort, non-blocking).
class MahoMailHelperLauncher : public mojom::MahoMailHelperClient {
 public:
  class Observer {
   public:
    virtual void OnAuthRequired(const std::string& account_id,
                                const std::string& provider,
                                const std::string& reason) = 0;
    virtual void OnAuthRefreshSucceeded(const std::string& account_id) = 0;
    virtual void OnAccountsChanged() = 0;
    virtual void OnHelperReady() {}
    virtual void OnHelperFailed(bool will_retry) {}
    virtual void OnSyncEvent(const std::string& event_type, const std::string& payload) {}
    virtual void OnBackfillEvent(const std::string& event_type, const std::string& payload) {}
    virtual void OnStatusChanged(const std::string& account_id, bool connected) {}
    virtual void OnNewMail(const std::string& account_id,
                           const std::string& email_id,
                           const std::string& message_id,
                           const std::string& sender,
                           const std::string& subject,
                           uint64_t cursor,
                           uint64_t epoch) {}
    virtual void OnMutation(const std::string& account_id, const std::string& payload) {}
    virtual void OnOutbox(const std::string& account_id, const std::string& payload) {}
    virtual void OnScheduler(const std::string& account_id, const std::string& payload) {}
    virtual void OnAgentStream(const std::string& session_id, const std::string& chunk) {}
    virtual void OnCalendar(const std::string& account_id, const std::string& payload) {}
    virtual void OnImport(const std::string& payload) {}
  };

  MahoMailHelperLauncher();
  ~MahoMailHelperLauncher() override;

  void SetObserver(Observer* observer) { observer_ = observer; }

  // mojom::MahoMailHelperClient:
  void OnSyncEvent(const std::string& event_type, const std::string& payload) override;
  void OnBackfillEvent(const std::string& event_type, const std::string& payload) override;
  void OnStatusChanged(const std::string& account_id, bool connected) override;
  void OnAuthRequired(const std::string& account_id,
                      const std::string& provider,
                      const std::string& reason) override;
  void OnAuthRefreshSucceeded(const std::string& account_id) override;
  void OnAccountsChanged() override;
  void OnNewMail(const std::string& account_id,
                 const std::string& email_id,
                 const std::string& message_id,
                 const std::string& sender,
                 const std::string& subject,
                 uint64_t cursor,
                 uint64_t epoch) override;
  void OnMutation(const std::string& account_id, const std::string& payload) override;
  void OnOutbox(const std::string& account_id, const std::string& payload) override;
  void OnScheduler(const std::string& account_id, const std::string& payload) override;
  void OnAgentStream(const std::string& session_id, const std::string& chunk) override;
  void OnCalendar(const std::string& account_id, const std::string& payload) override;
  void OnImport(const std::string& payload) override;

  MahoMailHelperLauncher(const MahoMailHelperLauncher&) = delete;
  MahoMailHelperLauncher& operator=(const MahoMailHelperLauncher&) = delete;

  // Starts the helper. `expected_version` is compared against the version the
  // helper reports; a mismatch triggers kill + relaunch (bounded by the cap).
  // `crashpad_database` is forwarded so the helper's Crashpad handler shares the
  // browser's crash database; may be empty.
  virtual void Launch(
      const std::string& expected_version,
      const std::string& profile_path,
      const base::FilePath& crashpad_database = base::FilePath());

  virtual void InjectDatabaseKeys(const std::string& sqlcipher_key,
                                  const std::string& credential_key);

  // Graceful drain: PrepareShutdown + Shutdown over Mojo, wait for the ack up to
  // a timeout, then force-terminate.
  virtual void Shutdown(
      base::OnceClosure drained_callback = base::DoNothing());

  // Returns the bound helper interface only after the version + Initialize
  // handshake has marked the launcher ready. Before readiness (process spawned
  // but handshake incomplete), during a crash-triggered rebind, or after a
  // disconnect this returns nullptr so callers cannot dispatch reads to a
  // helper that has not yet proven itself. No request queueing is performed.
  virtual mojom::MahoMailHelper* GetHelper() const;

  // True once the crash-loop cap was exceeded and the launcher gave up.
  bool has_given_up() const { return gave_up_; }

  // Test-only: binds `remote` as the helper and forces the ready state,
  // bypassing the process spawn + version/Initialize handshake. Lets unit
  // tests exercise the readiness gate and the service read proxies against a
  // fake helper without launching a real process.
  void BindHelperForTesting(mojo::PendingRemote<mojom::MahoMailHelper> remote,
                            bool ready);

  // Test-only: completes the version/Initialize handshake that
  // BindHelperForTesting bypasses, driving the same state transition as
  // OnInitialized(true) — keys cached before this point are injected now, and
  // readiness is only reached after both handshake and injection succeed.
  void CompleteHandshakeForTesting() {
    initialized_ = true;
    ReinjectKeysIfPresent();
  }

  void SetShutdownTimeoutForTesting(base::TimeDelta timeout) {
    shutdown_timeout_ = timeout;
  }
  void NotifyReadyForTesting() {
    ready_ = true;
    if (observer_) {
      observer_->OnHelperReady();
    }
  }
  void NotifyFailureForTesting(bool will_retry) {
    ready_ = false;
    if (observer_) {
      observer_->OnHelperFailed(will_retry);
    }
  }

  // Replaces only process creation; tests retain the production handshake and
  // failure/relaunch policy on a real Mojo pipe.
  void SetHelperFactoryForTesting(
      base::RepeatingCallback<mojo::PendingRemote<mojom::MahoMailHelper>()>
          factory) {
    helper_factory_for_testing_ = std::move(factory);
  }

  private:
   void LaunchInternal();
   void BeginHandshake(mojo::PendingRemote<mojom::MahoMailHelper> remote);
   void OnVersionReported(const std::string& helper_version);
   void OnInitialized(bool success);
   void OnConnectionError();
   void HandleFailureAndMaybeRelaunch();
   void ReinjectKeysIfPresent();
  void StartKeyInjectionDeadline();
   void OnKeysInjected(bool success);
   void OnPrepareShutdownComplete();
   void OnShutdownComplete();
   void OnShutdownTimeout();
   void CompleteShutdown();
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
   bool initialized_ = false;
   bool keys_sent_ = false;

  int respawn_count_ = 0;
  base::TimeTicks crash_window_start_;

  base::RepeatingCallback<mojo::PendingRemote<mojom::MahoMailHelper>()>
      helper_factory_for_testing_;
  base::Process process_;
  mojo::Remote<mojom::MahoMailHelper> helper_;
  mojo::Receiver<mojom::MahoMailHelperClient> client_receiver_{this};

  // One deadline for version, initialization, and key injection together.
  base::OneShotTimer startup_timer_;
  base::OneShotTimer relaunch_timer_;
  base::OneShotTimer shutdown_timer_;
  base::TimeDelta shutdown_timeout_ = base::Seconds(5);
  std::vector<base::OnceClosure> drained_callbacks_;

  raw_ptr<Observer> observer_ = nullptr;

  base::WeakPtrFactory<MahoMailHelperLauncher> weak_ptr_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_HELPER_LAUNCHER_H_
