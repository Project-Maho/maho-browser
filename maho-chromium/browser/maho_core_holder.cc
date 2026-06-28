// Copyright 2026 Maho Browser. All rights reserved.

#include <atomic>

struct MahoCore;

namespace maho {

namespace {
std::atomic<::MahoCore*> g_core{nullptr};
}

::MahoCore* GetCore() {
  return g_core.load(std::memory_order_acquire);
}

void SetCore(::MahoCore* core) {
  g_core.store(core, std::memory_order_release);
}

}
