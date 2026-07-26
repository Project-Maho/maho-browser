// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_overlay_vibrancy.h"

#include <dwmapi.h>

#include "base/win/windows_version.h"
#include "ui/views/widget/widget.h"
#include "ui/views/win/hwnd_util.h"

namespace maho {

void ApplyVibrancyToCommandOverlay(views::Widget* widget) {
  if (base::win::GetVersion() < base::win::Version::WIN11_22H2)
    return;

  HWND hwnd = views::HWNDForNativeWindow(widget->GetNativeWindow());
  if (!hwnd)
    return;

  static constexpr DWORD kBackdropTransientAcrylic = 3;
  DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE,
                        &kBackdropTransientAcrylic,
                        sizeof(kBackdropTransientAcrylic));
}

}  // namespace maho
