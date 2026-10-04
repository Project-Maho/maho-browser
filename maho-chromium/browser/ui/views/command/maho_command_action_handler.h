// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_ACTION_HANDLER_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_ACTION_HANDLER_H_

#include <string>

class Browser;

#include "maho/browser/maho_private_context_policy.h"  // nogncheck
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "ui/base/window_open_disposition.h"

namespace maho {

struct CommandSuggestion;

void ExecuteCommandAction(Browser* browser, const std::string& action_id);

// True only for the frozen 13 action IDs permitted in exact-primary-Incognito.
// Any other (regular-only or forged) ID must be denied. Sourced from
// MahoCommandModel::PrivateActionAllowlist so the Rust/C++ contract stays one.
bool IsPrimaryIncognitoAllowedActionId(const std::string& action_id);

// Sidebar toggle may persist prefs only for regular; primary Incognito uses an
// in-memory container-local toggle and never writes sidebar prefs.
bool ShouldPersistSidebarToggle(MahoPrivateContextClass klass);

bool CanExecuteMailCommandForTesting(bool feature_enabled,
                                     bool helper_available,
                                     bool platform_supported);

// Opens a command suggestion with the given disposition.
// |disposition| controls whether navigation opens in the current tab,
// a new foreground tab, or a new background tab.
void OpenCommandSuggestion(
    Browser* browser,
    const CommandSuggestion& suggestion,
    WindowOpenDisposition disposition = WindowOpenDisposition::CURRENT_TAB);

class MahoCommandActionHandler {
 public:
  // Dispatch-only Ask Maho entry point. Accepts only a regular-profile,
  // regular-context, trimmed non-empty kAskMaho request; emits exactly one
  // Assistant dispatch with context_intent=kNone. Returns true iff a dispatch
  // was emitted; all other cases return false without dispatching.
  static bool OnQuerySubmitted(Browser* browser,
                               const std::string& query,
                               PaletteAction action,
                               MahoPrivateContextClass context_class);
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_ACTION_HANDLER_H_
