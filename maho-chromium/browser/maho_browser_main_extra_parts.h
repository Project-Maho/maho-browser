#ifndef MAHO_BROWSER_MAHO_BROWSER_MAIN_EXTRA_PARTS_H_
#define MAHO_BROWSER_MAHO_BROWSER_MAIN_EXTRA_PARTS_H_

#include "chrome/browser/chrome_browser_main_extra_parts.h"
#include "base/memory/raw_ptr_exclusion.h"

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

  RAW_PTR_EXCLUSION MahoCore* core_ = nullptr;
};

#endif  // MAHO_BROWSER_MAHO_BROWSER_MAIN_EXTRA_PARTS_H_
