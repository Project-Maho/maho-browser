#ifndef MAHO_BROWSER_MAHO_BROWSER_MAIN_EXTRA_PARTS_H_
#define MAHO_BROWSER_MAHO_BROWSER_MAIN_EXTRA_PARTS_H_

#include <cstddef>
#include <memory>
#include <string>

#include "base/functional/callback.h"

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/memory/weak_ptr.h"
#include "base/power_monitor/power_observer.h"
#include "base/time/time.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "maho/browser/maho_routines_scheduler.h"

class Profile;
class BrowserWindowInterface;
struct MahoCore;

namespace maho {
class MahoMcpBrowserDelegate;

// Instantiates the production delegate, replacing only Mail's active-profile
// lookup and native approval UI. Callbacks run on the owning UI sequence.
std::unique_ptr<MahoMcpBrowserDelegate> CreateMailDelegateForTesting(
    base::RepeatingCallback<Profile*()> profile_resolver,
    base::RepeatingCallback<bool(const std::string&, const std::string&)>
        approval_presenter);

class MahoMcpSocketServer;
class MahoMcpPipeServer;
class MahoMcpSessionToken;
class MahoMailNotificationCoordinator;
class MahoMailNotificationCoordinatorRegistry;
class MahoSyncRelayClient;
}  // namespace maho

class MahoBrowserMainExtraParts : public ChromeBrowserMainExtraParts,
                                  public base::PowerSuspendObserver {
  public:
   MahoBrowserMainExtraParts();
   MahoBrowserMainExtraParts(const MahoBrowserMainExtraParts&) = delete;
   MahoBrowserMainExtraParts& operator=(const MahoBrowserMainExtraParts&) = delete;
   ~MahoBrowserMainExtraParts() override;

   static MahoBrowserMainExtraParts* GetInstance();
   enum class CoreStartupStageForTesting {
     kStorageOpen,
     kHydrationBegin,
     kHydrationComplete,
   };
   // Install before startup and remove after startup work has settled. The
   // observer does not replace creation, hydration, publication or teardown.
   static void SetCoreStartupObserverForTesting(
       base::RepeatingCallback<void(CoreStartupStageForTesting)> observer);
    void MaybeInitializeForBrowser(BrowserWindowInterface* browser);

    MahoCore* GetCore() const { return core_; }
    void OnSuspendForTesting() { OnSuspend(); }
    void AddMcpVaultCredentialLeaseForTesting(
        base::TimeTicks expires_at,
        bool promoted,
        Profile* lease_profile = nullptr);
    void SweepMcpVaultCredentialLeasesForTesting(base::TimeTicks now);
    bool PromoteMcpVaultCredentialLeaseForTesting(Profile* resolved_profile,
                                                  base::TimeTicks now);
    size_t McpVaultCredentialLeaseCountForTesting() const;
    size_t MailNotificationCoordinatorCountForTesting() const;
    void EnsureMailNotificationCoordinatorForTesting(Profile* profile) {
      EnsureMailNotificationCoordinator(profile);
    }

   private:
    enum class StartupState {
      kUninitialized,
      kLoading,
      kReady,
      kFailed,
      kClosing,
      kClosed,
    };
    struct CoreStartupResult;
    using CoreStartupResultPtr =
        std::unique_ptr<CoreStartupResult, base::OnTaskRunnerDeleter>;
    void PostEarlyInitialization() override;
    void PostProfileInit(Profile* profile, bool is_initial_profile) override;
    void PostMainMessageLoopRun() override;
    void OnSuspend() override;
    void InitializeRegularServicesOnce(Profile* profile);
    void EnsureMailNotificationCoordinator(Profile* profile);
    void OnRegularStorageDirectoryReady(base::WeakPtr<Profile> profile,
                                        bool storage_directory_ready);
    void OnStorageKeySetupComplete(base::WeakPtr<Profile> profile,
                                   bool storage_key_ready);
    void OnCoreStartupComplete(base::WeakPtr<Profile> profile,
                               uint64_t generation,
                               CoreStartupResultPtr result);
    void MaybeLaunchMailHelper(Profile* profile);
    void OnMailHelperCrashpadReady(Profile* profile,
                                   const base::FilePath& crashpad_database,
                                   bool crashpad_directory_ready);
    void StartObservingPowerSuspend();
    void StopObservingPowerSuspend();
    void LockVaultForLifecycle();
   // Idempotent Maho core + updater teardown shared by PostMainMessageLoopRun
   // and the destructor fallback. No-op if the core is already gone.
   void ShutdownCoreAndServices();

   base::FilePath storage_path_;
     RAW_PTR_EXCLUSION MahoCore* core_ = nullptr;
     bool observing_power_suspend_ = false;
     StartupState startup_state_ = StartupState::kUninitialized;
     uint64_t startup_generation_ = 0;

    std::unique_ptr<maho::MahoRoutinesScheduler> routines_scheduler_;
    std::unique_ptr<maho::MahoMailNotificationCoordinatorRegistry>
        mail_notification_coordinators_;
    std::unique_ptr<maho::MahoSyncRelayClient> sync_relay_client_;

  #if BUILDFLAG(IS_WIN)
   std::unique_ptr<maho::MahoMcpSessionToken> mcp_session_token_;
   std::unique_ptr<maho::MahoMcpPipeServer> mcp_pipe_server_;
 #else
   std::unique_ptr<maho::MahoMcpSocketServer> mcp_socket_server_;
 #endif

    base::WeakPtrFactory<MahoBrowserMainExtraParts> weak_factory_{this};
  };

#endif  // MAHO_BROWSER_MAHO_BROWSER_MAIN_EXTRA_PARTS_H_
