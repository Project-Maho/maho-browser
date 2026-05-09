// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_ACTION_HANDLER_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_ACTION_HANDLER_H_

#include <string>

class Browser;

#include "ui/base/window_open_disposition.h"

namespace maho {

struct CommandSuggestion;

void ExecuteCommandAction(Browser* browser, const std::string& action_id);

// Opens a command suggestion with the given disposition.
// |disposition| controls whether navigation opens in the current tab,
// a new foreground tab, or a new background tab.
void OpenCommandSuggestion(
    Browser* browser,
    const CommandSuggestion& suggestion,
    WindowOpenDisposition disposition = WindowOpenDisposition::CURRENT_TAB);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_ACTION_HANDLER_H_
