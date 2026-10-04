// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAHO_TAB_ID_SESSION_HELPER_H_
#define MAHO_BROWSER_MAHO_TAB_ID_SESSION_HELPER_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/supports_user_data.h"
#include "base/callback_list.h"
#include "chrome/browser/ui/browser_tab_strip_tracker.h"
#include "chrome/browser/ui/browser_tab_strip_tracker_delegate.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"

class Profile;
class PrefRegistrySimple;
struct MahoCore;

namespace maho {

class MahoTabIdSessionHelper : public base::SupportsUserData::Data,
                               public TabStripModelObserver,
                               public BrowserTabStripTrackerDelegate {
 public:
  static void RegisterProfilePrefs(PrefRegistrySimple* registry);
  static MahoTabIdSessionHelper* GetForProfile(Profile* profile);

  explicit MahoTabIdSessionHelper(Profile* profile);
  ~MahoTabIdSessionHelper() override;

  MahoTabIdSessionHelper(const MahoTabIdSessionHelper&) = delete;
  MahoTabIdSessionHelper& operator=(const MahoTabIdSessionHelper&) = delete;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // BrowserTabStripTrackerDelegate:
  bool ShouldTrackBrowser(BrowserWindowInterface* browser) override;

 private:
  void SaveTabIdToSession(content::WebContents* contents);
  void OnSessionRestoreFinished(Profile* profile, int num_tabs_restored);
  void PerformSessionMigration(Profile* profile, ::MahoCore* core);

  raw_ptr<Profile> profile_;
  std::unique_ptr<BrowserTabStripTracker> tracker_;
  base::CallbackListSubscription session_restored_subscription_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_MAHO_TAB_ID_SESSION_HELPER_H_
