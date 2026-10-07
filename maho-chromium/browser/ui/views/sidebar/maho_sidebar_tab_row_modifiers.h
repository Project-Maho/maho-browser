// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TAB_ROW_MODIFIERS_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TAB_ROW_MODIFIERS_H_

#include "ui/events/event_constants.h"

namespace maho {

// macOS excludes Control: a Ctrl+left on a view is the system secondary click
// (NativeWidgetMacNSWindow only rewrites it when the hit test lands on no view),
// so honoring it as a selection modifier swallowed the tab context menu.
// `is_mac_platform` is a parameter so the policy is testable on every platform.
inline bool IsTabMultiSelectModifierForPlatform(int event_flags,
                                                bool is_mac_platform) {
  if (is_mac_platform) {
    return (event_flags & ui::EF_COMMAND_DOWN) != 0;
  }
  return (event_flags & (ui::EF_COMMAND_DOWN | ui::EF_CONTROL_DOWN)) != 0;
}

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TAB_ROW_MODIFIERS_H_
