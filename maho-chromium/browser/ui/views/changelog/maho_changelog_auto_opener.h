// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_CHANGELOG_MAHO_CHANGELOG_AUTO_OPENER_H_
#define MAHO_BROWSER_UI_VIEWS_CHANGELOG_MAHO_CHANGELOG_AUTO_OPENER_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/updates/maho_product_version.h"
#include "maho/browser/updates/maho_update_manager.h"
#include "maho/browser/updates/maho_update_pref_names.h"
#include "maho/browser/ui/webui/maho_welcome/maho_welcome_prefs.h"
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

// Applies the decision for one browser window. Returns true when the pref was
// recorded, i.e. the flow reached a terminal state for this launch.
inline bool MahoChangelogEvaluateAndOpenForBrowser(
    BrowserWindowInterface* browser) {
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (!local_state || !browser ||
      browser->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
    return false;
  }
  Profile* profile = browser->GetProfile();
  if (!profile || !profile->IsRegularProfile()) {
    return false;
  }
  const std::string last_shown =
      local_state->GetString(prefs::kMahoChangelogLastShownVersion);
  // Keyed on the Maho product version (e.g. 2026.9.29), the version whose
  // release notes this surface shows. Chromium's engine version changes on
  // engine rolls that ship no Maho release notes, which re-opened the changelog
  // for changes the user never received.
  const std::string current_version = std::string(updates::kMahoProductVersion);
  const MahoChangelogAutoOpenDecision decision = MahoChangelogEvaluateAutoOpen(
      last_shown, current_version, profile->IsRegularProfile(),
      browser->GetType() == BrowserWindowInterface::TYPE_NORMAL,
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
  }
  return decision.write_pref;
}

// Opens Maho's own changelog surface in the profile's active window.
//
// This is the handler the macOS updater runs when the user asks Sparkle for
// the full release notes / version history. Sparkle's default is to hand the
// appcast's releaseNotesLink to the system browser, but that URL is a GitHub
// release asset served with Content-Disposition: attachment, so the user got a
// saved .html file instead of readable notes. Opening the bundled surface keeps
// the notes in-app, offline-capable, and on-brand.
//
// The updater callback holds a WeakPtr<Profile> and calls this helper only
// while the profile is alive.
inline void MahoChangelogOpenForProfile(Profile* profile) {
  if (!profile) {
    return;
  }
  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(profile);
  BrowserWindowInterface* window =
      collection ? collection->GetLastActiveBrowser() : nullptr;
  if (!window || window->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
    return;
  }
  NavigateParams params(window, GURL(kMahoChangelogURL),
                        ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

// Event-driven post-update auto-open: OnBrowserCreated is the single trigger.
// The observation detaches from the profile collection as soon as the pref is
// recorded (terminal state for this launch); yield paths keep observing so a
// later window (e.g. after the login gate completes) is still evaluated.
class MahoChangelogAutoOpener : public BrowserCollectionObserver {
 public:
  // Registers the process-lifetime opener for one profile. Safe to call from
  // PostProfileInit: browsers do not exist yet, so nothing is evaluated here
  // (plan Must-NOT: never Navigate directly from PostProfileInit).
  static void RegisterForProfile(Profile* profile) {
    auto* opener = new MahoChangelogAutoOpener(profile);
    opener->observation_.Observe(ProfileBrowserCollection::GetForProfile(profile));
  }

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* window) override;

  ~MahoChangelogAutoOpener() override;

 private:
  explicit MahoChangelogAutoOpener(Profile* profile);

  base::ScopedObservation<ProfileBrowserCollection, BrowserCollectionObserver>
      observation_;
  base::WeakPtr<Profile> profile_;
  bool handled_ = false;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_CHANGELOG_MAHO_CHANGELOG_AUTO_OPENER_H_
