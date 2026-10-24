// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/importer/maho_import_session_completion.h"

namespace maho {

std::vector<ImportProgressAction> AdvanceImportProgress(
    uint32_t kind,
    uint32_t import_type,
    ImportTerminalState* terminal_state) {
  // A typed error (kind 4 with a type bit) reports one failed import type;
  // the orchestrator keeps going and still sends AllComplete, so it is not
  // terminal. Only AllComplete and untyped (whole-import) errors end it.
  const bool is_terminal = kind == 3 || (kind == 4 && import_type == 0);
  if (!is_terminal) {
    return *terminal_state == ImportTerminalState::kRunning
               ? std::vector{ImportProgressAction::kForwardProgress}
               : std::vector<ImportProgressAction>{};
  }

  if (*terminal_state == ImportTerminalState::kCancelled) {
    return {ImportProgressAction::kReleaseSession};
  }
  if (*terminal_state != ImportTerminalState::kRunning) {
    return {};
  }
  if (kind == 3) {
    *terminal_state = ImportTerminalState::kCompleted;
    return {ImportProgressAction::kReleaseSession,
            ImportProgressAction::kHydrateSpaces,
            ImportProgressAction::kForwardProgress};
  }

  *terminal_state = ImportTerminalState::kFailed;
  return {ImportProgressAction::kReleaseSession,
          ImportProgressAction::kForwardProgress};
}

}  // namespace maho
