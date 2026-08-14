#include "maho/browser/ai/maho_browser_tool_registry.h"

#include <array>
#include <set>
#include <string>
#include <vector>

#include "base/values.h"
#include "maho/browser/ai/maho_browser_action_contract.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

std::set<std::string> ToolNames(const base::ListValue &tools) {
  std::set<std::string> names;
  for (const base::Value &tool : tools) {
    const std::string *name = tool.GetDict().FindString("name");
    if (name) {
      names.insert(*name);
    }
  }
  return names;
}

size_t VaultToolCount(const base::ListValue &tools) {
  size_t count = 0;
  for (const base::Value &tool : tools) {
    const std::string *name = tool.GetDict().FindString("name");
    if (name && name->rfind("vault_", 0) == 0) {
      ++count;
    }
  }
  return count;
}

const base::DictValue *FindSerializedTool(const base::ListValue &tools,
                                          const std::string &wanted_name) {
  for (const base::Value &tool : tools) {
    const base::DictValue &dict = tool.GetDict();
    const std::string *name = dict.FindString("name");
    if (name && *name == wanted_name) {
      return &dict;
    }
  }
  return nullptr;
}

std::set<std::string> StringSet(const base::ListValue &values) {
  std::set<std::string> result;
  for (const base::Value &value : values) {
    if (value.is_string()) {
      result.insert(value.GetString());
    }
  }
  return result;
}

std::set<std::string> PropertyNames(const base::DictValue &schema) {
  std::set<std::string> result;
  const base::DictValue *properties = schema.FindDict("properties");
  if (!properties) {
    return result;
  }
  for (const auto [name, value] : *properties) {
    result.insert(name);
  }
  return result;
}

const base::DictValue &PublicSchema(const base::ListValue &tools,
                                    std::string_view name) {
  const base::DictValue *tool = FindSerializedTool(tools, std::string(name));
  CHECK(tool);
  const base::DictValue *schema = tool->FindDict("inputSchema");
  CHECK(schema);
  return *schema;
}

base::DictValue ControlSchema(std::string_view name) {
  const auto *descriptor = MahoBrowserToolRegistry::FindCapability(name);
  CHECK(descriptor);
  std::optional<base::DictValue> serialized =
      MahoBrowserToolRegistry::SerializeControlPlaneCapability(*descriptor);
  CHECK(serialized);
  const base::DictValue *schema = serialized->FindDict("inputSchema");
  CHECK(schema);
  return schema->Clone();
}

TEST(MahoBrowserToolRegistryTest, CharacterizesCurrentDesktopProjection) {
  const std::set<std::string> expected = {
      "browser_accessibility_snapshot",
      "browser_click",
      "browser_hover",
      "browser_key_press",
      "browser_navigate",
      "browser_scroll",
      "browser_select",
      "browser_type",
      "browser_wait_for_navigation",
      "extract_structured_page_context",
      "get_active_tab",
      "get_selected_text",
      "mail_get_email",
      "mail_list_accounts",
      "mail_list_emails",
      "mail_list_folders",
      "mail_list_thread",
      "mail_save_draft",
      "mail_search_emails",
      "mail_send",
      "mail_update_draft",
      "page_get_attribute",
      "page_get_text",
      "page_query_selector",
      "page_wait_for_selector",
      "read_current_page",
      "search_in_page",
  };

  base::ListValue tools = MahoBrowserToolRegistry::SerializePhase1ToolSchemas();
  EXPECT_EQ(tools.size(), 27u);
  EXPECT_EQ(ToolNames(tools), expected);
  EXPECT_EQ(maho::ai::kMahoBrowserActionContractCount, 13u);

  size_t action_contract_entries = 0;
  for (const MahoBrowserToolRegistry::ToolSchema &schema :
       MahoBrowserToolRegistry::GetPhase1ToolSchemas()) {
    if (schema.browser_action_contract) {
      ++action_contract_entries;
    }
  }
  EXPECT_EQ(action_contract_entries, 13u);
}

