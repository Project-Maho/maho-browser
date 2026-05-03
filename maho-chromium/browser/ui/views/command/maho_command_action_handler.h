// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_ACTION_HANDLER_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_ACTION_HANDLER_H_

#include <string>

class Browser;

namespace maho {

struct CommandSuggestion;

void ExecuteCommandAction(Browser* browser, const std::string& action_id);

void OpenCommandSuggestion(Browser* browser,
                           const CommandSuggestion& suggestion);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_ACTION_HANDLER_H_
