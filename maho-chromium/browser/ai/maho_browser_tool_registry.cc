// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_browser_tool_registry.h"

#include <algorithm>
#include <initializer_list>
#include <set>
#include <utility>

#include "base/check.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/no_destructor.h"
#include "base/uuid.h"
#include "crypto/sha2.h"
#include "maho/browser/ai/maho_browser_action_contract.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"

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

base::DictValue BuildNumberSchema(std::string description) {
  base::DictValue schema;
  schema.Set("type", "number");
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

base::DictValue
BuildObjectSchema(base::DictValue properties,
                  std::initializer_list<std::string_view> required_names) {
  base::ListValue required;
  for (std::string_view name : required_names) {
    required.Append(name);
  }
  return BuildObjectSchema(std::move(properties), std::move(required));
}

base::DictValue BuildIntegerSchema(std::string description, int minimum,
                                   int maximum) {
  base::DictValue schema = BuildIntegerSchema(std::move(description));
  schema.Set("minimum", minimum);
  schema.Set("maximum", maximum);
  return schema;
}

base::DictValue BuildStringMapSchema(std::string description) {
  base::DictValue schema;
  schema.Set("type", "object");
  schema.Set("description", std::move(description));
  base::DictValue values;
  values.Set("type", "string");
  schema.Set("additionalProperties", std::move(values));
  return schema;
}

base::DictValue MissingCatalogInputSchema(std::string_view tool_name) {
  CHECK(tool_name.empty());
  return BuildNoArgSchema();
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
  properties.Set("limit",
                 BuildIntegerSchema("Maximum emails to return; default 50."));
  properties.Set("offset", BuildIntegerSchema("Pagination offset; default 0."));
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
  properties.Set("query_json",
                 BuildStringSchema(
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

base::DictValue BuildStringArraySchema(std::string description) {
  base::DictValue schema;
  schema.Set("type", "array");
  schema.Set("description", std::move(description));
  schema.Set("items", BuildStringSchema("Email address."));
  return schema;
}

base::DictValue BuildMailAttachmentsSchema() {
  base::DictValue item_properties;
  item_properties.Set("filename", BuildStringSchema("Attachment file name."));
  item_properties.Set(
      "mime_type", BuildStringSchema("Attachment MIME type, e.g. image/png."));
  item_properties.Set("data_base64",
                      BuildStringSchema("Attachment bytes, base64-encoded."));
  base::ListValue item_required;
  item_required.Append("filename");
  item_required.Append("mime_type");
  item_required.Append("data_base64");

  base::DictValue schema;
  schema.Set("type", "array");
  schema.Set("description",
             "Optional attachments. Size limits are enforced by the mail "
             "provider; oversized attachments are rejected on send.");
  schema.Set("items", BuildObjectSchema(std::move(item_properties),
                                        std::move(item_required)));
  return schema;
}

void AddComposeBodyProperties(base::DictValue &properties) {
  properties.Set("cc", BuildStringArraySchema("CC recipients."));
  properties.Set("bcc", BuildStringArraySchema("BCC recipients."));
  properties.Set("body_text", BuildStringSchema("Plain-text body."));
  properties.Set("body_html", BuildStringSchema("HTML body."));
  properties.Set("in_reply_to",
                 BuildStringSchema("RFC message id this replies to."));
  properties.Set("references",
                 BuildStringSchema("RFC references header value."));
  properties.Set("attachments", BuildMailAttachmentsSchema());
}

base::DictValue BuildMailComposeSchema() {
  base::DictValue properties;
  properties.Set("account_id", BuildStringSchema("Sending Mail account id."));
  properties.Set("to", BuildStringArraySchema("Primary recipients."));
  properties.Set("subject", BuildStringSchema("Email subject."));
  AddComposeBodyProperties(properties);
  base::ListValue required;
  required.Append("account_id");
  required.Append("to");
  required.Append("subject");
  return BuildObjectSchema(std::move(properties), std::move(required));
}

base::DictValue BuildMailUpdateDraftSchema() {
  base::DictValue properties;
  properties.Set("draft_id", BuildStringSchema("Draft id to update."));
  properties.Set("account_id", BuildStringSchema("Owning Mail account id."));
  properties.Set("to", BuildStringArraySchema("Primary recipients."));
  properties.Set("subject", BuildStringSchema("Email subject."));
  AddComposeBodyProperties(properties);
  base::ListValue required;
  required.Append("draft_id");
  required.Append("account_id");
  required.Append("to");
  required.Append("subject");
  return BuildObjectSchema(std::move(properties), std::move(required));
}

base::DictValue BuildRoutineTriggerSchema() {
  base::DictValue schema;
  schema.Set("type", "string");
  base::ListValue enum_list;
  enum_list.Append("cron");
  enum_list.Append("event");
  schema.Set("enum", std::move(enum_list));
  schema.Set(
      "description",
      "Trigger kind: 'cron' for schedule or 'event' for browser events "
      "(event triggers are deferred and disabled until emitters are wired).");
  return schema;
}

base::DictValue BuildRoutineCreateSchema() {
  base::DictValue properties;
  properties.Set("name", BuildStringSchema("Routine display name."));
  properties.Set(
      "prompt",
      BuildStringSchema(
          "Stored natural language prompt or recipe to execute. "
          "Arbitrary executable or script payloads are strictly forbidden."));
  properties.Set("trigger", BuildRoutineTriggerSchema());
  properties.Set(
      "schedule",
      BuildStringSchema(
          "Cron schedule expression (e.g. '0 9 * * *') when trigger is 'cron'."));
  properties.Set(
      "event",
      BuildStringSchema(
          "Event trigger name (e.g. 'on_startup', 'on_many_tabs') when trigger is 'event'."));
  return BuildObjectSchema(std::move(properties), {"name", "prompt"});
}

base::DictValue BuildRoutineUpdateSchema() {
  base::DictValue properties;
  properties.Set("id", BuildStringSchema("Routine ID to update."));
  properties.Set("name", BuildStringSchema("Updated routine name."));
  properties.Set(
      "prompt",
      BuildStringSchema(
          "Updated stored prompt or recipe. "
          "Arbitrary executable or script payloads are strictly forbidden."));
  properties.Set("trigger", BuildRoutineTriggerSchema());
  properties.Set("schedule",
                 BuildStringSchema("Updated cron schedule expression."));
  properties.Set("event",
                 BuildStringSchema("Updated event trigger name."));
  properties.Set("enabled",
                 BuildBooleanSchema("Whether the routine is enabled."));
  return BuildObjectSchema(std::move(properties), {"id"});
}

base::DictValue BuildRoutineDeleteSchema() {
  base::DictValue properties;
  properties.Set("id", BuildStringSchema("Routine ID to delete."));
  return BuildObjectSchema(std::move(properties), {"id"});
}

base::DictValue BuildRoutineHistorySchema() {
  base::DictValue properties;
  properties.Set(
      "routine_id",
      BuildStringSchema("Optional routine ID to filter execution history."));
  properties.Set(
      "limit",
      BuildIntegerSchema("Maximum history rows to return; default 50.", 1, 100));
  return BuildObjectSchema(std::move(properties), {});
}

void AddOptionalTabId(base::DictValue &properties) {
  properties.Set(
      "tab_id",
      BuildIntegerSchema(
          "Browser tab id. Mutations require it explicitly (-32013 without "
          "it); read-only tools fall back to the active tab when omitted."));
}

base::DictValue BuildBrowserActionInputSchema(std::string_view tool_name,
                                              bool catalog_surface = false) {
  base::DictValue properties;
  base::ListValue required;
  AddOptionalTabId(properties);

  if (tool_name == "browser_navigate") {
    properties.Set("url", BuildStringSchema("Destination URL."));
    if (!catalog_surface) {
      properties.Set("page_derived_justification",
                     BuildBooleanSchema(
                         "True when the destination came from page content."));
    }
    required.Append("url");
  } else if (tool_name == "page_query_selector") {
    properties.Set("selector", BuildStringSchema("CSS selector to query."));
    required.Append("selector");
  } else if (tool_name == "page_get_text") {
    properties.Set("ref_id",
                   BuildStringSchema("DOM ref id from page_query_selector."));
    required.Append("ref_id");
  } else if (tool_name == "page_get_attribute") {
    properties.Set("ref_id",
                   BuildStringSchema("DOM ref id from page_query_selector."));
    properties.Set("attribute", BuildStringSchema("Attribute name to read."));
    required.Append("ref_id");
    required.Append("attribute");
  } else if (tool_name == "page_wait_for_selector") {
    properties.Set("selector", BuildStringSchema("CSS selector to wait for."));
    properties.Set("timeout_ms",
                   BuildIntegerSchema("Timeout in milliseconds."));
    required.Append("selector");
  } else if (tool_name == "browser_wait_for_navigation") {
    properties.Set("since_timestamp_ms",
                   BuildIntegerSchema(
                       "Only navigation events after this Unix timestamp."));
    properties.Set("timeout_ms",
                   BuildIntegerSchema("Timeout in milliseconds."));
  } else if (tool_name == "browser_click" || tool_name == "browser_hover") {
    properties.Set(
        "ref", BuildIntegerSchema(
                   "Accessibility ref from browser_accessibility_snapshot."));
    if (tool_name == "browser_click") {
      properties.Set(
          "force",
          BuildBooleanSchema(
              "Bypass occlusion check without untrusted wrong-target input."));
    }
    required.Append("ref");
  } else if (tool_name == "browser_history_back") {
    required.Append("tab_id");
  } else if (tool_name == "browser_type") {
    properties.Set(
        "ref", BuildIntegerSchema(
                   "Accessibility ref from browser_accessibility_snapshot."));
    properties.Set(
        "text",
        BuildStringSchema(
            "Text to type. Credential fields require explicit user approval."));
    if (catalog_surface) {
      properties.Set(
          "allow_credentials",
          BuildBooleanSchema(
              "Request user-approved generic typing into a credential field."));
    }
    required.Append("ref");
    required.Append("text");
  } else if (tool_name == "browser_file_upload_select") {
    properties.Set("path",
                   BuildStringSchema("Absolute path to the local file to select."));
    properties.Set("selector",
                   BuildStringSchema("Optional CSS selector for file input element."));
    properties.Set("css",
                   BuildStringSchema("Alias for the file input CSS selector."));
    required.Append("path");
  } else if (tool_name == "browser_select") {
    properties.Set(
        "ref", BuildIntegerSchema(
                   "Accessibility ref from browser_accessibility_snapshot."));
    properties.Set("value", BuildStringSchema("Option value to select."));
    required.Append("ref");
    required.Append("value");
  } else if (tool_name == "browser_scroll") {
    properties.Set(
        "direction",
        BuildStringSchema("Scroll direction: up, down, left, or right."));
    properties.Set("pixels",
                   BuildIntegerSchema("Pixels to scroll; default 300."));
    properties.Set(
        "ref",
        BuildIntegerSchema("Optional accessibility ref to scroll an element."));
    required.Append("direction");
  } else if (tool_name == "browser_key_press") {
    properties.Set("key",
                   BuildStringSchema("Keyboard key, e.g. Enter or ArrowDown."));
    properties.Set(
        "modifiers",
        BuildStringListSchema("Optional modifiers: shift, ctrl, alt, meta."));
    required.Append("key");
  } else if (tool_name == "browser_visual_click" ||
             tool_name == "browser.visual_click") {
    properties.Set("frame_token",
                   BuildStringSchema("Server-issued visual frame token."));
    base::DictValue point;
    point.Set("x", BuildNumberSchema("X coordinate in CSS pixels."));
    point.Set("y", BuildNumberSchema("Y coordinate in CSS pixels."));
    properties.Set("click_point_css",
                   BuildObjectSchema(std::move(point), {"x", "y"}));
    base::DictValue rect;
    rect.Set("x", BuildNumberSchema("X origin in CSS pixels."));
    rect.Set("y", BuildNumberSchema("Y origin in CSS pixels."));
    rect.Set("width", BuildNumberSchema("Width in CSS pixels."));
    rect.Set("height", BuildNumberSchema("Height in CSS pixels."));
    properties.Set("target_rect_css",
                   BuildObjectSchema(std::move(rect), {}));
    properties.Set("x", BuildNumberSchema("Optional direct X coordinate."));
    properties.Set("y", BuildNumberSchema("Optional direct Y coordinate."));
    properties.Set("ref",
                   BuildIntegerSchema("Optional accessibility ref of target element."));
    properties.Set("lease_epoch",
                   BuildIntegerSchema("Expected lease epoch."));
    properties.Set("capture_id",
                   BuildStringSchema("Optional capture id (alias for frame_token)."));
    required.Append("frame_token");
  } else if (tool_name == "browser_request_help" ||
             tool_name == "browser.request_help") {
    properties.Set("prompt",
                   BuildStringSchema("Prompt or question requesting human assistance."));
    properties.Set("timeout_ms",
                   BuildIntegerSchema("Optional timeout in milliseconds."));
    properties.Set("request_id",
                   BuildStringSchema("Optional request id; server-minted if omitted."));
    required.Append("prompt");
  } else if (tool_name == "browser_observe" ||
             tool_name == "browser.observe") {
    properties.Set("cursor",
                   BuildStringSchema("Optional pagination cursor."));
    properties.Set("max_tokens",
                   BuildIntegerSchema("Optional maximum tokens to return."));
    properties.Set("probe_hover",
                   BuildBooleanSchema("Optional flag to probe hoverable elements."));
  }

  return BuildObjectSchema(std::move(properties), std::move(required));
}

base::DictValue
BuildApprovalMetadata(const maho::ai::BrowserActionContract &contract) {
  base::DictValue metadata;
  metadata.Set("kind",
               std::string(maho::ai::BrowserActionKindName(contract.kind)));
  metadata.Set("sensitivity",
               std::string(maho::ai::BrowserActionSensitivityName(
                   contract.sensitivity)));
  metadata.Set("approval", std::string(maho::ai::BrowserActionApprovalName(
                               contract.approval)));
  metadata.Set("requires_approval", maho::ai::RequiresApproval(contract));
  metadata.Set("lease",
               std::string(maho::ai::BrowserActionLeaseName(contract.lease)));
  metadata.Set("requires_lease", maho::ai::RequiresLease(contract));
  metadata.Set("domain_policy",
               std::string(maho::ai::BrowserActionDomainPolicyName(
                   contract.domain_policy)));
  metadata.Set("empty_allowlist_policy",
               std::string(maho::ai::BrowserActionEmptyAllowlistPolicyName(
                   contract.empty_allowlist_policy)));
  return metadata;
}

using CapabilityDescriptor = MahoBrowserToolRegistry::CapabilityDescriptor;
using Category = MahoBrowserToolRegistry::Category;
using Mutability = MahoBrowserToolRegistry::Mutability;
using Broker = MahoBrowserToolRegistry::Broker;
using Boundary = MahoBrowserToolRegistry::Boundary;
using Sensitivity = MahoBrowserToolRegistry::Sensitivity;
using FeatureGate = MahoBrowserToolRegistry::FeatureGate;
using MissingPolicy = MahoBrowserToolRegistry::MissingPolicy;
using SurfaceMask = MahoBrowserToolRegistry::SurfaceMask;

constexpr SurfaceMask kDesktopAgent = MahoBrowserToolRegistry::kDesktopAgent;
constexpr SurfaceMask kBrowserMcp = MahoBrowserToolRegistry::kBrowserMcp;
constexpr SurfaceMask kControlPlane = MahoBrowserToolRegistry::kControlPlane;
constexpr SurfaceMask kDesktopAgentAndBrowserMcp = kDesktopAgent | kBrowserMcp;

const char *MutabilityName(MahoBrowserToolRegistry::Mutability value) {
  switch (value) {
  case MahoBrowserToolRegistry::Mutability::kReadOnly:
    return "read_only";
  case MahoBrowserToolRegistry::Mutability::kMutable:
    return "mutable";
  case MahoBrowserToolRegistry::Mutability::kUnknown:
    return "unknown";
  }
}

const char *MissingPolicyName(MahoBrowserToolRegistry::MissingPolicy value) {
  switch (value) {
  case MahoBrowserToolRegistry::MissingPolicy::kFailClosed:
    return "fail_closed";
  case MahoBrowserToolRegistry::MissingPolicy::kUnknown:
    return "unknown";
  }
}

const char *BrokerName(MahoBrowserToolRegistry::Broker value) {
  switch (value) {
  case MahoBrowserToolRegistry::Broker::kBrowser:
    return "browser";
  case MahoBrowserToolRegistry::Broker::kMail:
    return "mail";
  case MahoBrowserToolRegistry::Broker::kRoutines:
    return "routines";
  case MahoBrowserToolRegistry::Broker::kVault:
    return "vault";
  case MahoBrowserToolRegistry::Broker::kUnknown:
    return "unknown";
  }
}

const char *BoundaryName(MahoBrowserToolRegistry::Boundary value) {
  switch (value) {
  case MahoBrowserToolRegistry::Boundary::kSession:
    return "session";
  case MahoBrowserToolRegistry::Boundary::kProfile:
    return "profile";
  case MahoBrowserToolRegistry::Boundary::kTab:
    return "tab";
  case MahoBrowserToolRegistry::Boundary::kUnknown:
    return "unknown";
  }
}

const char *SensitivityName(MahoBrowserToolRegistry::Sensitivity value) {
  switch (value) {
  case MahoBrowserToolRegistry::Sensitivity::kLow:
    return "low";
  case MahoBrowserToolRegistry::Sensitivity::kSensitive:
    return "sensitive";
  case MahoBrowserToolRegistry::Sensitivity::kCredential:
    return "credential";
  case MahoBrowserToolRegistry::Sensitivity::kUnknown:
    return "unknown";
  }
}

const char *CategoryName(MahoBrowserToolRegistry::Category value) {
  switch (value) {
  case MahoBrowserToolRegistry::Category::kTabs:
    return "tabs";
  case MahoBrowserToolRegistry::Category::kNavigation:
    return "navigation";
  case MahoBrowserToolRegistry::Category::kPage:
    return "page";
  case MahoBrowserToolRegistry::Category::kInput:
    return "input";
  case MahoBrowserToolRegistry::Category::kHistory:
    return "history";
  case MahoBrowserToolRegistry::Category::kBookmarks:
    return "bookmarks";
  case MahoBrowserToolRegistry::Category::kPolicy:
    return "policy";
  case MahoBrowserToolRegistry::Category::kRoutines:
    return "routines";
  case MahoBrowserToolRegistry::Category::kMail:
    return "mail";
  case MahoBrowserToolRegistry::Category::kLease:
    return "lease";
  case MahoBrowserToolRegistry::Category::kVault:
    return "vault";
  case MahoBrowserToolRegistry::Category::kCapture:
    return "capture";
  case MahoBrowserToolRegistry::Category::kUnknown:
    return "unknown";
  }
}

base::DictValue BuildReceiptMetadata(
    const MahoBrowserToolRegistry::CapabilityDescriptor &descriptor,
    const MahoBrowserToolRegistry::ExecutionReceiptContext &context) {
  base::DictValue controller;
  if (!context.controller_id.empty()) {
    controller.Set("id", context.controller_id);
  }
  controller.Set("name", context.controller_name);
  controller.Set("type", context.controller_type);
  controller.Set("plane", context.control_plane);

  base::DictValue target;
  if (context.target_tab_id.has_value()) {
    target.Set("tabId", base::Value(*context.target_tab_id));
  }
  if (context.target_origin.has_value()) {
    target.Set("origin", *context.target_origin);
  }

  base::DictValue outcome;
  outcome.Set("status", context.outcome_status);
  if (!context.outcome_code.empty()) {
    outcome.Set("code", context.outcome_code);
  }

  base::DictValue timestamps;
  if (context.started_at_seconds > 0) {
    timestamps.Set("startedAt", context.started_at_seconds);
  }
  if (context.completed_at_seconds > 0) {
    timestamps.Set("completedAt", context.completed_at_seconds);
  }

  base::DictValue metadata;
  metadata.Set("controller", std::move(controller));
  metadata.Set("target", std::move(target));
  metadata.Set("category", CategoryName(descriptor.category));
  metadata.Set("sensitivity", SensitivityName(descriptor.sensitivity));
  metadata.Set("approval", context.approval);
  metadata.Set("outcome", std::move(outcome));
  metadata.Set("timestamps", std::move(timestamps));
  return metadata;
}

const char *FeatureGateName(MahoBrowserToolRegistry::FeatureGate value) {
  switch (value) {
  case MahoBrowserToolRegistry::FeatureGate::kAlways:
    return "always";
  case MahoBrowserToolRegistry::FeatureGate::kMailBeta:
    return "mail_beta";
  case MahoBrowserToolRegistry::FeatureGate::kRoutines:
    return "routines";
  case MahoBrowserToolRegistry::FeatureGate::kVault:
    return "vault";
  case MahoBrowserToolRegistry::FeatureGate::kNativeInput:
    return "native_input";
  case MahoBrowserToolRegistry::FeatureGate::kUnknown:
    return "unknown";
  }
}

Sensitivity
CapabilitySensitivity(const maho::ai::BrowserActionContract &contract) {
  return contract.sensitivity == maho::ai::BrowserActionSensitivity::kLow
             ? Sensitivity::kLow
             : Sensitivity::kSensitive;
}

CapabilityDescriptor
MakeBrowserActionDescriptor(std::string_view canonical_id,
                            std::string_view tool_name, Category category,
                            Broker broker, Boundary boundary,
                            FeatureGate feature_gate, SurfaceMask surfaces,
                            std::string_view description) {
  const maho::ai::BrowserActionContract *contract =
      maho::ai::FindBrowserActionContract(tool_name);
  CHECK(contract);
  CHECK(maho::ai::EmptyAllowlistFailsClosed(*contract));
  return {canonical_id,
          tool_name,
          MahoBrowserToolRegistry::kSchemaVersion,
          MahoBrowserToolRegistry::kResultVersion,
          category,
          contract->kind == maho::ai::BrowserActionKind::kRead
              ? Mutability::kReadOnly
              : Mutability::kMutable,
          contract->changes_authority,
          broker,
          boundary,
          CapabilitySensitivity(*contract),
          feature_gate,
          surfaces,
          MissingPolicy::kFailClosed,
          description,
          contract};
}

std::vector<CapabilityDescriptor> BuildCapabilityDescriptors() {
  std::vector<CapabilityDescriptor> descriptors;
#define kAction kInput
#define MAHO_BROWSER_CAPABILITY(id, canonical_id, tool_name, category,         \
                                mutability, authority_change, broker,          \
                                boundary, sensitivity, feature_gate, surfaces, \
                                missing_policy, description)                   \
  descriptors.push_back(                                                       \
      {canonical_id, tool_name, MahoBrowserToolRegistry::kSchemaVersion,       \
       MahoBrowserToolRegistry::kResultVersion, Category::k##category,         \
       Mutability::k##mutability,                                              \
       std::string_view(#authority_change) == "Yes", Broker::k##broker,        \
       Boundary::k##boundary, Sensitivity::k##sensitivity,                     \
       FeatureGate::k##feature_gate, k##surfaces,                              \
       MissingPolicy::k##missing_policy, description,                          \
       maho::ai::FindBrowserActionContract(tool_name)});
#define MAHO_BROWSER_ACTION_CAPABILITY(id, canonical_id, tool_name, category,  \
                                       broker, boundary, feature_gate,         \
                                       surfaces, description)                  \
  descriptors.push_back(MakeBrowserActionDescriptor(                           \
      canonical_id, tool_name, Category::k##category, Broker::k##broker,       \
      Boundary::k##boundary, FeatureGate::k##feature_gate, k##surfaces,        \
      description));
#include "maho/browser/ai/maho_browser_capability_catalog.def"
#undef MAHO_BROWSER_ACTION_CAPABILITY
#undef MAHO_BROWSER_CAPABILITY
#undef kAction
  for (CapabilityDescriptor& descriptor : descriptors) {
    if (descriptor.surfaces & MahoBrowserToolRegistry::kDesktopAgent) {
      descriptor.surfaces |= MahoBrowserToolRegistry::kCliAgent;
    }
    if (descriptor.surfaces & MahoBrowserToolRegistry::kPublicMcp) {
      descriptor.surfaces |= MahoBrowserToolRegistry::kCliGeneric;
    }
  }
  return descriptors;
}

bool FeatureEnabled(const CapabilityDescriptor &descriptor, bool mail_enabled,
                    bool routines_enabled, bool vault_enabled,
                    bool native_input_enabled) {
  switch (descriptor.feature_gate) {
  case FeatureGate::kUnknown:
    return false;
  case FeatureGate::kAlways:
    return true;
  case FeatureGate::kMailBeta:
    return mail_enabled;
  case FeatureGate::kRoutines:
    return routines_enabled;
  case FeatureGate::kVault:
    return vault_enabled;
  case FeatureGate::kNativeInput:
    return native_input_enabled;
  }
  return false;
}

bool HasCompletePublicMetadata(const CapabilityDescriptor &descriptor) {
  return !descriptor.canonical_id.empty() && !descriptor.tool_name.empty() &&
         descriptor.schema_version > 0 && descriptor.result_version > 0 &&
         descriptor.category != Category::kUnknown &&
         descriptor.mutability != Mutability::kUnknown &&
         descriptor.required_broker != Broker::kUnknown &&
         descriptor.required_boundary != Boundary::kUnknown &&
         descriptor.sensitivity != Sensitivity::kUnknown &&
         descriptor.feature_gate != FeatureGate::kUnknown &&
         descriptor.missing_policy == MissingPolicy::kFailClosed &&
         !descriptor.description.empty();
}

base::DictValue
BuildDesktopInputSchema(const CapabilityDescriptor &descriptor) {
  if (descriptor.browser_action_contract) {
    return BuildBrowserActionInputSchema(descriptor.tool_name);
  }
  if (descriptor.tool_name == "search_in_page") {
    return BuildSearchSchema();
  }
  if (descriptor.tool_name == "browser_history_search_desktop") {
    base::DictValue properties;
    properties.Set("query", BuildStringSchema("Text to search in history."));
    properties.Set("max_results",
                   BuildIntegerSchema("Maximum history results to return."));
    return BuildObjectSchema(std::move(properties), {});
  }
  if (descriptor.tool_name == "screenshot_proof") {
    base::DictValue properties;
    AddOptionalTabId(properties);
    return BuildObjectSchema(std::move(properties), {});
  }
  if (descriptor.tool_name == "mail_list_folders") {
    return BuildMailAccountSchema();
  }
  if (descriptor.tool_name == "mail_list_emails") {
    return BuildMailListEmailsSchema();
  }
  if (descriptor.tool_name == "mail_get_email") {
    return BuildMailIdSchema("email_id", "Email id.");
  }
  if (descriptor.tool_name == "mail_search_emails") {
    return BuildMailSearchSchema();
  }
  if (descriptor.tool_name == "mail_list_thread") {
    return BuildMailThreadSchema();
  }
  if (descriptor.tool_name == "mail_send" ||
      descriptor.tool_name == "mail_save_draft") {
    return BuildMailComposeSchema();
  }
  if (descriptor.tool_name == "mail_update_draft") {
    return BuildMailUpdateDraftSchema();
  }
  if (descriptor.tool_name == "browser_routines_create") {
    return BuildRoutineCreateSchema();
  }
  if (descriptor.tool_name == "browser_routines_update") {
    return BuildRoutineUpdateSchema();
  }
  if (descriptor.tool_name == "browser_routines_delete") {
    return BuildRoutineDeleteSchema();
  }
  if (descriptor.tool_name == "browser_routines_history") {
    return BuildRoutineHistorySchema();
  }
  if (descriptor.tool_name == "browser_request_help" ||
      descriptor.tool_name == "browser.request_help") {
    base::DictValue properties;
    properties.Set("prompt",
                   BuildStringSchema("Prompt or question requesting human assistance."));
    properties.Set("timeout_ms",
                   BuildIntegerSchema("Optional timeout in milliseconds."));
    properties.Set("request_id",
                   BuildStringSchema("Optional request id; server-minted if omitted."));
    return BuildObjectSchema(std::move(properties), {"prompt"});
  }
  return BuildNoArgSchema();
}

base::DictValue
BuildCatalogInputSchema(const CapabilityDescriptor &descriptor) {
  if (descriptor.browser_action_contract) {
    return BuildBrowserActionInputSchema(descriptor.tool_name,
                                         /*catalog_surface=*/true);
  }

  const std::string_view name = descriptor.tool_name;
  base::DictValue properties;
  if (name == "browser_tab_list" || name == "browser_get_blocked_domains" ||
      name == "browser_routines_list" || name == "mail_list_accounts" ||
      name == "browser_list_exact_origins") {
    return BuildNoArgSchema();
  }
  if (name == "browser_tab_get" || name == "browser_tab_close" ||
      name == "browser_tab_switch" || name == "browser_history_back" ||
      name == "browser_adopt_tab" ||
      name == "browser_release_tab" || name == "browser_release_lease" ||
      name == "browser_tab_return") {
    properties.Set("tab_id", BuildIntegerSchema("Browser tab id."));
    return BuildObjectSchema(std::move(properties), {"tab_id"});
  }
  if (name == "browser_tab_borrow") {
    properties.Set("tab_id", BuildIntegerSchema("Browser tab id."));
    properties.Set("origin_space_id",
                   BuildIntegerSchema("Source space id before borrowing."));
    properties.Set("agent_space_id",
                   BuildIntegerSchema("Destination agent space id."));
    properties.Set("ttl_seconds",
                   BuildIntegerSchema("Optional TTL in seconds (default 60)."));
    return BuildObjectSchema(std::move(properties), {"tab_id"});
  }
  if (name == "browser_tab_new") {
    properties.Set("url",
                   BuildStringSchema("URL to open; defaults to about:blank."));
    return BuildObjectSchema(std::move(properties), {});
  }
  if (name == "browser_history_search" ||
      name == "browser_history_search_desktop") {
    properties.Set("query", BuildStringSchema("Text to search in history."));
    properties.Set("max_results",
                   BuildIntegerSchema("Maximum history results to return."));
    return BuildObjectSchema(std::move(properties), {});
  }
  if (name == "page.accessibility_snapshot_v2" ||
      name == "page_accessibility_snapshot_v2") {
    AddOptionalTabId(properties);
    properties.Set("mode",
                   BuildStringSchema(
                       "Observation mode: 'interactive' (default), 'compact', or 'full'."));
    properties.Set("include_hidden",
                   BuildBooleanSchema(
                       "Whether to include hidden or invisible nodes; default false."));
    base::DictValue scope_props;
    scope_props.Set("selector",
                    BuildStringSchema("Optional CSS selector to scope the snapshot."));
    scope_props.Set("ref",
                    BuildIntegerSchema("Optional accessibility ref to scope the snapshot."));
    base::DictValue scope_schema;
    scope_schema.Set("type", "object");
    scope_schema.Set("description",
                     "Optional subtree scoping by CSS selector or accessibility ref.");
    scope_schema.Set("properties", std::move(scope_props));
    properties.Set("scope", std::move(scope_schema));
    properties.Set("since_snapshot_token",
                   BuildStringSchema(
                       "Optional snapshot token to compute differential changes against."));
    properties.Set("max_bytes",
                   BuildIntegerSchema(
                       "Maximum serialized snapshot size in bytes; default 24000."));
    properties.Set("max_depth",
                   BuildIntegerSchema("Maximum tree depth to traverse."));
    return BuildObjectSchema(std::move(properties), {});
  }
  if (name == "browser_bookmarks_search") {
    properties.Set("query", BuildStringSchema("Text to search in bookmarks."));
    return BuildObjectSchema(std::move(properties), {"query"});
  }
  if (name == "browser_bookmark_create") {
    properties.Set("title", BuildStringSchema("Bookmark title."));
    properties.Set("url", BuildStringSchema("Bookmark URL."));
    properties.Set("folder", BuildStringSchema("Optional bookmark folder."));
    return BuildObjectSchema(std::move(properties), {"title", "url"});
  }
  if (name == "browser_page_content" || name == "browser_page_text" ||
      name == "browser_page_context" || name == "browser_screenshot_full" ||
      name == "screenshot_proof" ||
      name == "browser_network_start_capture" ||
      name == "browser_network_stop_capture" ||
      name == "browser_network_get_har" ||
      name == "vault_list_credentials_for_active_page") {
    AddOptionalTabId(properties);
    return BuildObjectSchema(std::move(properties), {});
  }
  if (name == "browser_search_in_page") {
    AddOptionalTabId(properties);
    properties.Set("query", BuildStringSchema("Text to find in the page."));
    return BuildObjectSchema(std::move(properties), {"query"});
  }
  if (name == "browser_same_origin_fetch") {
    AddOptionalTabId(properties);
    properties.Set("url", BuildStringSchema("Same-origin HTTPS URL."));
    properties.Set("method", BuildStringSchema("HTTP method: GET or POST."));
    properties.Set("body", BuildStringSchema("Optional request body."));
    properties.Set("headers",
                   BuildStringMapSchema("Optional string-valued headers."));
    return BuildObjectSchema(std::move(properties), {"url"});
  }
  if (name == "browser_set_blocked_domains") {
    properties.Set("domains", BuildStringListSchema(
                                  "Domains or wildcard domains to block."));
    return BuildObjectSchema(std::move(properties), {"domains"});
  }
  if (name == "browser_routines_run") {
    properties.Set("id", BuildStringSchema("Routine id."));
    return BuildObjectSchema(std::move(properties), {"id"});
  }
  if (name == "browser_routines_create") {
    return BuildRoutineCreateSchema();
  }
  if (name == "browser_routines_update") {
    return BuildRoutineUpdateSchema();
  }
  if (name == "browser_routines_delete") {
    return BuildRoutineDeleteSchema();
  }
  if (name == "browser_routines_history") {
    return BuildRoutineHistorySchema();
  }
  if (name == "mail_list_folders") {
    return BuildMailAccountSchema();
  }
  if (name == "mail_list_emails") {
    return BuildMailListEmailsSchema();
  }
  if (name == "mail_get_email") {
    return BuildMailIdSchema("email_id", "Email id.");
  }
  if (name == "mail_search_emails") {
    properties.Set("query", BuildStringSchema("Mail search text."));
    properties.Set("account_id",
                   BuildStringSchema("Optional mail account id."));
    properties.Set("folder_id", BuildStringSchema("Optional mail folder id."));
    properties.Set("limit", BuildIntegerSchema("Maximum results; default 50."));
    properties.Set("offset",
                   BuildIntegerSchema("Pagination offset; default 0."));
    return BuildObjectSchema(std::move(properties), {"query"});
  }
  if (name == "mail_list_thread") {
    return BuildMailThreadSchema();
  }
  if (name == "mail_extract_otp") {
    properties.Set("account_id",
                   BuildStringSchema("Optional mail account id."));
    properties.Set("folder_id", BuildStringSchema("Optional mail folder id."));
    properties.Set("query", BuildStringSchema("Optional OTP search text."));
    properties.Set("max_age_seconds",
                   BuildIntegerSchema("Maximum message age; default 300."));
    return BuildObjectSchema(std::move(properties), {});
  }
  if (name == "mail_send" || name == "mail_save_draft" ||
      name == "mail_queue_email" || name == "mail_flag" ||
      name == "mail_add_account") {
    properties.Set(
        "request_json",
        BuildStringSchema("JSON request accepted by the mail broker."));
    return BuildObjectSchema(std::move(properties), {"request_json"});
  }
  if (name == "mail_update_draft") {
    properties.Set("draft_id", BuildStringSchema("Draft id to update."));
    properties.Set(
        "request_json",
        BuildStringSchema("JSON request accepted by the mail broker."));
    return BuildObjectSchema(std::move(properties),
                             {"draft_id", "request_json"});
  }
  if (name == "mail_test_connection") {
    properties.Set("params_json",
                   BuildStringSchema("JSON connection parameters."));
    return BuildObjectSchema(std::move(properties), {"params_json"});
  }
  if (name == "mail_delete_account" || name == "mail_reconnect_account") {
    return BuildMailAccountSchema();
  }
  if (name == "mail_start_oauth") {
    properties.Set("provider", BuildStringSchema("OAuth provider."));
    properties.Set("client_id", BuildStringSchema("OAuth client id."));
    properties.Set("redirect_uri", BuildStringSchema("OAuth redirect URI."));
    return BuildObjectSchema(std::move(properties),
                             {"provider", "client_id", "redirect_uri"});
  }
  if (name == "mail_complete_oauth") {
    properties.Set("state", BuildStringSchema("OAuth state value."));
    properties.Set("code", BuildStringSchema("OAuth authorization code."));
    return BuildObjectSchema(std::move(properties), {"state", "code"});
  }
  if (name == "mail_import_migration_archive") {
    properties.Set("archive_json",
                   BuildStringSchema("JSON mail migration archive."));
    return BuildObjectSchema(std::move(properties), {"archive_json"});
  }
  if (name == "browser_acquire_lease" || name == "browser_heartbeat_lease") {
    properties.Set("tab_id", BuildIntegerSchema("Browser tab id."));
    properties.Set(
        "ttl_seconds",
        BuildIntegerSchema("Lease duration in seconds; default 60."));
    return BuildObjectSchema(std::move(properties), {"tab_id"});
  }
  if (name == "browser_release_lease" || name == "browser_tab_return") {
    properties.Set("tab_id", BuildIntegerSchema("Browser tab id."));
    return BuildObjectSchema(std::move(properties), {"tab_id"});
  }
  if (name == "browser_tab_borrow") {
    properties.Set("tab_id", BuildIntegerSchema("Browser tab id."));
    properties.Set("origin_space_id",
                   BuildIntegerSchema("Source space id before borrowing."));
    properties.Set("agent_space_id",
                   BuildIntegerSchema("Destination agent space id."));
    properties.Set("ttl_seconds",
                   BuildIntegerSchema("Optional TTL in seconds (default 60)."));
    return BuildObjectSchema(std::move(properties), {"tab_id"});
  }
  if (name == "browser_grant_exact_origin" ||
      name == "browser_revoke_exact_origin") {
    properties.Set("origin", BuildStringSchema("Exact HTTP or HTTPS origin."));
    return BuildObjectSchema(std::move(properties), {"origin"});
  }
  if (name == "artifact.list") {
    properties.Set("session_id",
                   BuildStringSchema("Optional owning Agent session id."));
    return BuildObjectSchema(std::move(properties), {});
  }
  if (name == "artifact.export") {
    properties.Set("artifact_id", BuildStringSchema("Artifact id."));
    properties.Set("destination",
                   BuildStringSchema("Explicit local destination path."));
    return BuildObjectSchema(std::move(properties),
                             {"artifact_id", "destination"});
  }
  if (name == "vault_request_credential_use") {
    AddOptionalTabId(properties);
    properties.Set("handle", BuildStringSchema("Opaque credential handle."));
    properties.Set("origin", BuildStringSchema("Exact credential origin."));
    return BuildObjectSchema(std::move(properties), {"handle", "origin"});
  }
  if (name == "vault_fill_credential" || name == "vault_fill_totp") {
    AddOptionalTabId(properties);
    properties.Set("grant_handle",
                   BuildStringSchema("Short-lived credential fill grant."));
    properties.Set("ref", BuildIntegerSchema("Accessibility element ref."));
    return BuildObjectSchema(std::move(properties), {"grant_handle", "ref"});
  }
  if (name == "browser_screenshot_element") {
    AddOptionalTabId(properties);
    properties.Set("ref", BuildIntegerSchema("Accessibility element ref."));
    return BuildObjectSchema(std::move(properties), {"ref"});
  }
  if (name == "browser_console_messages") {
    AddOptionalTabId(properties);
    properties.Set("limit",
                   BuildIntegerSchema("Maximum messages; default 100."));
    properties.Set(
        "since_timestamp_ms",
        BuildIntegerSchema("Only messages after this Unix timestamp."));
    properties.Set("level_filter",
                   BuildStringListSchema("Console levels to include."));
    return BuildObjectSchema(std::move(properties), {});
  }
  if (name == "browser_set_viewport_size") {
    AddOptionalTabId(properties);
    properties.Set("width_px",
                   BuildIntegerSchema("Viewport width in pixels.", 100, 4096));
    properties.Set("height_px",
                   BuildIntegerSchema("Viewport height in pixels.", 100, 4096));
    return BuildObjectSchema(std::move(properties), {"width_px", "height_px"});
  }

  // NOTE: page_accessibility_snapshot_v2 (both dotted and underscored
  // spellings) is handled by the block above, which returns first — keep
  // that block as the single source for its schema.
  if (name == "browser_locator_click") {
    AddOptionalTabId(properties);
    base::DictValue locator;
    locator.Set("ref", BuildStringSchema("Existing element ref (@e<N>)."));
    locator.Set("css", BuildStringSchema("CSS selector for the target element."));
    locator.Set("role", BuildStringSchema("ARIA role when using role+name lookup."));
    locator.Set("name",
                BuildStringSchema("Accessible name when using role+name lookup."));
    locator.Set("exact",
                BuildBooleanSchema("Require an exact role+name match (default true)."));
    locator.Set("force",
                BuildBooleanSchema("Bypass occlusion check without untrusted wrong-target input."));
    properties.Set("locator", BuildObjectSchema(std::move(locator), {}));
    properties.Set("force",
                   BuildBooleanSchema("Bypass occlusion check without untrusted wrong-target input."));
    properties.Set("observe",
                   BuildStringSchema("Post-action observation: diff or none."));
    properties.Set("wait",
                   BuildStringSchema("Wait strategy: auto, navigation, or selector."));
    properties.Set("lease",
                   BuildStringSchema("Lease mode: scoped keeps steal/approval policy."));
    return BuildObjectSchema(std::move(properties), {"locator"});
  }
  if (name == "browser_locator_type") {
    AddOptionalTabId(properties);
    base::DictValue locator;
    locator.Set("ref", BuildStringSchema("Existing element ref (@e<N>)."));
    locator.Set("css", BuildStringSchema("CSS selector for the target element."));
    locator.Set("role", BuildStringSchema("ARIA role when using role+name lookup."));
    locator.Set("name",
                BuildStringSchema("Accessible name when using role+name lookup."));
    locator.Set("exact",
                BuildBooleanSchema("Require an exact role+name match (default true)."));
    properties.Set("locator", BuildObjectSchema(std::move(locator), {}));
    properties.Set("text", BuildStringSchema("Text to type into the element."));
    properties.Set("submit", BuildBooleanSchema("Press Enter after typing."));
    properties.Set("observe",
                   BuildStringSchema("Post-action observation: diff or none."));
    properties.Set("wait",
                   BuildStringSchema("Wait strategy: auto, navigation, or selector."));
    properties.Set("lease",
                   BuildStringSchema("Lease mode: scoped keeps steal/approval policy."));
    return BuildObjectSchema(std::move(properties), {"locator", "text"});
  }
  if (name == "browser_act_and_observe") {
    AddOptionalTabId(properties);
    properties.Set(
        "action", BuildStringSchema("Action: click, type, navigate, or observe."));
    base::DictValue locator;
    locator.Set("ref", BuildStringSchema("Existing element ref (@e<N>)."));
    locator.Set("css", BuildStringSchema("CSS selector for the target element."));
    locator.Set("role", BuildStringSchema("ARIA role when using role+name lookup."));
    locator.Set("name",
                BuildStringSchema("Accessible name when using role+name lookup."));
    properties.Set("locator", BuildObjectSchema(std::move(locator), {}));
    properties.Set("text", BuildStringSchema("Text for type actions."));
    properties.Set("url", BuildStringSchema("URL for navigate actions."));
    properties.Set("submit", BuildBooleanSchema("Press Enter after typing."));
    properties.Set("wait",
                   BuildStringSchema("Wait strategy: auto, navigation, or selector."));
    properties.Set("observe",
                   BuildStringSchema("Post-action observation: diff or none."));
    properties.Set(
        "since_snapshot_token",
        BuildStringSchema("Snapshot token to diff against for observe actions."));
    properties.Set("lease",
                   BuildStringSchema("Lease mode: scoped keeps steal/approval policy."));
    return BuildObjectSchema(std::move(properties), {"action"});
  }

  if (name == "browser_request_help" || name == "browser.request_help") {
    properties.Set("prompt",
                   BuildStringSchema("Prompt or question requesting human assistance."));
    properties.Set("timeout_ms",
                   BuildIntegerSchema("Optional timeout in milliseconds."));
    properties.Set("request_id",
                   BuildStringSchema("Optional request id; server-minted if omitted."));
    return BuildObjectSchema(std::move(properties), {"prompt"});
  }

  if (name == "browser_observe" || name == "browser.observe") {
    AddOptionalTabId(properties);
    properties.Set("cursor",
                   BuildStringSchema("Optional pagination cursor."));
    properties.Set("max_tokens",
                   BuildIntegerSchema("Optional maximum tokens to return."));
    properties.Set("probe_hover",
                   BuildBooleanSchema("Optional flag to probe hoverable elements."));
    return BuildObjectSchema(std::move(properties), {});
  }

  return MissingCatalogInputSchema(name);
}

MahoBrowserToolRegistry::ToolSchema MakeToolSchema(
    std::string name, std::string description, base::DictValue input_schema,
    const maho::ai::BrowserActionContract *browser_action_contract = nullptr) {
  MahoBrowserToolRegistry::ToolSchema schema;
  schema.name = std::move(name);
  schema.description = std::move(description);
  schema.input_schema = std::move(input_schema);
  schema.browser_action_contract = browser_action_contract;
  return schema;
}

std::vector<MahoBrowserToolRegistry::ToolSchema> BuildPhase1ToolSchemas() {
  std::vector<MahoBrowserToolRegistry::ToolSchema> tools;
  for (const CapabilityDescriptor *descriptor :
       MahoBrowserToolRegistry::GetCapabilitiesForSurface(
           MahoBrowserToolRegistry::kDesktopAgent)) {
    tools.push_back(MakeToolSchema(std::string(descriptor->tool_name),
                                   std::string(descriptor->description),
                                   BuildDesktopInputSchema(*descriptor),
                                   descriptor->browser_action_contract));
  }
  return tools;
}

}  // namespace

MahoBrowserToolRegistry::CapabilityDescriptor::CapabilityDescriptor(
    std::string_view canonical_id,
    std::string_view tool_name,
    int schema_version,
    int result_version,
    Category category,
    Mutability mutability,
    bool changes_authority,
    Broker required_broker,
    Boundary required_boundary,
    Sensitivity sensitivity,
    FeatureGate feature_gate,
    SurfaceMask surfaces,
    MissingPolicy missing_policy,
    std::string_view description,
    const maho::ai::BrowserActionContract* browser_action_contract)
    : canonical_id(canonical_id),
      tool_name(tool_name),
      schema_version(schema_version),
      result_version(result_version),
      category(category),
      mutability(mutability),
      changes_authority(changes_authority),
      required_broker(required_broker),
      required_boundary(required_boundary),
      sensitivity(sensitivity),
      feature_gate(feature_gate),
      surfaces(surfaces),
      missing_policy(missing_policy),
      description(description),
      browser_action_contract(browser_action_contract) {}

MahoBrowserToolRegistry::CapabilityDescriptor::CapabilityDescriptor(
    const CapabilityDescriptor&) = default;
MahoBrowserToolRegistry::CapabilityDescriptor&
MahoBrowserToolRegistry::CapabilityDescriptor::operator=(
    const CapabilityDescriptor&) = default;
MahoBrowserToolRegistry::CapabilityDescriptor::CapabilityDescriptor(
    CapabilityDescriptor&&) = default;
MahoBrowserToolRegistry::CapabilityDescriptor&
MahoBrowserToolRegistry::CapabilityDescriptor::operator=(
    CapabilityDescriptor&&) = default;
MahoBrowserToolRegistry::CapabilityDescriptor::~CapabilityDescriptor() =
    default;

MahoBrowserToolRegistry::ExecutionReceiptContext::ExecutionReceiptContext() =
    default;
MahoBrowserToolRegistry::ExecutionReceiptContext::~ExecutionReceiptContext() =
    default;

// static
const std::vector<MahoBrowserToolRegistry::CapabilityDescriptor>&
MahoBrowserToolRegistry::GetCapabilityDescriptors() {
  static const base::NoDestructor<std::vector<CapabilityDescriptor>>
      kDescriptors(BuildCapabilityDescriptors());
  CHECK(ValidateCapabilityDescriptors(*kDescriptors));
  return *kDescriptors;
}

// static
bool MahoBrowserToolRegistry::ValidateCapabilityDescriptors(
    base::span<const CapabilityDescriptor> descriptors) {
  std::set<std::string_view> canonical_ids;
  std::set<std::string_view> tool_names;
  const SurfaceMask kKnownSurfaces =
      kDesktopAgent | kBrowserMcp | kControlPlane | kCliAgent | kCliGeneric;
  for (const CapabilityDescriptor &descriptor : descriptors) {
    if (descriptor.canonical_id.empty() || descriptor.tool_name.empty() ||
        descriptor.schema_version <= 0 || descriptor.result_version <= 0 ||
        descriptor.category == Category::kUnknown ||
        descriptor.mutability == Mutability::kUnknown ||
        descriptor.required_broker == Broker::kUnknown ||
        descriptor.required_boundary == Boundary::kUnknown ||
        descriptor.sensitivity == Sensitivity::kUnknown ||
        descriptor.feature_gate == FeatureGate::kUnknown ||
        descriptor.surfaces == 0 ||
        (descriptor.surfaces & ~kKnownSurfaces) != 0 ||
        descriptor.missing_policy != MissingPolicy::kFailClosed ||
        descriptor.description.empty() ||
        !canonical_ids.insert(descriptor.canonical_id).second ||
        !tool_names.insert(descriptor.tool_name).second) {
      return false;
    }
    const maho::ai::BrowserActionContract *contract =
        maho::ai::FindBrowserActionContract(descriptor.tool_name);
    if (contract != descriptor.browser_action_contract) {
      return false;
    }
    if (contract &&
        (descriptor.mutability == Mutability::kReadOnly) !=
            (contract->kind == maho::ai::BrowserActionKind::kRead)) {
      return false;
    }
    if (contract &&
        descriptor.changes_authority != contract->changes_authority) {
      return false;
    }
  }
  return true;
}

// static
const MahoBrowserToolRegistry::CapabilityDescriptor *
MahoBrowserToolRegistry::FindCapability(std::string_view tool_name) {
  for (const CapabilityDescriptor &descriptor : GetCapabilityDescriptors()) {
    if (descriptor.tool_name == tool_name ||
        descriptor.canonical_id == tool_name) {
      return &descriptor;
    }
  }
  return nullptr;
}

const MahoBrowserToolRegistry::CapabilityDescriptor *
MahoBrowserToolRegistry::FindCapabilityById(std::string_view canonical_id) {
  for (const CapabilityDescriptor &descriptor : GetCapabilityDescriptors()) {
    if (descriptor.canonical_id == canonical_id) {
      return &descriptor;
    }
  }
  return nullptr;
}

// static
std::vector<const MahoBrowserToolRegistry::CapabilityDescriptor *>
MahoBrowserToolRegistry::GetCapabilitiesForSurface(Surface surface,
                                                   bool mail_enabled,
                                                   bool routines_enabled,
                                                   bool vault_enabled,
                                                   bool native_input_enabled) {
  std::vector<const CapabilityDescriptor *> capabilities;
  for (const CapabilityDescriptor &descriptor : GetCapabilityDescriptors()) {
    if (HasSurface(descriptor, surface) &&
        FeatureEnabled(descriptor, mail_enabled, routines_enabled,
                       vault_enabled, native_input_enabled)) {
      capabilities.push_back(&descriptor);
    }
  }
  return capabilities;
}

// static
std::optional<base::DictValue>
MahoBrowserToolRegistry::SerializePublicMcpCapability(
    const CapabilityDescriptor &descriptor) {
  if (!HasSurface(descriptor, kPublicMcp) ||
      !HasCompletePublicMetadata(descriptor)) {
    return std::nullopt;
  }

  base::DictValue policy;
  policy.Set("mutability", MutabilityName(descriptor.mutability));
  policy.Set("changesAuthority", descriptor.changes_authority);
  policy.Set("requiredBroker", BrokerName(descriptor.required_broker));
  policy.Set("requiredBoundary", BoundaryName(descriptor.required_boundary));
  policy.Set("sensitivity", SensitivityName(descriptor.sensitivity));
  policy.Set("featureGate", FeatureGateName(descriptor.feature_gate));
  policy.Set("missingPolicy", MissingPolicyName(descriptor.missing_policy));

  base::DictValue tool;
  tool.Set("name", descriptor.tool_name);
  tool.Set("description", descriptor.description);
  tool.Set("inputSchema", BuildCatalogInputSchema(descriptor));
  tool.Set("capabilityId", descriptor.canonical_id);
  tool.Set("schemaVersion", descriptor.schema_version);
  tool.Set("resultVersion", descriptor.result_version);
  tool.Set("policy", std::move(policy));
  return tool;
}

std::optional<base::DictValue>
MahoBrowserToolRegistry::SerializeControlPlaneCapability(
    const CapabilityDescriptor &descriptor) {
  if (!HasSurface(descriptor, kControlPlane) ||
      !HasCompletePublicMetadata(descriptor)) {
    return std::nullopt;
  }
  base::DictValue tool;
  tool.Set("name", descriptor.tool_name);
  tool.Set("description", descriptor.description);
  tool.Set("inputSchema", BuildCatalogInputSchema(descriptor));
  tool.Set("capabilityId", descriptor.canonical_id);
  tool.Set("schemaVersion", descriptor.schema_version);
  tool.Set("resultVersion", descriptor.result_version);
  return tool;
}

// static
base::ListValue MahoBrowserToolRegistry::SerializePublicMcpCapabilities(
    bool mail_enabled, bool routines_enabled, bool vault_enabled,
    bool native_input_enabled) {
  base::ListValue tools;
  for (const CapabilityDescriptor *descriptor : GetCapabilitiesForSurface(
           kPublicMcp, mail_enabled, routines_enabled, vault_enabled,
           native_input_enabled)) {
    std::optional<base::DictValue> tool =
        SerializePublicMcpCapability(*descriptor);
    if (tool.has_value()) {
      tools.Append(std::move(*tool));
    }
  }
  return tools;
}

base::DictValue MahoBrowserToolRegistry::SerializeDiagnostics(
    Surface surface,
    bool mail_enabled,
    bool routines_enabled,
    bool vault_enabled,
    bool native_input_enabled) {
  const auto descriptors = GetCapabilitiesForSurface(
      surface, mail_enabled, routines_enabled, vault_enabled,
      native_input_enabled);
  std::vector<std::string_view> sorted_ids;
  std::string canonical_bytes;
  for (const CapabilityDescriptor* descriptor : descriptors) {
    sorted_ids.push_back(descriptor->canonical_id);
    canonical_bytes.append(descriptor->canonical_id)
        .append("\t")
        .append(descriptor->tool_name)
        .append("\t")
        .append(base::NumberToString(descriptor->schema_version))
        .append("\t")
        .append(base::NumberToString(descriptor->result_version))
        .append("\n");
  }
  std::ranges::sort(sorted_ids);
  base::ListValue ids;
  for (std::string_view id : sorted_ids) {
    ids.Append(id);
  }

  base::DictValue gates;
  gates.Set("mailBeta", mail_enabled);
  gates.Set("routines", routines_enabled);
  gates.Set("vault", vault_enabled);
  gates.Set("nativeInput", native_input_enabled);

  const char* surface_name = "publicMcp";
  switch (surface) {
    case kDesktopAgent:
      surface_name = "desktopAgent";
      break;
    case kPublicMcp:
      surface_name = "publicMcp";
      break;
    case kControlPlane:
      surface_name = "controlPlane";
      break;
    case kCliAgent:
      surface_name = "cliAgent";
      break;
    case kCliGeneric:
      surface_name = "cliGeneric";
      break;
  }

  base::DictValue projection;
  projection.Set("count", static_cast<int>(descriptors.size()));
  projection.Set("ids", std::move(ids));
  base::DictValue surfaces;
  surfaces.Set(surface_name, std::move(projection));

  base::DictValue diagnostics;
  diagnostics.Set("catalogVersion", kCatalogVersion);
  diagnostics.Set("catalogHash",
                  base::HexEncode(crypto::SHA256HashString(canonical_bytes)));
  diagnostics.Set("schemaVersion", kSchemaVersion);
  diagnostics.Set("resultVersion", kResultVersion);
  diagnostics.Set("canonicalCount",
                  static_cast<int>(GetCapabilityDescriptors().size()));
  diagnostics.Set("gates", std::move(gates));
  diagnostics.Set("surfaces", std::move(surfaces));
  diagnostics.Set("intentionalExclusions",
                  base::ListValue()
                      .Append("other_surface_capabilities")
                      .Append("sensitive_payloads"));
  return diagnostics;
}

base::ListValue MahoBrowserToolRegistry::SerializeAgentCapabilities(
    bool mail_enabled, bool routines_enabled, bool vault_enabled,
    bool native_input_enabled) {
  base::ListValue tools;
  for (const CapabilityDescriptor *descriptor :
       GetCapabilitiesForSurface(kDesktopAgent, mail_enabled, routines_enabled,
                                 vault_enabled, native_input_enabled)) {
    if (!HasCompletePublicMetadata(*descriptor)) {
      continue;
    }
    base::DictValue policy;
    policy.Set("sensitive", descriptor->sensitivity != Sensitivity::kLow);
    const bool can_auto_approve =
        descriptor->mutability == Mutability::kReadOnly &&
        !descriptor->changes_authority &&
        descriptor->sensitivity == Sensitivity::kLow;
    policy.Set("permission", can_auto_approve ? "auto_approve" : "always_ask");

    base::DictValue tool;
    tool.Set("capabilityId", descriptor->canonical_id);
    tool.Set("name", descriptor->tool_name);
    tool.Set("description", descriptor->description);
    tool.Set("inputSchema", BuildDesktopInputSchema(*descriptor));
    tool.Set("schemaVersion", descriptor->schema_version);
    tool.Set("policy", std::move(policy));
    tools.Append(std::move(tool));
  }
  return tools;
}

base::DictValue MahoBrowserToolRegistry::SerializeExecutionResult(
    const CapabilityDescriptor &descriptor, std::string output_json,
    const ExecutionReceiptContext &context) {
  base::DictValue receipt;
  receipt.Set("capabilityId", descriptor.canonical_id);
  receipt.Set("executionId", context.execution_id);
  receipt.Set("metadata", BuildReceiptMetadata(descriptor, context));

  base::DictValue execution;
  execution.Set("outputJson", std::move(output_json));
  execution.Set("receipt", std::move(receipt));
  return execution;
}

MahoBrowserToolRegistry::ToolSchema::ToolSchema() = default;
MahoBrowserToolRegistry::ToolSchema::ToolSchema(ToolSchema &&) = default;
MahoBrowserToolRegistry::ToolSchema &
MahoBrowserToolRegistry::ToolSchema::operator=(ToolSchema &&) = default;
MahoBrowserToolRegistry::ToolSchema::~ToolSchema() = default;

// static
const std::vector<MahoBrowserToolRegistry::ToolSchema> &
MahoBrowserToolRegistry::GetPhase1ToolSchemas() {
  static const base::NoDestructor<std::vector<ToolSchema>> kTools(
      BuildPhase1ToolSchemas());
  return *kTools;
}

// static
const MahoBrowserToolRegistry::ToolSchema *
MahoBrowserToolRegistry::FindToolSchema(const std::string &tool_name) {
  for (const ToolSchema &schema : GetPhase1ToolSchemas()) {
    if (schema.name == tool_name) {
      return &schema;
    }
  }
  return nullptr;
}

// static
base::ListValue
MahoBrowserToolRegistry::SerializePhase1ToolSchemas(bool enabled) {
  if (!enabled) {
    return base::ListValue();
  }

  base::ListValue tools;
  for (const ToolSchema &schema : GetPhase1ToolSchemas()) {
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
    const std::string &tool_name) {
  return maho::ai::FindBrowserActionContract(tool_name) != nullptr;
}

// static
bool MahoBrowserToolRegistry::IsRoutineEventTriggerSupported() {
  // Event triggers remain disabled until browser notification and inbox heartbeat
  // emitters are wired to the runtime in Wave 2B/3.
  return false;
}

// static
bool MahoBrowserToolRegistry::ExecuteRoutineCreate(
    const base::DictValue& params,
    MahoCore* core,
    std::string* error_out) {
  const std::string* name = params.FindString("name");
  if (!name || name->empty()) {
    if (error_out) {
      *error_out = "Missing or empty routine 'name'";
    }
    return false;
  }
  const std::string* prompt = params.FindString("prompt");
  if (!prompt || prompt->empty()) {
    if (error_out) {
      *error_out = "Missing or empty routine 'prompt'";
    }
    return false;
  }

  // Reject executable or script payloads explicitly.
  if (params.Find("script") || params.Find("code") || params.Find("executable") ||
      params.Find("command")) {
    if (error_out) {
      *error_out = "Script and code payloads are not accepted; use stored natural language prompts only";
    }
    return false;
  }

  const std::string* trigger = params.FindString("trigger");
  if (trigger) {
    if (*trigger != "cron" && *trigger != "event") {
      if (error_out) {
        *error_out = "Trigger must be either 'cron' or 'event'";
      }
      return false;
    }
    if (*trigger == "event" && !IsRoutineEventTriggerSupported()) {
      if (error_out) {
        *error_out = "Event triggers are deferred and currently disabled until emitters are wired";
      }
      return false;
    }
  }

  if (params.Find("event") && !IsRoutineEventTriggerSupported()) {
    if (error_out) {
      *error_out = "Event triggers are deferred and currently disabled until emitters are wired";
    }
    return false;
  }

  base::DictValue routine_dict;
  routine_dict.Set("name", *name);
  routine_dict.Set("prompt", *prompt);
  if (const std::string* schedule = params.FindString("schedule")) {
    routine_dict.Set("schedule", *schedule);
  }
  if (const std::string* event = params.FindString("event")) {
    routine_dict.Set("trigger", *event);
  }

  std::string json_str;
  if (!base::JSONWriter::Write(routine_dict, &json_str)) {
    if (error_out) {
      *error_out = "Failed to serialize routine payload";
    }
    return false;
  }

  MahoCore* live_core = core ? core : maho::GetCore();
  if (!live_core) {
    if (error_out) {
      *error_out = "MahoCore is not initialized";
    }
    return false;
  }

  int32_t rc = maho_routines_create_custom(live_core, json_str.c_str());
  if (rc != 0) {
    if (error_out) {
      *error_out = "maho_routines_create_custom returned error code " + base::NumberToString(rc);
    }
    return false;
  }
  return true;
}

// static
bool MahoBrowserToolRegistry::ExecuteRoutineUpdate(
    const base::DictValue& params,
    MahoCore* core,
    std::string* error_out) {
  const std::string* id = params.FindString("id");
  if (!id || id->empty()) {
    if (error_out) {
      *error_out = "Missing or empty routine 'id'";
    }
    return false;
  }

  // Reject executable or script payloads explicitly.
  if (params.Find("script") || params.Find("code") || params.Find("executable") ||
      params.Find("command")) {
    if (error_out) {
      *error_out = "Script and code payloads are not accepted; use stored natural language prompts only";
    }
    return false;
  }

  const std::string* trigger = params.FindString("trigger");
  if (trigger) {
    if (*trigger != "cron" && *trigger != "event") {
      if (error_out) {
        *error_out = "Trigger must be either 'cron' or 'event'";
      }
      return false;
    }
  }

  const std::string* schedule = params.FindString("schedule");
  const std::string* event = params.FindString("event");
  if (schedule && trigger && *trigger == "event") {
    if (error_out) {
      *error_out = "'schedule' cannot be combined with an 'event' trigger";
    }
    return false;
  }
  if (event && trigger && *trigger == "cron") {
    if (error_out) {
      *error_out = "'event' cannot be combined with a 'cron' trigger";
    }
    return false;
  }

  if ((trigger && *trigger == "event") || event) {
    if (!IsRoutineEventTriggerSupported()) {
      if (error_out) {
        *error_out =
            "Event triggers are deferred and currently disabled until emitters are wired";
      }
      return false;
    }
  }

  if (trigger && *trigger == "cron" && !schedule) {
    if (error_out) {
      *error_out = "A 'cron' trigger requires a 'schedule' expression";
    }
    return false;
  }
  if (trigger && *trigger == "event" && !event) {
    if (error_out) {
      *error_out = "An 'event' trigger requires an 'event' name";
    }
    return false;
  }

  base::DictValue update_dict;
  if (const std::string* name = params.FindString("name")) {
    if (name->empty()) {
      if (error_out) {
        *error_out = "Routine 'name' cannot be empty";
      }
      return false;
    }
    update_dict.Set("name", *name);
  }
  if (const std::string* prompt = params.FindString("prompt")) {
    if (prompt->empty()) {
      if (error_out) {
        *error_out = "Routine 'prompt' cannot be empty";
      }
      return false;
    }
    update_dict.Set("prompt", *prompt);
  }
  if (schedule) {
    update_dict.Set("schedule", *schedule);
  }
  if (event) {
    update_dict.Set("trigger", *event);
  }
  if (std::optional<bool> enabled = params.FindBool("enabled")) {
    update_dict.Set("enabled", *enabled);
  }
  if (update_dict.empty()) {
    if (error_out) {
      *error_out =
          "No fields to update; provide name, prompt, schedule, event, or enabled";
    }
    return false;
  }

  std::string json_str;
  if (!base::JSONWriter::Write(update_dict, &json_str)) {
    if (error_out) {
      *error_out = "Failed to serialize routine update payload";
    }
    return false;
  }

  MahoCore* live_core = core ? core : maho::GetCore();
  if (!live_core) {
    if (error_out) {
      *error_out = "MahoCore is not initialized";
    }
    return false;
  }

  int32_t rc =
      maho_routines_update_custom(live_core, id->c_str(), json_str.c_str());
  if (rc == -2) {
    if (error_out) {
      *error_out = "Routine not found: " + *id;
    }
    return false;
  }
  if (rc != 0) {
    if (error_out) {
      *error_out =
          "maho_routines_update_custom returned error code " +
          base::NumberToString(rc);
    }
    return false;
  }
  return true;
}

// static
bool MahoBrowserToolRegistry::ExecuteRoutineDelete(
    std::string_view routine_id,
    MahoCore* core,
    std::string* error_out) {
  if (routine_id.empty()) {
    if (error_out) {
      *error_out = "Routine ID cannot be empty";
    }
    return false;
  }

  MahoCore* live_core = core ? core : maho::GetCore();
  if (!live_core) {
    if (error_out) {
      *error_out = "MahoCore is not initialized";
    }
    return false;
  }

  int32_t rc = maho_routines_delete_custom(live_core, std::string(routine_id).c_str());
  if (rc != 0) {
    if (error_out) {
      *error_out = "maho_routines_delete_custom returned error code " + base::NumberToString(rc);
    }
    return false;
  }
  return true;
}

// static
std::string MahoBrowserToolRegistry::ExecuteRoutineHistory(
    std::optional<std::string_view> routine_id,
    uint32_t limit,
    MahoCore* core) {
  MahoCore* live_core = core ? core : maho::GetCore();
  if (!live_core) {
    return "[]";
  }

  char* json = maho_routines_history(
      live_core,
      routine_id ? std::string(*routine_id).c_str() : nullptr,
      limit);
  if (!json) {
    return "[]";
  }
  std::string result(json);
  maho_string_free(json);
  return result;
}

// static
base::DictValue MahoBrowserToolRegistry::ExecuteBrowserRequestHelp(
    const base::DictValue& params,
    std::string* error_out) {
  const std::string* prompt = params.FindString("prompt");
  if (!prompt || prompt->empty()) {
    if (error_out) {
      *error_out = "Missing or empty required argument 'prompt'";
    }
    return base::DictValue();
  }

  std::string request_id;
  const std::string* req_id_param = params.FindString("request_id");
  if (req_id_param && !req_id_param->empty()) {
    request_id = *req_id_param;
  } else {
    request_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  }

  // Note: No adapter reference exists in MahoBrowserToolRegistry.
  // Dispatch returns a waiting status stub as consistent with sibling tools;
  // full adapter FSM glue lands with the executor lane review.
  base::DictValue result;
  result.Set("request_id", std::move(request_id));
  result.Set("status", "waiting");
  return result;
}

