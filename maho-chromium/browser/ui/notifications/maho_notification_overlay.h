// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_NOTIFICATIONS_MAHO_NOTIFICATION_OVERLAY_H_
#define MAHO_BROWSER_UI_NOTIFICATIONS_MAHO_NOTIFICATION_OVERLAY_H_

#include <string>

class MahoNotificationOverlay {
 public:
  MahoNotificationOverlay();
  ~MahoNotificationOverlay();

  void Show(const std::string& title, const std::string& body);
};

#endif  // MAHO_BROWSER_UI_NOTIFICATIONS_MAHO_NOTIFICATION_OVERLAY_H_
