// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/notifications/maho_notification_overlay.h"

#include "base/logging.h"

MahoNotificationOverlay::MahoNotificationOverlay() = default;
MahoNotificationOverlay::~MahoNotificationOverlay() = default;

void MahoNotificationOverlay::Show(const std::string& title,
                                   const std::string& body) {
  LOG(INFO) << "MahoNotificationOverlay::Show title=" << title
            << " body=" << body;
}
