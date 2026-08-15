// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_notification_permission.h"

#include "base/functional/bind.h"
#include "maho/browser/mail_helper/maho_mail_service.h"

namespace maho {

void InstallMahoMailNotificationPermissionCallbacks() {
  SetMahoMailNotificationPermissionCallbacks(
      base::BindRepeating(
          [](base::OnceCallback<void(MailOsNotificationPermission)> callback) {
            std::move(callback).Run(
                MailOsNotificationPermission::kUnsupported);
          }),
      base::BindRepeating(
          [](base::OnceCallback<void(MailOsNotificationPermission)> callback) {
            std::move(callback).Run(
                MailOsNotificationPermission::kUnsupported);
          }));
}

}  // namespace maho
