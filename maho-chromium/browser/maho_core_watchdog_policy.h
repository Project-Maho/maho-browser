// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAHO_CORE_WATCHDOG_POLICY_H_
#define MAHO_BROWSER_MAHO_CORE_WATCHDOG_POLICY_H_

#include <cstdint>
#include <map>
#include <optional>

#include "base/time/time.h"

namespace maho {

// What a core task is doing. The deadline depends on the operation: a quick
// interactive call that takes 30 s is stuck, while startup may legitimately
// take minutes and long I/O (imports, backups, model downloads) has no fixed
// bound, so it is never reported as hung.
enum class CoreOperationClass {
  kInteractive,
  kStartup,
  kLongIo,
};

inline constexpr base::TimeDelta kInteractiveCoreDeadline = base::Seconds(30);
inline constexpr base::TimeDelta kStartupCoreDeadline = base::Seconds(120);

// Returns the deadline for |operation|, or nullopt when it has none.
std::optional<base::TimeDelta> CoreOperationDeadline(
    CoreOperationClass operation);

enum class CoreHealth {
  kHealthy,
  kDegraded,
};

// Tracks in-flight core tasks and decides when the core is degraded. It only
// reports state: it never cancels, kills or restarts anything. Time comes in
// as explicit arguments so the policy is deterministic and testable without
// sleeping.
class CoreWatchdogPolicy {
 public:
  CoreWatchdogPolicy();
  CoreWatchdogPolicy(const CoreWatchdogPolicy&) = delete;
  CoreWatchdogPolicy& operator=(const CoreWatchdogPolicy&) = delete;
  ~CoreWatchdogPolicy();

  uint64_t OnTaskStarted(CoreOperationClass operation, base::TimeTicks now);
  void OnTaskFinished(uint64_t task_id);

  // Re-evaluates health at |now|. Returns the new health only when it changed,
  // so callers emit one event per transition.
  std::optional<CoreHealth> Evaluate(base::TimeTicks now);

  CoreHealth health() const { return health_; }
  size_t in_flight() const { return tasks_.size(); }

 private:
  struct Task {
    CoreOperationClass operation;
    base::TimeTicks started;
  };

  uint64_t next_id_ = 1;
  std::map<uint64_t, Task> tasks_;
  CoreHealth health_ = CoreHealth::kHealthy;
};

}  // namespace maho

#endif  // MAHO_BROWSER_MAHO_CORE_WATCHDOG_POLICY_H_
