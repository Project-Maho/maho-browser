// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_MAHO_IMPORT_SESSION_COMPLETION_H_
#define MAHO_BROWSER_IMPORTER_MAHO_IMPORT_SESSION_COMPLETION_H_

#include <cstdint>
#include <vector>

namespace maho {

enum class ImportTerminalState {
  kRunning,
  kCompleted,
  kCancelled,
  kFailed,
};

enum class ImportProgressAction {
  kReleaseSession,
  kHydrateSpaces,
  kForwardProgress,
};

std::vector<ImportProgressAction> AdvanceImportProgress(
    uint32_t kind,
    ImportTerminalState* terminal_state);

}  // namespace maho

#endif  // MAHO_BROWSER_IMPORTER_MAHO_IMPORT_SESSION_COMPLETION_H_
