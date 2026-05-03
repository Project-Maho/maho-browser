// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_core_holder.h"

#include <atomic>

namespace maho {

namespace {
// Thread safety: g_core is set on the UI thread during PostEarlyInitialization
// (before any network requests) and cleared during PostMainMessageLoopRun
// (after all network activity stops). The pointer is stable for the entire
// window where WillStartRequest can be called. std::atomic provides the
// formal happens-before guarantee for cross-thread reads.
std::atomic<MahoCore*> g_core{nullptr};
}  // namespace

MahoCore* GetCore() {
  return g_core.load(std::memory_order_acquire);
}

void SetCore(MahoCore* core) {
  g_core.store(core, std::memory_order_release);
}

}  // namespace maho
