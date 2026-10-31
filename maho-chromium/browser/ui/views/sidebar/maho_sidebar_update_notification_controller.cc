// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_update_notification_controller.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/updates/maho_product_version.h"
#include "maho/browser/updates/maho_update_pref_names.h"

namespace maho {

MahoSidebarUpdateNotificationController::
    MahoSidebarUpdateNotificationController(
        Browser* browser,
        base::RepeatingClosure on_state_changed)
    : browser_(browser), on_state_changed_(std::move(on_state_changed)) {
  CHECK(on_state_changed_);
  MahoUpdateManager::GetInstance()->AddObserver(this);
}

MahoSidebarUpdateNotificationController::
    ~MahoSidebarUpdateNotificationController() {
  MahoUpdateManager::GetInstance()->RemoveObserver(this);
}

MahoSidebarUpdateNotificationModel
MahoSidebarUpdateNotificationController::BuildModel() {
  MahoSidebarUpdateNotificationModel model;
  auto* manager = MahoUpdateManager::GetInstance();

  const bool simulate_only_show =
      manager->is_simulating_for_testing() &&
      manager->GetState() == UpdateState::kReadyToInstall;

  bool should_show = false;
  if (manager->GetState() == UpdateState::kReadyToInstall &&
      !dismissed_in_session_) {
    PrefService* local_state =
        g_browser_process ? g_browser_process->local_state() : nullptr;
    const std::string current = CurrentVersion();
    const std::string last_seen =
        local_state ? local_state->GetString(prefs::kMahoUpdateLastSeenVersion)
                    : std::string();
    should_show = simulate_only_show || (current != last_seen);
  } else if (simulate_only_show) {
    should_show = true;
  }

  if (!should_show) {
    return model;
  }

  model.visible = true;
  model.heading = u"Maho is ready to update";
  model.accessible_heading =
      u"A new version of Maho is downloaded and ready to install.";
  model.on_dismiss = base::BindRepeating(
      &MahoSidebarUpdateNotificationController::Dismiss,
      base::Unretained(this));

  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  MahoSidebarUpdateCheckboxModel auto_install;
  auto_install.label = u"Automatically install updates";
  auto_install.accessible_label =
      u"Install future updates automatically without prompting";
  auto_install.checked = local_state
                             ? local_state->GetBoolean(
                                   prefs::kMahoUpdateAutoInstall)
                             : false;
  auto_install.on_toggle = base::BindRepeating(
      &MahoSidebarUpdateNotificationController::OnAutoInstallToggled,
      base::Unretained(this));
  model.checkbox = std::move(auto_install);

  MahoSidebarUpdateActionModel restart;
  restart.label = u"Restart and update";
  restart.accessible_label = u"Restart Maho to apply the update";
  restart.special = true;
  restart.on_activate = base::BindRepeating(
      &MahoSidebarUpdateNotificationController::OnRestartActivated,
      base::Unretained(this));
  model.actions.push_back(std::move(restart));

  return model;
}

void MahoSidebarUpdateNotificationController::OnAutoInstallToggled(
    bool checked) {
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (local_state) {
    local_state->SetBoolean(prefs::kMahoUpdateAutoInstall, checked);
  }
}

void MahoSidebarUpdateNotificationController::Dismiss() {
  dismissed_in_session_ = true;
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (local_state) {
    local_state->SetString(prefs::kMahoUpdateLastSeenVersion, CurrentVersion());
  }
  on_state_changed_.Run();
}

void MahoSidebarUpdateNotificationController::OnRestartActivated() {
  Dismiss();
  MahoUpdateManager::GetInstance()->ApplyUpdateAndRestart();
}

void MahoSidebarUpdateNotificationController::OnUpdateStateChanged(
    UpdateState /*state*/) {
  on_state_changed_.Run();
}

void MahoSidebarUpdateNotificationController::OnUpdateProgress(
    double /*percent*/) {}

std::string MahoSidebarUpdateNotificationController::CurrentVersion() const {
  // The "already seen this upgrade" pref must be keyed on the Maho product
  // version: that is the version users are notified about, and the one that
  // changes on a Maho release. Chromium's engine version changes independently
  // of Maho releases, which re-armed the notification spuriously.
  return std::string(updates::kMahoProductVersion);
}

}  // namespace maho