TEST(MahoBrowserToolRegistryTest,
     AdvertisesBrowserActionContractsAndNoVaultTools) {
  base::ListValue tools = MahoBrowserToolRegistry::SerializePhase1ToolSchemas();
  std::set<std::string> names = ToolNames(tools);

  EXPECT_EQ(VaultToolCount(tools), 0u);
  EXPECT_EQ(names.count("read_current_page"), 1u);
  EXPECT_EQ(names.count("get_selected_text"), 1u);
  EXPECT_EQ(names.count("get_active_tab"), 1u);
  EXPECT_EQ(names.count("search_in_page"), 1u);
  EXPECT_EQ(names.count("extract_structured_page_context"), 1u);
  EXPECT_EQ(names.count("mail_list_accounts"), 1u);
  EXPECT_EQ(names.count("mail_list_folders"), 1u);
  EXPECT_EQ(names.count("mail_list_emails"), 1u);
  EXPECT_EQ(names.count("mail_get_email"), 1u);
  EXPECT_EQ(names.count("mail_search_emails"), 1u);
  EXPECT_EQ(names.count("mail_list_thread"), 1u);
  EXPECT_EQ(names.count("browser_click"), 1u);
  EXPECT_EQ(names.count("browser_navigate"), 1u);
  EXPECT_EQ(names.count("browser_accessibility_snapshot"), 1u);
}

TEST(MahoBrowserToolRegistryTest, AdvertisesApprovalGatedMailWriteTools) {
  base::ListValue tools = MahoBrowserToolRegistry::SerializePhase1ToolSchemas();
  std::set<std::string> names = ToolNames(tools);

  EXPECT_EQ(names.count("mail_send"), 1u);
  EXPECT_EQ(names.count("mail_save_draft"), 1u);
  EXPECT_EQ(names.count("mail_update_draft"), 1u);

  const base::DictValue *send = FindSerializedTool(tools, "mail_send");
  ASSERT_TRUE(send);
  const base::DictValue *send_schema = send->FindDict("input_schema");
  ASSERT_TRUE(send_schema);
  const base::ListValue *send_required = send_schema->FindList("required");
  ASSERT_TRUE(send_required);
  std::set<std::string> send_required_names;
  for (const base::Value &value : *send_required) {
    if (value.is_string()) {
      send_required_names.insert(value.GetString());
    }
  }
  EXPECT_EQ(send_required_names.count("account_id"), 1u);
  EXPECT_EQ(send_required_names.count("to"), 1u);
  EXPECT_EQ(send_required_names.count("subject"), 1u);

  const base::DictValue *update =
      FindSerializedTool(tools, "mail_update_draft");
  ASSERT_TRUE(update);
  const base::DictValue *update_schema = update->FindDict("input_schema");
  ASSERT_TRUE(update_schema);
  const base::ListValue *update_required = update_schema->FindList("required");
  ASSERT_TRUE(update_required);
  bool requires_draft_id = false;
  for (const base::Value &value : *update_required) {
    if (value.is_string() && value.GetString() == "draft_id") {
      requires_draft_id = true;
    }
  }
  EXPECT_TRUE(requires_draft_id);
}

TEST(MahoBrowserToolRegistryTest,
     SerializedBrowserActionsExposeCanonicalContracts) {
  base::ListValue tools = MahoBrowserToolRegistry::SerializePhase1ToolSchemas();
  std::set<std::string> names = ToolNames(tools);

  EXPECT_EQ(VaultToolCount(tools), 0u);
  for (const maho::ai::BrowserActionContract &contract :
       maho::ai::kMahoBrowserActionContracts) {
    EXPECT_EQ(names.count(std::string(contract.tool_name)), 1u)
        << contract.tool_name;
  }

  const base::DictValue *click = FindSerializedTool(tools, "browser_click");
  ASSERT_TRUE(click);
  const base::DictValue *click_metadata = click->FindDict("approval_metadata");
  ASSERT_TRUE(click_metadata);
  EXPECT_EQ(*click_metadata->FindString("kind"), "action");
  EXPECT_EQ(*click_metadata->FindString("sensitivity"), "sensitive");
  EXPECT_TRUE(click_metadata->FindBool("requires_approval").value());
  EXPECT_TRUE(click_metadata->FindBool("requires_lease").value());
  EXPECT_EQ(*click_metadata->FindString("empty_allowlist_policy"),
            "fail_closed");

  const base::DictValue *hover = FindSerializedTool(tools, "browser_hover");
  ASSERT_TRUE(hover);
  const base::DictValue *hover_metadata = hover->FindDict("approval_metadata");
  ASSERT_TRUE(hover_metadata);
  EXPECT_TRUE(hover_metadata->FindBool("requires_approval").value());
  EXPECT_FALSE(hover_metadata->FindBool("requires_lease").value());
}

