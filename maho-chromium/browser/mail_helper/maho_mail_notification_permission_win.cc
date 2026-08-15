// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_notification_permission.h"

#include <windows.ui.notifications.h>
#include <wrl/client.h>

#include <utility>

#include "base/functional/bind.h"
#include "base/win/scoped_hstring.h"
#include "base/win/scoped_winrt_initializer.h"
#include "chrome/installer/util/install_util.h"
#include "chrome/installer/util/shell_util.h"
#include "maho/browser/mail_helper/maho_mail_service.h"

namespace maho {
namespace {

MailOsNotificationPermission QueryPermission() {
  base::win::ScopedWinrtInitializer winrt;
  Microsoft::WRL::ComPtr<ABI::Windows::UI::Notifications::
                             IToastNotificationManagerStatics>
      manager;
  const HRESULT factory_result = ::RoGetActivationFactory(
      base::win::ScopedHString::Create(
          RuntimeClass_Windows_UI_Notifications_ToastNotificationManager)
          .get(),
      IID_PPV_ARGS(&manager));
  if (FAILED(factory_result) || !manager) {
    return MailOsNotificationPermission::kUnsupported;
  }

  Microsoft::WRL::ComPtr<ABI::Windows::UI::Notifications::IToastNotifier>
      notifier;
  const std::wstring app_user_model_id =
      ShellUtil::GetBrowserModelId(InstallUtil::IsPerUserInstall());
  const base::win::ScopedHString app_id =
      base::win::ScopedHString::Create(app_user_model_id);
  if (FAILED(manager->CreateToastNotifierWithId(app_id.get(), &notifier)) ||
      !notifier) {
    return MailOsNotificationPermission::kUnsupported;
  }

  ABI::Windows::UI::Notifications::NotificationSetting setting;
  if (FAILED(notifier->get_Setting(&setting))) {
    return MailOsNotificationPermission::kUnsupported;
  }
  return setting == ABI::Windows::UI::Notifications::NotificationSetting_Enabled
             ? MailOsNotificationPermission::kGranted
             : MailOsNotificationPermission::kDenied;
}

}  // namespace

void InstallMahoMailNotificationPermissionCallbacks() {
  SetMahoMailNotificationPermissionCallbacks(
      base::BindRepeating(
          [](base::OnceCallback<void(MailOsNotificationPermission)> callback) {
            std::move(callback).Run(QueryPermission());
          }),
      base::BindRepeating(
          [](base::OnceCallback<void(MailOsNotificationPermission)> callback) {
            // Windows desktop toast authorization has no application-triggered
            // prompt. Creating the OS notifier and reading its Setting is the
            // authoritative query for both status and a request attempt.
            std::move(callback).Run(QueryPermission());
          }));
}

}  // namespace maho
