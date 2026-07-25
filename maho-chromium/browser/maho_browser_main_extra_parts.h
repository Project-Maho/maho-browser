#ifndef MAHO_BROWSER_MAHO_BROWSER_MAIN_EXTRA_PARTS_H_
#define MAHO_BROWSER_MAHO_BROWSER_MAIN_EXTRA_PARTS_H_

#include <memory>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/power_monitor/power_observer.h"
#include "build/build_config.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "maho/browser/extensions/maho_extension_state_bridge.h"
#include "maho/browser/maho_routines_scheduler.h"

class Profile;
class BrowserWindowInterface;
struct MahoCore;

namespace maho {
class MahoMcpSocketServer;
class MahoMcpPipeServer;
class MahoMcpSessionToken;
class MahoSyncRelayClient;
class MahoExtensionSidePanelCoordinator;
}  // namespace maho

class MahoBrowserMainExtraParts : public ChromeBrowserMainExtraParts,
                                  public base::PowerSuspendObserver {
  public:
   MahoBrowserMainExtraParts();
   MahoBrowserMainExtraParts(const MahoBrowserMainExtraParts&) = delete;
   MahoBrowserMainExtraParts& operator=(const MahoBrowserMainExtraParts&) = delete;
   ~MahoBrowserMainExtraParts() override;

   static MahoBrowserMainExtraParts* GetInstance();
    void MaybeInitializeForBrowser(BrowserWindowInterface* browser);

    MahoCore* GetCore() const { return core_; }
    void OnSuspendForTesting() { OnSuspend(); }

   private:
    void PostEarlyInitialization() override;
    void PostProfileInit(Profile* profile, bool is_initial_profile) override;
    void PostMainMessageLoopRun() override;
    void OnSuspend() override;
    void InitializeRegularServicesOnce(Profile* profile);
    void MaybeLaunchMailHelper(Profile* profile);
    void StartObservingPowerSuspend();
    void StopObservingPowerSuspend();
    void LockVaultForLifecycle();
   // Idempotent Maho core + updater teardown shared by PostMainMessageLoopRun
   // and the destructor fallback. No-op if the core is already gone.
   void ShutdownCoreAndServices();

   base::FilePath storage_path_;
    RAW_PTR_EXCLUSION MahoCore* core_ = nullptr;
    RAW_PTR_EXCLUSION Profile* regular_profile_ = nullptr;
    bool observing_power_suspend_ = false;

   std::unique_ptr<maho::MahoExtensionStateBridge> extension_state_bridge_;
   std::unique_ptr<maho::MahoExtensionSidePanelCoordinator>
       extension_side_panel_coordinator_;
   std::unique_ptr<maho::MahoRoutinesScheduler> routines_scheduler_;
   std::unique_ptr<maho::MahoSyncRelayClient> sync_relay_client_;

 #if BUILDFLAG(IS_WIN)
   std::unique_ptr<maho::MahoMcpSessionToken> mcp_session_token_;
   std::unique_ptr<maho::MahoMcpPipeServer> mcp_pipe_server_;
 #else
   std::unique_ptr<maho::MahoMcpSocketServer> mcp_socket_server_;
 #endif
 };

#endif  // MAHO_BROWSER_MAHO_BROWSER_MAIN_EXTRA_PARTS_H_
