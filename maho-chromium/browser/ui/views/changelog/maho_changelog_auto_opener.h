// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_CHANGELOG_MAHO_CHANGELOG_AUTO_OPENER_H_
#define MAHO_BROWSER_UI_VIEWS_CHANGELOG_MAHO_CHANGELOG_AUTO_OPENER_H_

#include <string>

#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "components/prefs/pref_service.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/ui/webui/maho_welcome/maho_welcome_prefs.h"
#include "maho/browser/updates/maho_update_manager.h"
#include "maho/browser/updates/maho_update_pref_names.h"
#include "maho/components/constants/webui_url_constants.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace maho {

// Pure decision for the post-update auto-open.
// Invariant: the pref is written ONLY when the tab opens; every yield path
// returns write_pref=false. Empty last_shown means fresh install (record the
// version, never open a tab).
struct MahoChangelogAutoOpenDecision {
  bool open_tab = false;
  bool write_pref = false;
};

inline MahoChangelogAutoOpenDecision MahoChangelogEvaluateAutoOpen(
    const std::string& last_shown,
    const std::string& current_version,
    bool regular_profile,
    bool normal_browser_window,
    bool login_gate_active,
    bool update_ready_to_install) {
  if (!regular_profile || !normal_browser_window) {
    return {/*open_tab=*/false, /*write_pref=*/false};
  }
  if (login_gate_active) {
    return {/*open_tab=*/false, /*write_pref=*/false};
  }
  if (last_shown.empty()) {
    return {/*open_tab=*/false, /*write_pref=*/true};
  }
  if (update_ready_to_install) {
    return {/*open_tab=*/false, /*write_pref=*/false};
  }
  if (last_shown != current_version) {
    return {/*open_tab=*/true, /*write_pref=*/true};
  }
  return {/*open_tab=*/false, /*write_pref=*/false};
}

inline void MahoChangelogTryOpenTab() {
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  auto* profile_manager =
      g_browser_process ? g_browser_process->profile_manager() : nullptr;
  if (!local_state || !profile_manager) {
    return;
  }
  for (Profile* profile : profile_manager->GetLoadedProfiles()) {
    if (!profile->IsRegularProfile()) {
      continue;
    }
    Browser* browser = chrome::FindTabbedBrowser(profile, false);
    if (!browser) {
      continue;
    }
    const std::string last_shown =
        local_state->GetString(prefs::kMahoChangelogLastShownVersion);
    const std::string current_version =
        std::string(version_info::GetVersionNumber());
    const MahoChangelogAutoOpenDecision decision = MahoChangelogEvaluateAutoOpen(
        last_shown, current_version, profile->IsRegularProfile(),
        browser->is_type_normal(),
        profile->GetPrefs()->GetBoolean(maho::welcome::kLoginGateActive),
        MahoUpdateManager::GetInstance()->GetState() ==
            UpdateState::kReadyToInstall);
    if (decision.open_tab) {
      NavigateParams params(browser, GURL(kMahoChangelogURL),
                            ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
      params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
      Navigate(&params);
    }
    if (decision.write_pref) {
      local_state->SetString(prefs::kMahoChangelogLastShownVersion,
                             current_version);
      return;
    }
  }
}

// Checks once per attempt for up to ~60s after startup (browsers may be
// restored asynchronously). Stops permanently once the pref is recorded.
inline void MahoChangelogScheduleAutoOpenCheck(int attempt) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](int attempt_left) {
            PrefService* local_state =
                g_browser_process ? g_browser_process->local_state() : nullptr;
            if (!local_state ||
                local_state->FindPreference(prefs::kMahoChangelogLastShownVersion)) {
              MahoChangelogTryOpenTab();
              if (local_state &&
                  !local_state->GetString(prefs::kMahoChangelogLastShownVersion)
                       .empty()) {
                return;
              }
            }
            if (attempt_left > 0) {
              base::SequencedTaskRunner::GetCurrentDefault()
                  ->PostDelayedTask(
                      FROM_HERE,
                      base::BindOnce(
                          [](int remaining) {
                            MahoChangelogScheduleAutoOpenCheck(remaining);
                          },
                          attempt_left - 1),
                      base::Seconds(5));
            }
          },
          attempt),
      attempt == 0 ? base::Seconds(15) : base::Seconds(5));
}

inline void MahoChangelogScheduleAutoOpenCheck() {
  MahoChangelogScheduleAutoOpenCheck(12);
}

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_CHANGELOG_MAHO_CHANGELOG_AUTO_OPENER_H_
