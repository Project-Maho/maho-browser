// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/maho_core_watchdog_policy.h"

namespace maho {

std::optional<base::TimeDelta> CoreOperationDeadline(
    CoreOperationClass operation) {
  switch (operation) {
    case CoreOperationClass::kInteractive:
      return kInteractiveCoreDeadline;
    case CoreOperationClass::kStartup:
      return kStartupCoreDeadline;
    case CoreOperationClass::kLongIo:
      return std::nullopt;
  }
  return std::nullopt;
}

CoreWatchdogPolicy::CoreWatchdogPolicy() = default;
CoreWatchdogPolicy::~CoreWatchdogPolicy() = default;

uint64_t CoreWatchdogPolicy::OnTaskStarted(CoreOperationClass operation,
                                           base::TimeTicks now) {
  const uint64_t id = next_id_++;
  tasks_.emplace(id, Task{operation, now});
  return id;
}

void CoreWatchdogPolicy::OnTaskFinished(uint64_t task_id) {
  tasks_.erase(task_id);
}

std::optional<CoreHealth> CoreWatchdogPolicy::Evaluate(base::TimeTicks now) {
  bool overdue = false;
  for (const auto& [id, task] : tasks_) {
    const std::optional<base::TimeDelta> deadline =
        CoreOperationDeadline(task.operation);
    if (deadline && now - task.started > *deadline) {
      overdue = true;
      break;
    }
  }
  const CoreHealth next = overdue ? CoreHealth::kDegraded : CoreHealth::kHealthy;
  if (next == health_) {
    return std::nullopt;
  }
  health_ = next;
  return next;
}

}  // namespace maho
