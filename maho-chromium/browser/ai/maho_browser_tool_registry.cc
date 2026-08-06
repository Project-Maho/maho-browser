// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_browser_tool_registry.h"

#include <utility>

#include "base/no_destructor.h"
#include "maho/browser/ai/maho_browser_action_contract.h"

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

base::DictValue BuildStringSchema(std::string description) {
  base::DictValue schema;
  schema.Set("type", "string");
  schema.Set("description", std::move(description));
  return schema;
}

base::DictValue BuildIntegerSchema(std::string description) {
  base::DictValue schema;
  schema.Set("type", "integer");
  schema.Set("description", std::move(description));
  return schema;
}

base::DictValue BuildBooleanSchema(std::string description) {
  base::DictValue schema;
  schema.Set("type", "boolean");
  schema.Set("description", std::move(description));
  return schema;
}

base::DictValue BuildStringListSchema(std::string description) {
  base::DictValue items;
  items.Set("type", "string");

  base::DictValue schema;
  schema.Set("type", "array");
  schema.Set("description", std::move(description));
  schema.Set("items", std::move(items));
  return schema;
}

base::DictValue BuildObjectSchema(base::DictValue properties,
                                  base::ListValue required) {
  base::DictValue schema;
  schema.Set("type", "object");
  schema.Set("additionalProperties", false);
  schema.Set("properties", std::move(properties));
  schema.Set("required", std::move(required));
  return schema;
}

base::DictValue BuildMailAccountSchema() {
  base::DictValue properties;
  properties.Set("account_id", BuildStringSchema("Mail account id."));
  base::ListValue required;
  required.Append("account_id");
  return BuildObjectSchema(std::move(properties), std::move(required));
}

base::DictValue BuildMailListEmailsSchema() {
  base::DictValue properties;
  properties.Set("account_id", BuildStringSchema("Mail account id."));
  properties.Set("folder_id", BuildStringSchema("Mail folder id."));
  properties.Set("limit", BuildIntegerSchema(
                              "Maximum emails to return; default 50."));
  properties.Set("offset",
                 BuildIntegerSchema("Pagination offset; default 0."));
  base::ListValue required;
  required.Append("account_id");
  required.Append("folder_id");
  return BuildObjectSchema(std::move(properties), std::move(required));
}

base::DictValue BuildMailIdSchema(std::string key, std::string description) {
  base::DictValue properties;
  properties.Set(key, BuildStringSchema(std::move(description)));
  base::ListValue required;
  required.Append(key);
  return BuildObjectSchema(std::move(properties), std::move(required));
}

base::DictValue BuildMailSearchSchema() {
  base::DictValue properties;
  properties.Set("query_json", BuildStringSchema(
      "JSON search query accepted by the Mail search broker."));
  base::ListValue required;
  required.Append("query_json");
  return BuildObjectSchema(std::move(properties), std::move(required));
}

base::DictValue BuildMailThreadSchema() {
  base::DictValue properties;
  properties.Set("account_id", BuildStringSchema("Mail account id."));
  properties.Set("message_id",
                 BuildStringSchema("RFC message id for the thread."));
  base::ListValue required;
  required.Append("account_id");
  required.Append("message_id");
  return BuildObjectSchema(std::move(properties), std::move(required));
}

void AddOptionalTabId(base::DictValue& properties) {
  properties.Set("tab_id", BuildIntegerSchema(
                               "Browser tab id. Omit or pass 0 for the active tab."));
}

