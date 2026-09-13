// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_notification_permission.h"

#import <UserNotifications/UserNotifications.h>

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "maho/browser/mail_helper/maho_mail_service.h"

namespace maho {
namespace {

using PermissionCallback =
    base::OnceCallback<void(MailOsNotificationPermission)>;

struct PermissionState {
  base::Lock lock;
  bool settings_query_pending = false;
  bool request_pending = false;
  std::vector<PermissionCallback> pending_status_callbacks;
  std::vector<PermissionCallback> pending_request_callbacks;
};

PermissionState& GetPermissionState() {
  static base::NoDestructor<PermissionState> state;
  return *state;
}

MailOsNotificationPermission MapPermission(UNAuthorizationStatus status) {
  switch (status) {
    case UNAuthorizationStatusNotDetermined:
      return MailOsNotificationPermission::kNotDetermined;
    case UNAuthorizationStatusDenied:
      return MailOsNotificationPermission::kDenied;
    case UNAuthorizationStatusAuthorized:
    case UNAuthorizationStatusProvisional:
      return MailOsNotificationPermission::kGranted;
    default:
      return MailOsNotificationPermission::kUnsupported;
  }
}

void RefreshSettings(PermissionCallback callback) {
  PermissionState* state = &GetPermissionState();
  PermissionCallback reply =
      base::BindPostTaskToCurrentDefault(std::move(callback));
  {
    base::AutoLock lock(state->lock);
    state->pending_status_callbacks.push_back(std::move(reply));
    if (state->settings_query_pending) {
      return;
    }
    state->settings_query_pending = true;
  }

  [[UNUserNotificationCenter currentNotificationCenter]
      getNotificationSettingsWithCompletionHandler:^(
          UNNotificationSettings* settings) {
        MailOsNotificationPermission permission =
            MapPermission(settings.authorizationStatus);
        std::vector<PermissionCallback> callbacks;
        {
          base::AutoLock lock(state->lock);
          state->settings_query_pending = false;
          if (permission == MailOsNotificationPermission::kNotDetermined &&
              state->request_pending) {
            permission = MailOsNotificationPermission::kPromptPending;
          }
          callbacks.swap(state->pending_status_callbacks);
        }
        for (auto& pending_callback : callbacks) {
          std::move(pending_callback).Run(permission);
        }
      }];
}

void RequestPermission(PermissionCallback callback) {
  scoped_refptr<base::SequencedTaskRunner> runner =
      base::SequencedTaskRunner::GetCurrentDefault();
  PermissionState* state = &GetPermissionState();
  PermissionCallback reply =
      base::BindPostTask(runner, std::move(callback));
  {
    base::AutoLock lock(state->lock);
    state->pending_request_callbacks.push_back(std::move(reply));
    if (state->request_pending) {
      return;
    }
    state->request_pending = true;
  }

  const UNAuthorizationOptions options = UNAuthorizationOptionAlert |
                                         UNAuthorizationOptionSound |
                                         UNAuthorizationOptionBadge;
  [[UNUserNotificationCenter currentNotificationCenter]
      requestAuthorizationWithOptions:options
                    completionHandler:^(BOOL granted, NSError* error) {
                      runner->PostTask(
                          FROM_HERE,
                          base::BindOnce([]() {
                            PermissionState* state = &GetPermissionState();
                            std::vector<PermissionCallback> callbacks;
                            {
                              base::AutoLock lock(state->lock);
                              state->request_pending = false;
                              callbacks.swap(state->pending_request_callbacks);
                            }
                            for (auto& pending_callback : callbacks) {
                              RefreshSettings(std::move(pending_callback));
                            }
                          }));
                    }];
}

}  // namespace

void InstallMahoMailNotificationPermissionCallbacks() {
  SetMahoMailNotificationPermissionCallbacks(
      base::BindRepeating(&RefreshSettings),
      base::BindRepeating(&RequestPermission));
}

}  // namespace maho