TEST(MahoBrowserToolRegistryTest, BrowserMcpProjectionIsTheExactExistingSet) {
  const std::set<std::string> expected = {
      "browser_accessibility_snapshot",
      "browser_bookmark_create",
      "browser_bookmarks_search",
      "browser_click",
      "browser_console_messages",
      "browser_get_blocked_domains",
      "browser_history_search",
      "browser_hover",
      "browser_key_press",
      "browser_navigate",
      "browser_network_get_har",
      "browser_network_start_capture",
      "browser_network_stop_capture",
      "browser_page_content",
      "browser_page_context",
      "browser_page_text",
      "browser_routines_list",
      "browser_routines_run",
      "browser_same_origin_fetch",
      "browser_screenshot_element",
      "browser_screenshot_full",
      "browser_scroll",
      "browser_search_in_page",
      "browser_select",
      "browser_set_blocked_domains",
      "browser_set_viewport_size",
      "browser_tab_close",
      "browser_tab_get",
      "browser_tab_list",
      "browser_tab_new",
      "browser_tab_switch",
      "browser_type",
      "browser_wait_for_navigation",
      "mail_extract_otp",
      "mail_get_email",
      "mail_list_accounts",
      "mail_list_emails",
      "mail_list_folders",
      "mail_list_thread",
      "mail_flag",
      "mail_queue_email",
      "mail_save_draft",
      "mail_search_emails",
      "mail_send",
      "mail_update_draft",
      "page_get_attribute",
      "page_get_text",
      "page_query_selector",
      "page_wait_for_selector",
  };

  std::set<std::string> actual;
  const auto capabilities = MahoBrowserToolRegistry::GetCapabilitiesForSurface(
      MahoBrowserToolRegistry::kBrowserMcp);
  for (const auto *descriptor : capabilities) {
    EXPECT_TRUE(actual.insert(std::string(descriptor->tool_name)).second)
        << descriptor->tool_name;
  }
  EXPECT_EQ(capabilities.size(), 49u);
  EXPECT_EQ(actual, expected);
}

TEST(MahoBrowserToolRegistryTest, PublicAndControlSchemasAreClosed) {
  base::ListValue public_tools =
      MahoBrowserToolRegistry::SerializePublicMcpCapabilities();
  ASSERT_EQ(public_tools.size(), 49u);

  auto expect_closed_schema = [](const base::DictValue &tool) {
    const std::string *name = tool.FindString("name");
    ASSERT_TRUE(name);
    const base::DictValue *schema = tool.FindDict("inputSchema");
    ASSERT_TRUE(schema) << *name;
    ASSERT_TRUE(schema->FindString("type")) << *name;
    EXPECT_EQ(*schema->FindString("type"), "object") << *name;
    EXPECT_EQ(schema->FindBool("additionalProperties"), false) << *name;
    const base::DictValue *properties = schema->FindDict("properties");
    const base::ListValue *required = schema->FindList("required");
    ASSERT_TRUE(properties) << *name;
    ASSERT_TRUE(required) << *name;
    for (const base::Value &required_key : *required) {
      ASSERT_TRUE(required_key.is_string()) << *name;
      EXPECT_TRUE(properties->contains(required_key.GetString()))
          << *name << ": " << required_key.GetString();
    }
  };

  for (const base::Value &tool : public_tools) {
    expect_closed_schema(tool.GetDict());
  }
  for (const auto *descriptor :
       MahoBrowserToolRegistry::GetCapabilitiesForSurface(
           MahoBrowserToolRegistry::kControlPlane)) {
    std::optional<base::DictValue> tool =
        MahoBrowserToolRegistry::SerializeControlPlaneCapability(*descriptor);
    ASSERT_TRUE(tool) << descriptor->tool_name;
    expect_closed_schema(*tool);
  }
}

