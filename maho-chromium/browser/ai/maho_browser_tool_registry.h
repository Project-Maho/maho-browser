// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_BROWSER_TOOL_REGISTRY_H_
#define MAHO_BROWSER_AI_MAHO_BROWSER_TOOL_REGISTRY_H_

#include <string>
#include <vector>

#include "base/values.h"

namespace maho::ai {
struct BrowserActionContract;
}

class MahoBrowserToolRegistry {
 public:
  struct ToolSchema {
    std::string name;
    std::string description;
    base::DictValue input_schema;
    const maho::ai::BrowserActionContract* browser_action_contract = nullptr;
  };

  static const std::vector<ToolSchema>& GetPhase1ToolSchemas();
  static const ToolSchema* FindToolSchema(const std::string& tool_name);
  static base::ListValue SerializePhase1ToolSchemas(bool enabled = true);
  static bool IsBrowserActionTool(const std::string& tool_name);
};

#endif  // MAHO_BROWSER_AI_MAHO_BROWSER_TOOL_REGISTRY_H_