base::DictValue BuildBrowserActionInputSchema(std::string_view tool_name) {
  base::DictValue properties;
  base::ListValue required;
  AddOptionalTabId(properties);

  if (tool_name == "browser_navigate") {
    properties.Set("url", BuildStringSchema("Destination URL."));
    properties.Set("page_derived_justification",
                   BuildBooleanSchema(
                       "True when the destination came from page content."));
    required.Append("url");
  } else if (tool_name == "page_query_selector") {
    properties.Set("selector", BuildStringSchema("CSS selector to query."));
    required.Append("selector");
  } else if (tool_name == "page_get_text") {
    properties.Set("ref_id", BuildStringSchema("DOM ref id from page_query_selector."));
    required.Append("ref_id");
  } else if (tool_name == "page_get_attribute") {
    properties.Set("ref_id", BuildStringSchema("DOM ref id from page_query_selector."));
    properties.Set("attribute", BuildStringSchema("Attribute name to read."));
    required.Append("ref_id");
    required.Append("attribute");
  } else if (tool_name == "page_wait_for_selector") {
    properties.Set("selector", BuildStringSchema("CSS selector to wait for."));
    properties.Set("timeout_ms", BuildIntegerSchema("Timeout in milliseconds."));
    required.Append("selector");
  } else if (tool_name == "browser_wait_for_navigation") {
    properties.Set("since_timestamp_ms",
                   BuildIntegerSchema("Only navigation events after this Unix timestamp."));
    properties.Set("timeout_ms", BuildIntegerSchema("Timeout in milliseconds."));
  } else if (tool_name == "browser_click" || tool_name == "browser_hover") {
    properties.Set("ref", BuildIntegerSchema(
                              "Accessibility ref from browser_accessibility_snapshot."));
    required.Append("ref");
  } else if (tool_name == "browser_type") {
    properties.Set("ref", BuildIntegerSchema(
                              "Accessibility ref from browser_accessibility_snapshot."));
    properties.Set("text", BuildStringSchema("Text to type. Never use for credentials."));
    required.Append("ref");
    required.Append("text");
  } else if (tool_name == "browser_select") {
    properties.Set("ref", BuildIntegerSchema(
                              "Accessibility ref from browser_accessibility_snapshot."));
    properties.Set("value", BuildStringSchema("Option value to select."));
    required.Append("ref");
    required.Append("value");
  } else if (tool_name == "browser_scroll") {
    properties.Set("direction",
                   BuildStringSchema("Scroll direction: up, down, left, or right."));
    properties.Set("pixels", BuildIntegerSchema("Pixels to scroll; default 300."));
    properties.Set("ref", BuildIntegerSchema(
                              "Optional accessibility ref to scroll an element."));
    required.Append("direction");
  } else if (tool_name == "browser_key_press") {
    properties.Set("key", BuildStringSchema("Keyboard key, e.g. Enter or ArrowDown."));
    properties.Set("modifiers",
                   BuildStringListSchema("Optional modifiers: shift, ctrl, alt, meta."));
    required.Append("key");
  }

  return BuildObjectSchema(std::move(properties), std::move(required));
}

std::string BrowserActionDescription(
    const maho::ai::BrowserActionContract& contract) {
  const std::string name(contract.tool_name);
  if (name == "browser_navigate") {
    return "Navigate the active tab to a URL.";
  }
  if (name == "browser_accessibility_snapshot") {
    return "Return an accessibility tree snapshot with @ref labels.";
  }
  if (name == "page_query_selector") {
    return "Query the active page for a CSS selector and return a DOM ref.";
  }
  if (name == "page_get_text") {
    return "Return text for a DOM ref from page_query_selector.";
  }
  if (name == "page_get_attribute") {
    return "Return an attribute for a DOM ref from page_query_selector.";
  }
  if (name == "page_wait_for_selector") {
    return "Wait for a CSS selector to appear in the active page.";
  }
  if (name == "browser_wait_for_navigation") {
    return "Wait for navigation on the active tab to complete or time out.";
  }
  if (name == "browser_click") {
    return "Click an element by @ref from browser_accessibility_snapshot.";
  }
  if (name == "browser_type") {
    return "Type text into an element by @ref, refusing credential fields.";
  }
  if (name == "browser_select") {
    return "Select a value on an element by @ref.";
  }
  if (name == "browser_scroll") {
    return "Scroll the viewport or an element by @ref.";
  }
  if (name == "browser_hover") {
    return "Hover over an element by @ref.";
  }
  if (name == "browser_key_press") {
    return "Press a keyboard key in the active tab.";
  }
  return "Run a browser action against the active tab.";
}