TEST(MahoBrowserToolRegistryTest, CatalogSchemasMatchRuntimeArguments) {
  const base::ListValue tools =
      MahoBrowserToolRegistry::SerializePublicMcpCapabilities();

  const base::DictValue &tab_get = PublicSchema(tools, "browser_tab_get");
  EXPECT_EQ(PropertyNames(tab_get), std::set<std::string>({"tab_id"}));
  EXPECT_EQ(StringSet(*tab_get.FindList("required")),
            std::set<std::string>({"tab_id"}));

  const base::DictValue &set_blocked =
      PublicSchema(tools, "browser_set_blocked_domains");
  EXPECT_EQ(PropertyNames(set_blocked), std::set<std::string>({"domains"}));
  EXPECT_EQ(StringSet(*set_blocked.FindList("required")),
            std::set<std::string>({"domains"}));
  EXPECT_EQ(PublicSchema(tools, "browser_get_blocked_domains")
                .FindDict("properties")
                ->size(),
            0u);

  const base::DictValue &routine_run =
      PublicSchema(tools, "browser_routines_run");
  EXPECT_EQ(PropertyNames(routine_run), std::set<std::string>({"id"}));
  EXPECT_EQ(StringSet(*routine_run.FindList("required")),
            std::set<std::string>({"id"}));

  const base::DictValue &element =
      PublicSchema(tools, "browser_screenshot_element");
  EXPECT_EQ(PropertyNames(element), std::set<std::string>({"ref", "tab_id"}));
  EXPECT_EQ(StringSet(*element.FindList("required")),
            std::set<std::string>({"ref"}));

  const base::DictValue &viewport =
      PublicSchema(tools, "browser_set_viewport_size");
  EXPECT_EQ(PropertyNames(viewport),
            std::set<std::string>({"height_px", "tab_id", "width_px"}));
  EXPECT_EQ(StringSet(*viewport.FindList("required")),
            std::set<std::string>({"height_px", "width_px"}));
  EXPECT_EQ(
      viewport.FindDictByDottedPath("properties.width_px")->FindInt("minimum"),
      100);
  EXPECT_EQ(
      viewport.FindDictByDottedPath("properties.width_px")->FindInt("maximum"),
      4096);

  const base::DictValue &mail_search =
      PublicSchema(tools, "mail_search_emails");
  EXPECT_EQ(PropertyNames(mail_search),
            std::set<std::string>(
                {"account_id", "folder_id", "limit", "offset", "query"}));
  EXPECT_EQ(StringSet(*mail_search.FindList("required")),
            std::set<std::string>({"query"}));

  const base::DictValue &mail_send = PublicSchema(tools, "mail_send");
  EXPECT_EQ(PropertyNames(mail_send), std::set<std::string>({"request_json"}));
  EXPECT_EQ(StringSet(*mail_send.FindList("required")),
            std::set<std::string>({"request_json"}));

  base::DictValue vault =
      ControlSchema("vault_list_credentials_for_active_page");
  EXPECT_EQ(PropertyNames(vault), std::set<std::string>({"tab_id"}));
  EXPECT_TRUE(vault.FindList("required")->empty());

  base::DictValue artifact_list = ControlSchema("artifact.list");
  EXPECT_EQ(PropertyNames(artifact_list),
            std::set<std::string>({"session_id"}));
  EXPECT_TRUE(artifact_list.FindList("required")->empty());

  base::DictValue artifact_export = ControlSchema("artifact.export");
  EXPECT_EQ(PropertyNames(artifact_export),
            std::set<std::string>({"artifact_id", "destination"}));
  EXPECT_EQ(StringSet(*artifact_export.FindList("required")),
            std::set<std::string>({"artifact_id", "destination"}));

  base::DictValue lease = ControlSchema("browser_acquire_lease");
  EXPECT_EQ(PropertyNames(lease),
            std::set<std::string>({"tab_id", "ttl_seconds"}));
  EXPECT_EQ(StringSet(*lease.FindList("required")),
            std::set<std::string>({"tab_id"}));

  base::DictValue oauth = ControlSchema("mail_complete_oauth");
  EXPECT_EQ(PropertyNames(oauth), std::set<std::string>({"code", "state"}));
  EXPECT_EQ(StringSet(*oauth.FindList("required")),
            std::set<std::string>({"code", "state"}));

  base::DictValue vault_fill = ControlSchema("vault_fill_credential");
  EXPECT_EQ(PropertyNames(vault_fill),
            std::set<std::string>({"grant_handle", "ref", "tab_id"}));
  EXPECT_EQ(StringSet(*vault_fill.FindList("required")),
            std::set<std::string>({"grant_handle", "ref"}));
}

