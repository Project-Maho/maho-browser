// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_UPDATE_NOTIFICATION_CONTROLLER_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_UPDATE_NOTIFICATION_CONTROLLER_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_update_notification_model.h"
#include "maho/browser/updates/maho_update_manager.h"

class Browser;

namespace maho {

// Owns visibility decision logic for the Zen-style update notification.
// Observes MahoUpdateManager and decides whether the notification should
// appear based on:
//   * Manager state == kReadyToInstall, AND
//   * Current version != last seen version (one-shot per upgrade), AND
//   * Not dismissed in this session.
// Simulate mode bypasses the version-comparison gate so visual QA does not
// require a fresh local-state.
class MahoSidebarUpdateNotificationController : public MahoUpdateObserver {
 public:
  MahoSidebarUpdateNotificationController(
      Browser* browser,
      base::RepeatingClosure on_state_changed);
  MahoSidebarUpdateNotificationController(
      const MahoSidebarUpdateNotificationController&) = delete;
  MahoSidebarUpdateNotificationController& operator=(
      const MahoSidebarUpdateNotificationController&) = delete;
  ~MahoSidebarUpdateNotificationController() override;

  MahoSidebarUpdateNotificationModel BuildModel();
  void Dismiss();

  bool dismissed_in_session_for_testing() const {
    return dismissed_in_session_;
  }
  void clear_session_dismissed_for_testing() { dismissed_in_session_ = false; }

  // MahoUpdateObserver:
  void OnUpdateStateChanged(UpdateState state) override;
  void OnUpdateProgress(double percent) override;

 private:
  void OnRestartActivated();
  void OnAutoInstallToggled(bool checked);
  std::string CurrentVersion() const;

  raw_ptr<Browser> browser_;
  base::RepeatingClosure on_state_changed_;
  bool dismissed_in_session_ = false;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_UPDATE_NOTIFICATION_CONTROLLER_H_
