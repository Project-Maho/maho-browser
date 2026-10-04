// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_RUNTIME_EVENT_PERSISTENCE_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_RUNTIME_EVENT_PERSISTENCE_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "base/values.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"

namespace maho::ai {

inline constexpr size_t kReplayBytes = 4 * 1024 * 1024;
inline constexpr size_t kReplayRecords = 2048;

struct RuntimeReplayBudget {
  RuntimeReplayBudget();
  ~RuntimeReplayBudget();
  void Reset();

  size_t bytes = 0;
  std::deque<size_t> event_bytes;
  uint64_t omitted_turns = 0;
  uint64_t omitted_through_sequence = 0;
  bool exhausted = false;
};

enum class ReplayAdmission { kAccepted, kIgnored, kExhausted };

ReplayAdmission AppendBoundedReplayEvent(
    std::vector<maho_ai::mojom::RuntimeEventPtr>& events,
    RuntimeReplayBudget& budget,
    maho_ai::mojom::RuntimeEventPtr& event);
void NormalizeReplayWindow(
    std::vector<maho_ai::mojom::RuntimeEventPtr>& events,
    RuntimeReplayBudget& budget);

base::DictValue SerializeRuntimeEventForPersistence(
    const maho_ai::mojom::RuntimeEvent& event);

}  // namespace maho::ai

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_RUNTIME_EVENT_PERSISTENCE_H_