TEST(MahoBrowserToolRegistryTest, CanonicalDescriptorsHaveCompletePolicy) {
  const auto &descriptors = MahoBrowserToolRegistry::GetCapabilityDescriptors();
  EXPECT_EQ(descriptors.size(), 75u);
  EXPECT_TRUE(
      MahoBrowserToolRegistry::ValidateCapabilityDescriptors(descriptors));

  std::set<std::string_view> ids;
  std::set<std::string_view> names;
  for (const auto &descriptor : descriptors) {
    EXPECT_TRUE(ids.insert(descriptor.canonical_id).second);
    EXPECT_TRUE(names.insert(descriptor.tool_name).second);
    EXPECT_EQ(descriptor.schema_version,
              MahoBrowserToolRegistry::kSchemaVersion);
    EXPECT_EQ(descriptor.result_version,
              MahoBrowserToolRegistry::kResultVersion);
    EXPECT_NE(descriptor.category, MahoBrowserToolRegistry::Category::kUnknown);
    EXPECT_NE(descriptor.mutability,
              MahoBrowserToolRegistry::Mutability::kUnknown);
    EXPECT_NE(descriptor.required_broker,
              MahoBrowserToolRegistry::Broker::kUnknown);
    EXPECT_NE(descriptor.required_boundary,
              MahoBrowserToolRegistry::Boundary::kUnknown);
    EXPECT_NE(descriptor.sensitivity,
              MahoBrowserToolRegistry::Sensitivity::kUnknown);
    EXPECT_NE(descriptor.feature_gate,
              MahoBrowserToolRegistry::FeatureGate::kUnknown);
    EXPECT_NE(descriptor.surfaces, 0u);
    EXPECT_EQ(descriptor.missing_policy,
              MahoBrowserToolRegistry::MissingPolicy::kFailClosed);
  }
}

TEST(MahoBrowserToolRegistryTest, ActionContractsRemainThePolicySource) {
  for (const maho::ai::BrowserActionContract &contract :
       maho::ai::kMahoBrowserActionContracts) {
    const auto *descriptor =
        MahoBrowserToolRegistry::FindCapability(contract.tool_name);
    ASSERT_TRUE(descriptor) << contract.tool_name;
    EXPECT_EQ(descriptor->browser_action_contract, &contract);
    EXPECT_EQ(descriptor->changes_authority, contract.changes_authority);
    EXPECT_EQ(descriptor->mutability ==
                  MahoBrowserToolRegistry::Mutability::kReadOnly,
              contract.kind == maho::ai::BrowserActionKind::kRead);
    EXPECT_EQ(descriptor->missing_policy,
              MahoBrowserToolRegistry::MissingPolicy::kFailClosed);
  }
}

TEST(MahoBrowserToolRegistryTest, FeatureGatesFilterSurfaceProjection) {
  const auto all = MahoBrowserToolRegistry::GetCapabilitiesForSurface(
      MahoBrowserToolRegistry::kBrowserMcp);
  const auto gated = MahoBrowserToolRegistry::GetCapabilitiesForSurface(
      MahoBrowserToolRegistry::kBrowserMcp, false, false, false);
  EXPECT_EQ(all.size(), 49u);
  EXPECT_EQ(gated.size(), 35u);
  for (const auto *descriptor : gated) {
    EXPECT_EQ(descriptor->feature_gate,
              MahoBrowserToolRegistry::FeatureGate::kAlways);
  }
}

TEST(MahoBrowserToolRegistryTest,
     AgentProjectionNamesResolveToTheirCanonicalDescriptors) {
  const base::ListValue tools =
      MahoBrowserToolRegistry::SerializeAgentCapabilities();
  ASSERT_FALSE(tools.empty());
  for (const base::Value& tool : tools) {
    const base::DictValue& serialized = tool.GetDict();
    const std::string* name = serialized.FindString("name");
    const std::string* capability_id = serialized.FindString("capabilityId");
    ASSERT_TRUE(name);
    ASSERT_TRUE(capability_id);
    const auto* descriptor =
        MahoBrowserToolRegistry::FindCapability(*name);
    ASSERT_TRUE(descriptor);
    EXPECT_EQ(descriptor->canonical_id, *capability_id);
  }
}

