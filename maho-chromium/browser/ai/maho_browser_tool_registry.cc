// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_browser_tool_registry.h"

#include <utility>

#include "base/no_destructor.h"

namespace {

base::DictValue BuildNoArgSchema() {
  base::DictValue schema;
  schema.Set("type", "object");
  schema.Set("additionalProperties", false);
  schema.Set("properties", base::DictValue());
  schema.Set("required", base::ListValue());
  return schema;
}

base::DictValue BuildSearchSchema() {
  base::DictValue schema;
  schema.Set("type", "object");
  schema.Set("additionalProperties", false);

  base::DictValue properties;
  base::DictValue query_schema;
  query_schema.Set("type", "string");
  query_schema.Set("description", "Text to search for in the active page.");
  properties.Set("query", std::move(query_schema));

  base::DictValue max_results_schema;
  max_results_schema.Set("type", "integer");
  max_results_schema.Set("minimum", 1);
  max_results_schema.Set("maximum", 20);
  max_results_schema.Set(
      "description",
      "Maximum number of excerpts to return from the active page.");
  properties.Set("max_results", std::move(max_results_schema));

  schema.Set("properties", std::move(properties));
  base::ListValue required;
  required.Append("query");
  schema.Set("required", std::move(required));
  return schema;
}

std::vector<MahoBrowserToolRegistry::ToolSchema> BuildPhase1ToolSchemas() {
  std::vector<MahoBrowserToolRegistry::ToolSchema> tools;
  tools.push_back({
      .name = "read_current_page",
      .description =
          "Read the currently active page and return browser-ready page context.",
      .input_schema = BuildNoArgSchema(),
  });
  tools.push_back({
      .name = "get_selected_text",
      .description =
          "Return the current text selection from the active browser tab.",
      .input_schema = BuildNoArgSchema(),
  });
  tools.push_back({
      .name = "get_active_tab",
      .description =
          "Return metadata for the active browser tab in the current window.",
      .input_schema = BuildNoArgSchema(),
  });
  tools.push_back({
      .name = "search_in_page",
      .description =
          "Search the active page content and return matching excerpts.",
      .input_schema = BuildSearchSchema(),
  });
  tools.push_back({
      .name = "extract_structured_page_context",
      .description =
          "Extract structured context from the active page for browser chat orchestration.",
      .input_schema = BuildNoArgSchema(),
  });
  return tools;
}

}  // namespace

// static
const std::vector<MahoBrowserToolRegistry::ToolSchema>&
MahoBrowserToolRegistry::GetPhase1ToolSchemas() {
  static const base::NoDestructor<std::vector<ToolSchema>> kTools(BuildPhase1ToolSchemas());
  return *kTools;
}

// static
const MahoBrowserToolRegistry::ToolSchema* MahoBrowserToolRegistry::FindToolSchema(
    const std::string& tool_name) {
  for (const ToolSchema& schema : GetPhase1ToolSchemas()) {
    if (schema.name == tool_name) {
      return &schema;
    }
  }
  return nullptr;
}

// static
base::ListValue MahoBrowserToolRegistry::SerializePhase1ToolSchemas(
    bool enabled) {
  if (!enabled) {
    return base::ListValue();
  }

  base::ListValue tools;
  for (const ToolSchema& schema : GetPhase1ToolSchemas()) {
    base::DictValue tool;
    tool.Set("name", schema.name);
    tool.Set("description", schema.description);
    tool.Set("input_schema", schema.input_schema.Clone());
    tools.Append(std::move(tool));
  }
  return tools;
}