base::DictValue BuildApprovalMetadata(
    const maho::ai::BrowserActionContract& contract) {
  base::DictValue metadata;
  metadata.Set("kind", std::string(maho::ai::BrowserActionKindName(contract.kind)));
  metadata.Set("sensitivity",
               std::string(maho::ai::BrowserActionSensitivityName(
                   contract.sensitivity)));
  metadata.Set("approval",
               std::string(maho::ai::BrowserActionApprovalName(contract.approval)));
  metadata.Set("requires_approval", maho::ai::RequiresApproval(contract));
  metadata.Set("lease", std::string(maho::ai::BrowserActionLeaseName(contract.lease)));
  metadata.Set("requires_lease", maho::ai::RequiresLease(contract));
  metadata.Set("domain_policy",
               std::string(maho::ai::BrowserActionDomainPolicyName(
                   contract.domain_policy)));
  metadata.Set("empty_allowlist_policy",
               std::string(maho::ai::BrowserActionEmptyAllowlistPolicyName(
                   contract.empty_allowlist_policy)));
  return metadata;
}

MahoBrowserToolRegistry::ToolSchema MakeToolSchema(
    std::string name,
    std::string description,
    base::DictValue input_schema,
    const maho::ai::BrowserActionContract* browser_action_contract = nullptr) {
  MahoBrowserToolRegistry::ToolSchema schema;
  schema.name = std::move(name);
  schema.description = std::move(description);
  schema.input_schema = std::move(input_schema);
  schema.browser_action_contract = browser_action_contract;
  return schema;
}

std::vector<MahoBrowserToolRegistry::ToolSchema> BuildPhase1ToolSchemas() {
  std::vector<MahoBrowserToolRegistry::ToolSchema> tools;
  tools.push_back(MakeToolSchema(
      "read_current_page",
      "Read the currently active page and return browser-ready page context.",
      BuildNoArgSchema()));
  tools.push_back(MakeToolSchema(
      "get_selected_text",
      "Return the current text selection from the active browser tab.",
      BuildNoArgSchema()));
  tools.push_back(MakeToolSchema(
      "get_active_tab",
      "Return metadata for the active browser tab in the current window.",
      BuildNoArgSchema()));
  tools.push_back(MakeToolSchema(
      "search_in_page",
      "Search the active page content and return matching excerpts.",
      BuildSearchSchema()));
  tools.push_back(MakeToolSchema(
      "extract_structured_page_context",
      "Extract structured context from the active page for browser chat "
      "orchestration.",
      BuildNoArgSchema()));
  tools.push_back(MakeToolSchema(
      "mail_list_accounts", "List Mail accounts available to Maho Mail.",
      BuildNoArgSchema()));
  tools.push_back(MakeToolSchema("mail_list_folders",
                                 "List folders for a Mail account.",
                                 BuildMailAccountSchema()));
  tools.push_back(MakeToolSchema("mail_list_emails",
                                 "List emails in a Mail account folder.",
                                 BuildMailListEmailsSchema()));
  tools.push_back(MakeToolSchema("mail_get_email", "Read a single email by id.",
                                 BuildMailIdSchema("email_id", "Email id.")));
  tools.push_back(MakeToolSchema(
      "mail_search_emails",
      "Search emails using a Mail search query JSON object.",
      BuildMailSearchSchema()));
  tools.push_back(MakeToolSchema("mail_list_thread",
                                 "List all emails in a Mail thread.",
                                 BuildMailThreadSchema()));
  for (const maho::ai::BrowserActionContract& contract :
       maho::ai::kMahoBrowserActionContracts) {
    tools.push_back(MakeToolSchema(
        std::string(contract.tool_name), BrowserActionDescription(contract),
        BuildBrowserActionInputSchema(contract.tool_name), &contract));
  }
  return tools;
}

}  // namespace

MahoBrowserToolRegistry::ToolSchema::ToolSchema() = default;
MahoBrowserToolRegistry::ToolSchema::ToolSchema(ToolSchema&&) = default;
MahoBrowserToolRegistry::ToolSchema&
MahoBrowserToolRegistry::ToolSchema::operator=(ToolSchema&&) = default;
MahoBrowserToolRegistry::ToolSchema::~ToolSchema() = default;

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
    if (schema.browser_action_contract) {
      tool.Set("approval_metadata",
               BuildApprovalMetadata(*schema.browser_action_contract));
    }
    tools.Append(std::move(tool));
  }
  return tools;
}

bool MahoBrowserToolRegistry::IsBrowserActionTool(
    const std::string& tool_name) {
  return maho::ai::FindBrowserActionContract(tool_name) != nullptr;
}