TEST(MahoBrowserToolRegistryTest,
     ExecutionResultKeepsIntegerTargetAndCanonicalReceiptMetadata) {
  const auto capabilities =
      MahoBrowserToolRegistry::GetCapabilitiesForSurface(
          MahoBrowserToolRegistry::kBrowserMcp);
  ASSERT_FALSE(capabilities.empty());

  MahoBrowserToolRegistry::ExecutionReceiptContext context;
  context.execution_id = "receipt-1";
  context.controller_id = "session-1";
  context.controller_name = "Maho CLI";
  context.controller_type = "automation";
  context.control_plane = "mcp";
  context.target_tab_id = 7;
  context.target_origin = "https://example.test";
  context.approval = "not_requested";
  context.outcome_status = "completed";
  context.outcome_code = "completed";
  context.started_at_seconds = 10;
  context.completed_at_seconds = 11;
  base::DictValue result = MahoBrowserToolRegistry::SerializeExecutionResult(
      *capabilities.front(), R"({"ok":true})", context);

  EXPECT_EQ(*result.FindString("outputJson"), R"({"ok":true})");
  const base::DictValue* receipt = result.FindDict("receipt");
  ASSERT_TRUE(receipt);
  EXPECT_EQ(*receipt->FindString("executionId"), "receipt-1");
  const base::DictValue* metadata = receipt->FindDict("metadata");
  ASSERT_TRUE(metadata);
  const base::DictValue* target = metadata->FindDict("target");
  ASSERT_TRUE(target);
  EXPECT_EQ(*target->FindInt("tabId"), 7);
  EXPECT_EQ(*metadata->FindString("approval"), "not_requested");
  EXPECT_EQ(*metadata->FindDict("outcome")->FindString("status"),
            "completed");
}

TEST(MahoBrowserToolRegistryTest, ControlPlaneProjectionExcludesPublicTools) {
  const auto controls = MahoBrowserToolRegistry::GetCapabilitiesForSurface(
      MahoBrowserToolRegistry::kControlPlane);
  ASSERT_EQ(controls.size(), 21u);
  const std::set<std::string_view> expected = {
      "browser_acquire_lease",
      "browser_adopt_tab",
      "artifact.export",
      "artifact.list",
      "browser_grant_exact_origin",
      "browser_heartbeat_lease",
      "browser_list_exact_origins",
      "browser_release_lease",
      "browser_release_tab",
      "browser_revoke_exact_origin",
      "mail_add_account",
      "mail_complete_oauth",
      "mail_delete_account",
      "mail_import_migration_archive",
      "mail_reconnect_account",
      "mail_start_oauth",
      "mail_test_connection",
      "vault_fill_credential",
      "vault_fill_totp",
      "vault_list_credentials_for_active_page",
      "vault_request_credential_use",
  };
  std::set<std::string_view> actual;
  for (const auto *descriptor : controls) {
    actual.insert(descriptor->tool_name);
    EXPECT_FALSE(MahoBrowserToolRegistry::HasSurface(
        *descriptor, MahoBrowserToolRegistry::kPublicMcp));
  }
  EXPECT_EQ(actual, expected);
}

