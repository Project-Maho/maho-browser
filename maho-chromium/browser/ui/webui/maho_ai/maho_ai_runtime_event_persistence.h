// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_RUNTIME_EVENT_PERSISTENCE_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_RUNTIME_EVENT_PERSISTENCE_H_

#include "base/values.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"

namespace maho::ai {

base::DictValue SerializeRuntimeEventForPersistence(
    const maho_ai::mojom::RuntimeEvent& event);

}  // namespace maho::ai

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_RUNTIME_EVENT_PERSISTENCE_H_
