// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAHO_CORE_HOLDER_H_
#define MAHO_BROWSER_MAHO_CORE_HOLDER_H_

struct MahoCore;

namespace maho {

// Returns the global MahoCore instance, or nullptr if not yet initialized.
MahoCore* GetCore();

// Sets the global MahoCore instance. Called during startup/shutdown.
void SetCore(MahoCore* core);

}  // namespace maho

#endif  // MAHO_BROWSER_MAHO_CORE_HOLDER_H_
