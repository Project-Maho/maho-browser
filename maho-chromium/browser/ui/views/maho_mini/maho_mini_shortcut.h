// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_MAHO_MINI_MAHO_MINI_SHORTCUT_H_
#define MAHO_BROWSER_UI_VIEWS_MAHO_MINI_MAHO_MINI_SHORTCUT_H_

#include <string>
#include <string_view>

#include "ui/base/accelerators/accelerator.h"

namespace maho {

struct ParsedMiniShortcut {
  ui::Accelerator accelerator;
  std::string error;
};

// Parses a platform accelerator string. Empty strings represent the existing
// built-in default and are resolved by the registration owner.
ParsedMiniShortcut ParseMiniShortcut(std::string_view shortcut);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_MAHO_MINI_MAHO_MINI_SHORTCUT_H_
