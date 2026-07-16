// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UPDATES_CRASH_LOOP_SENTINEL_H_
#define MAHO_BROWSER_UPDATES_CRASH_LOOP_SENTINEL_H_

#include <string>

#include "base/component_export.h"

class PrefService;

namespace maho {
namespace updates {

class COMPONENT_EXPORT(MAHO_UPDATES) CrashLoopSentinel {
 public:
  // Checks if the current version has crashed 3 or more times consecutively on startup.
  // Also increments the crash count and updates the sentinel in local_state.
  // Returns true if a crash loop is detected (i.e. count >= 3).
  static bool CheckAndMarkStart(PrefService* local_state, const std::string& current_version);

  // Marks the startup as stable (clears the crash count sentinel).
  static void MarkStable(PrefService* local_state, const std::string& current_version);
};

}  // namespace updates
}  // namespace maho

#endif  // MAHO_BROWSER_UPDATES_CRASH_LOOP_SENTINEL_H_
