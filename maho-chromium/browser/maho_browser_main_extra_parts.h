#ifndef MAHO_BROWSER_MAHO_BROWSER_MAIN_EXTRA_PARTS_H_
#define MAHO_BROWSER_MAHO_BROWSER_MAIN_EXTRA_PARTS_H_

#include <memory>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "maho/browser/extensions/maho_extension_state_bridge.h"
#include "maho/browser/maho_routines_scheduler.h"

class Profile;
struct MahoCore;

class MahoBrowserMainExtraParts : public ChromeBrowserMainExtraParts {
 public:
  MahoBrowserMainExtraParts();
  MahoBrowserMainExtraParts(const MahoBrowserMainExtraParts&) = delete;
  MahoBrowserMainExtraParts& operator=(const MahoBrowserMainExtraParts&) = delete;
  ~MahoBrowserMainExtraParts() override;

  MahoCore* GetCore() const { return core_; }

 private:
  void PostEarlyInitialization() override;
  void PostProfileInit(Profile* profile, bool is_initial_profile) override;
  void PostMainMessageLoopRun() override;

  base::FilePath storage_path_;
  RAW_PTR_EXCLUSION MahoCore* core_ = nullptr;

  std::unique_ptr<maho::MahoExtensionStateBridge> extension_state_bridge_;
  std::unique_ptr<maho::MahoRoutinesScheduler> routines_scheduler_;
  base::CallbackListSubscription session_restored_subscription_;
};

#endif  // MAHO_BROWSER_MAHO_BROWSER_MAIN_EXTRA_PARTS_H_
