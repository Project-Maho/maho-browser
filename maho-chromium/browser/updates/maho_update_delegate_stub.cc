// Copyright 2026 Maho Browser. All rights reserved.

#include <memory>

#include "maho/browser/updates/maho_update_manager.h"
#include "maho/browser/updates/platform_updater_delegate.h"

namespace maho {

// Minimal stub for macOS until the Sparkle-backed delegate is reintegrated.
std::unique_ptr<PlatformUpdaterDelegate> CreatePlatformUpdaterDelegate() {
  return nullptr;
}

}  // namespace maho