TEST(MahoBrowserToolRegistryTest,
     DiagnosticsExposeOnlyRequestedPublicSurfaceProjection) {
  base::DictValue diagnostics = MahoBrowserToolRegistry::SerializeDiagnostics(
      MahoBrowserToolRegistry::kPublicMcp, false, false, false);
  EXPECT_EQ(diagnostics.FindInt("catalogVersion"), 1);
  EXPECT_EQ(diagnostics.FindInt("schemaVersion"), 1);
  EXPECT_EQ(diagnostics.FindInt("resultVersion"), 1);
  EXPECT_EQ(diagnostics.FindInt("canonicalCount"), 75);
  ASSERT_TRUE(diagnostics.FindString("catalogHash"));
  EXPECT_EQ(diagnostics.FindString("catalogHash")->size(), 64u);

  const base::DictValue *gates = diagnostics.FindDict("gates");
  ASSERT_TRUE(gates);
  EXPECT_EQ(gates->FindBool("mailBeta"), false);
  EXPECT_EQ(gates->FindBool("routines"), false);
  EXPECT_EQ(gates->FindBool("vault"), false);

  const base::DictValue *surfaces = diagnostics.FindDict("surfaces");
  ASSERT_TRUE(surfaces);
  const base::DictValue* public_mcp = surfaces->FindDict("publicMcp");
  ASSERT_TRUE(public_mcp);
  EXPECT_EQ(public_mcp->FindInt("count"), 35);
  EXPECT_EQ(public_mcp->FindList("ids")->size(), 35u);
  EXPECT_FALSE(surfaces->Find("desktopAgent"));
  EXPECT_FALSE(surfaces->Find("cliAgent"));
  EXPECT_FALSE(surfaces->Find("cliGeneric"));
  EXPECT_FALSE(surfaces->Find("controlPlane"));

  const std::string serialized = diagnostics.DebugString();
  EXPECT_EQ(serialized.find("maho/control"), std::string::npos);
  EXPECT_EQ(serialized.find("credential_value"), std::string::npos);
  EXPECT_EQ(serialized.find("raw_arguments"), std::string::npos);
  EXPECT_EQ(serialized.find("control.origin.grant"), std::string::npos);
}

TEST(MahoBrowserToolRegistryTest, UnknownAndIncompleteMetadataFailClosed) {
  EXPECT_EQ(MahoBrowserToolRegistry::FindCapability("unknown_browser_tool"),
            nullptr);

  MahoBrowserToolRegistry::CapabilityDescriptor valid = {
      "probe.valid",
      "probe_valid",
      1,
      1,
      MahoBrowserToolRegistry::Category::kPage,
      MahoBrowserToolRegistry::Mutability::kReadOnly,
      false,
      MahoBrowserToolRegistry::Broker::kBrowser,
      MahoBrowserToolRegistry::Boundary::kTab,
      MahoBrowserToolRegistry::Sensitivity::kLow,
      MahoBrowserToolRegistry::FeatureGate::kAlways,
      MahoBrowserToolRegistry::kBrowserMcp,
      MahoBrowserToolRegistry::MissingPolicy::kFailClosed,
      "probe",
      nullptr};
  EXPECT_TRUE(MahoBrowserToolRegistry::ValidateCapabilityDescriptors(
      base::span_from_ref(valid)));

  auto missing_mutability = valid;
  missing_mutability.mutability = MahoBrowserToolRegistry::Mutability::kUnknown;
  EXPECT_FALSE(MahoBrowserToolRegistry::ValidateCapabilityDescriptors(
      base::span_from_ref(missing_mutability)));

  auto missing_authority_policy = valid;
  missing_authority_policy.missing_policy =
      MahoBrowserToolRegistry::MissingPolicy::kUnknown;
  EXPECT_FALSE(MahoBrowserToolRegistry::ValidateCapabilityDescriptors(
      base::span_from_ref(missing_authority_policy)));

  auto invalid_surface = valid;
  invalid_surface.surfaces = 1u << 20;
  EXPECT_FALSE(MahoBrowserToolRegistry::ValidateCapabilityDescriptors(
      base::span_from_ref(invalid_surface)));

  auto duplicate = valid;
  duplicate.tool_name = "probe_duplicate";
  const std::array duplicate_ids = {valid, duplicate};
  EXPECT_FALSE(
      MahoBrowserToolRegistry::ValidateCapabilityDescriptors(duplicate_ids));
}

TEST(MahoBrowserToolRegistryTest, LocalAgentToolsNeverLeakIntoBrowserCatalog) {
  const std::set<std::string_view> forbidden = {"fs_read", "fs_write",
                                                "shell_exec", "web_fetch"};
  for (const auto &descriptor :
       MahoBrowserToolRegistry::GetCapabilityDescriptors()) {
    EXPECT_EQ(forbidden.count(descriptor.tool_name), 0u)
        << descriptor.tool_name;
  }
}

TEST(MahoBrowserToolRegistryTest, DisabledRegistryReturnsZeroSchemas) {
  base::ListValue tools =
      MahoBrowserToolRegistry::SerializePhase1ToolSchemas(false);

  EXPECT_TRUE(tools.empty());
}

} // namespace
