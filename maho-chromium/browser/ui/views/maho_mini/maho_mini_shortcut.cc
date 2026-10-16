// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/maho_mini/maho_mini_shortcut.h"

#include "build/build_config.h"
#include "ui/base/accelerators/command.h"
#include "ui/events/event_constants.h"
#if BUILDFLAG(IS_MAC)
#include "ui/events/keycodes/keyboard_code_conversion_mac.h"
#endif
#include "ui/events/keycodes/keyboard_codes.h"

namespace maho {

ParsedMiniShortcut ParseMiniShortcut(std::string_view shortcut) {
  if (shortcut.empty()) {
    return {{}, "Shortcut is empty; using the built-in default."};
  }

  const ui::Accelerator accelerator =
      ui::Command::StringToAccelerator(shortcut);
  if (accelerator.key_code() == ui::VKEY_UNKNOWN) {
    return {{}, "Shortcut format is invalid."};
  }
  const int modifiers = accelerator.modifiers() &
                        (ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN |
                         ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN);
  if (modifiers == ui::EF_NONE) {
    return {{}, "Shortcut must include at least one modifier."};
  }

  const bool command_or_control = accelerator.IsCmdDown() ||
                                  accelerator.IsCtrlDown();
  if (command_or_control && accelerator.key_code() == ui::VKEY_Q) {
    return {{}, "The quit shortcut is reserved by the platform."};
  }
  if (accelerator.key_code() == ui::VKEY_DELETE &&
      accelerator.IsCtrlDown() && accelerator.IsAltDown()) {
    return {{}, "Ctrl+Alt+Delete is reserved by the operating system."};
  }
#if BUILDFLAG(IS_WIN)
  if (accelerator.IsCtrlDown() && accelerator.IsAltDown() &&
      accelerator.key_code() == ui::VKEY_DELETE)
    return {{}, "Ctrl+Alt+Delete is reserved by Windows."};
#endif

  return {accelerator, {}};
}

}  // namespace maho
