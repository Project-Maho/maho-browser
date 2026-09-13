// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_notification_permission.h"

#include "base/functional/bind.h"
#include "maho/browser/mail_helper/maho_mail_service.h"

namespace maho {

// Linux has no per-app OS permission gate; user control stays with Maho's own
// notification preferences. Return kGranted for both status and request.
void InstallMahoMailNotificationPermissionCallbacks() {
  SetMahoMailNotificationPermissionCallbacks(
      base::BindRepeating(
          [](base::OnceCallback<void(MailOsNotificationPermission)> callback) {
            std::move(callback).Run(MailOsNotificationPermission::kGranted);
          }),
      base::BindRepeating(
          [](base::OnceCallback<void(MailOsNotificationPermission)> callback) {
            std::move(callback).Run(MailOsNotificationPermission::kGranted);
          }));
}

}  // namespace maho
