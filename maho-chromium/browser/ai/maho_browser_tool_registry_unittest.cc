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
      "browser_file_upload_select",
      "browser_history_search_desktop",
      "browser_hover",
      "browser_key_press",
      "browser_navigate",
      "browser_routines_create",
      "browser_routines_delete",
      "browser_routines_history",
      "browser_routines_update",
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
      "screenshot_proof",
      "search_in_page",
  };

  base::ListValue tools = MahoBrowserToolRegistry::SerializePhase1ToolSchemas();
  EXPECT_EQ(tools.size(), 40u);
  std::set<std::string> expected_names(expected);
  expected_names.insert("browser_act_and_observe");
  expected_names.insert("browser_locator_click");
  expected_names.insert("browser_locator_type");
  expected_names.insert("browser_tab_list");
  expected_names.insert("browser_tab_new");
  expected_names.insert("browser_tab_switch");
    EXPECT_EQ(ToolNames(tools), expected_names);
  EXPECT_EQ(maho::ai::kMahoBrowserActionContractCount, 14u);

  size_t action_contract_entries = 0;
  for (const MahoBrowserToolRegistry::ToolSchema &schema :
       MahoBrowserToolRegistry::GetPhase1ToolSchemas()) {
    if (schema.browser_action_contract) {
      ++action_contract_entries;
    }
  }
  EXPECT_EQ(action_contract_entries, 14u);
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
      "browser_act_and_observe",
      "browser_bookmark_create",
      "browser_bookmarks_search",
      "browser_click",
      "browser_console_messages",
      "browser_file_upload_select",
      "browser_get_blocked_domains",
      "browser_history_search",
      "browser_hover",
      "browser_key_press",
      "browser_locator_click",
      "browser_locator_type",
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
      "page_accessibility_snapshot_v2",
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
  EXPECT_EQ(capabilities.size(), 54u);
  EXPECT_EQ(actual, expected);
}

TEST(MahoBrowserToolRegistryTest, PublicAndControlSchemasAreClosed) {
  base::ListValue public_tools =
      MahoBrowserToolRegistry::SerializePublicMcpCapabilities();
  ASSERT_EQ(public_tools.size(), 54u);

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

  const base::DictValue& file_upload =
      PublicSchema(tools, "browser_file_upload_select");
  EXPECT_EQ(PropertyNames(file_upload),
            std::set<std::string>({"path", "tab_id"}));
  EXPECT_EQ(StringSet(*file_upload.FindList("required")),
            std::set<std::string>({"path"}));

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
  EXPECT_EQ(descriptors.size(), 86u);
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
  EXPECT_EQ(all.size(), 54u);
  EXPECT_EQ(gated.size(), 40u);
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
  EXPECT_EQ(diagnostics.FindInt("canonicalCount"), 86);
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
  EXPECT_EQ(public_mcp->FindInt("count"), 40);
  EXPECT_EQ(public_mcp->FindList("ids")->size(), 40u);
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

TEST(MahoBrowserToolRegistryTest,
     RoutineSchemasRejectScriptPayloadsAndEnforceTriggerEnum) {
  base::ListValue tools = MahoBrowserToolRegistry::SerializePhase1ToolSchemas();

  // 1. browser_routines_create schema checks
  const base::DictValue* create =
      FindSerializedTool(tools, "browser_routines_create");
  ASSERT_TRUE(create);
  const base::DictValue* create_schema = create->FindDict("input_schema");
  ASSERT_TRUE(create_schema);
  EXPECT_EQ(*create_schema->FindString("type"), "object");
  EXPECT_EQ(create_schema->FindBool("additionalProperties"), false);

  const base::DictValue* create_props = create_schema->FindDict("properties");
  ASSERT_TRUE(create_props);
  EXPECT_TRUE(create_props->contains("name"));
  EXPECT_TRUE(create_props->contains("prompt"));
  EXPECT_TRUE(create_props->contains("trigger"));
  EXPECT_TRUE(create_props->contains("schedule"));
  EXPECT_TRUE(create_props->contains("event"));

  // Scripts / executable code properties must NOT be accepted in schema
  EXPECT_FALSE(create_props->contains("script"));
  EXPECT_FALSE(create_props->contains("code"));
  EXPECT_FALSE(create_props->contains("executable"));
  EXPECT_FALSE(create_props->contains("command"));

  // Trigger property enum MUST contain ONLY "cron" and "event"
  const base::DictValue* trigger_schema = create_props->FindDict("trigger");
  ASSERT_TRUE(trigger_schema);
  EXPECT_EQ(*trigger_schema->FindString("type"), "string");
  const base::ListValue* trigger_enum = trigger_schema->FindList("enum");
  ASSERT_TRUE(trigger_enum);
  EXPECT_EQ(trigger_enum->size(), 2u);
  std::set<std::string> enum_values = StringSet(*trigger_enum);
  EXPECT_EQ(enum_values, std::set<std::string>({"cron", "event"}));

  // Required properties for create
  const base::ListValue* create_required = create_schema->FindList("required");
  ASSERT_TRUE(create_required);
  std::set<std::string> create_req_set = StringSet(*create_required);
  EXPECT_EQ(create_req_set.count("name"), 1u);
  EXPECT_EQ(create_req_set.count("prompt"), 1u);

  // 2. browser_routines_update schema checks
  const base::DictValue* update =
      FindSerializedTool(tools, "browser_routines_update");
  ASSERT_TRUE(update);
  const base::DictValue* update_schema = update->FindDict("input_schema");
  ASSERT_TRUE(update_schema);
  EXPECT_EQ(update_schema->FindBool("additionalProperties"), false);
  const base::DictValue* update_props = update_schema->FindDict("properties");
  ASSERT_TRUE(update_props);
  EXPECT_TRUE(update_props->contains("id"));
  EXPECT_FALSE(update_props->contains("script"));
  EXPECT_FALSE(update_props->contains("code"));

  // 3. browser_routines_delete schema checks
  const base::DictValue* del =
      FindSerializedTool(tools, "browser_routines_delete");
  ASSERT_TRUE(del);
  const base::DictValue* del_schema = del->FindDict("input_schema");
  ASSERT_TRUE(del_schema);
  EXPECT_EQ(del_schema->FindBool("additionalProperties"), false);
  const base::ListValue* del_required = del_schema->FindList("required");
  ASSERT_TRUE(del_required);
  EXPECT_EQ(StringSet(*del_required), std::set<std::string>({"id"}));

  // 4. browser_routines_history schema checks
  const base::DictValue* history =
      FindSerializedTool(tools, "browser_routines_history");
  ASSERT_TRUE(history);
  const base::DictValue* history_schema = history->FindDict("input_schema");
  ASSERT_TRUE(history_schema);
  EXPECT_EQ(history_schema->FindBool("additionalProperties"), false);
  const base::DictValue* history_props = history_schema->FindDict("properties");
  ASSERT_TRUE(history_props);
  EXPECT_TRUE(history_props->contains("routine_id"));
  EXPECT_TRUE(history_props->contains("limit"));
}

TEST(MahoBrowserToolRegistryTest,
     RoutineMutationsMarkedSensitiveAndRequireConfirmation) {
  // Check descriptors directly
  const auto* create_desc =
      MahoBrowserToolRegistry::FindCapability("browser_routines_create");
  ASSERT_TRUE(create_desc);
  EXPECT_EQ(create_desc->mutability,
            MahoBrowserToolRegistry::Mutability::kMutable);
  EXPECT_EQ(create_desc->sensitivity,
            MahoBrowserToolRegistry::Sensitivity::kSensitive);
  EXPECT_TRUE(create_desc->changes_authority);

  const auto* update_desc =
      MahoBrowserToolRegistry::FindCapability("browser_routines_update");
  ASSERT_TRUE(update_desc);
  EXPECT_EQ(update_desc->mutability,
            MahoBrowserToolRegistry::Mutability::kMutable);
  EXPECT_EQ(update_desc->sensitivity,
            MahoBrowserToolRegistry::Sensitivity::kSensitive);
  EXPECT_TRUE(update_desc->changes_authority);

  const auto* delete_desc =
      MahoBrowserToolRegistry::FindCapability("browser_routines_delete");
  ASSERT_TRUE(delete_desc);
  EXPECT_EQ(delete_desc->mutability,
            MahoBrowserToolRegistry::Mutability::kMutable);
  EXPECT_EQ(delete_desc->sensitivity,
            MahoBrowserToolRegistry::Sensitivity::kSensitive);
  EXPECT_TRUE(delete_desc->changes_authority);

  const auto* history_desc =
      MahoBrowserToolRegistry::FindCapability("browser_routines_history");
  ASSERT_TRUE(history_desc);
  EXPECT_EQ(history_desc->mutability,
            MahoBrowserToolRegistry::Mutability::kReadOnly);
  EXPECT_EQ(history_desc->sensitivity,
            MahoBrowserToolRegistry::Sensitivity::kLow);
  EXPECT_FALSE(history_desc->changes_authority);

  // Check serialized agent capabilities policy
  base::ListValue agent_tools =
      MahoBrowserToolRegistry::SerializeAgentCapabilities();

  for (const std::string& mutation_tool :
       {"browser_routines_create", "browser_routines_update",
        "browser_routines_delete"}) {
    const base::DictValue* tool =
        FindSerializedTool(agent_tools, mutation_tool);
    ASSERT_TRUE(tool) << mutation_tool;
    const base::DictValue* policy = tool->FindDict("policy");
    ASSERT_TRUE(policy) << mutation_tool;
    EXPECT_TRUE(policy->FindBool("sensitive").value()) << mutation_tool;
    EXPECT_EQ(*policy->FindString("permission"), "always_ask")
        << mutation_tool;
  }

  const base::DictValue* hist_tool =
      FindSerializedTool(agent_tools, "browser_routines_history");
  ASSERT_TRUE(hist_tool);
  const base::DictValue* hist_policy = hist_tool->FindDict("policy");
  ASSERT_TRUE(hist_policy);
  EXPECT_FALSE(hist_policy->FindBool("sensitive").value());
  EXPECT_EQ(*hist_policy->FindString("permission"), "auto_approve");
}

TEST(MahoBrowserToolRegistryTest,
     RoutineEventTriggerMarkedDisabledUntilEmitterWired) {
  EXPECT_FALSE(MahoBrowserToolRegistry::IsRoutineEventTriggerSupported());

  // Attempting to create a routine with event trigger fails closed
  base::DictValue event_routine;
  event_routine.Set("name", "Event Routine");
  event_routine.Set("prompt", "Take screenshot on startup");
  event_routine.Set("trigger", "event");
  event_routine.Set("event", "on_startup");

  std::string error;
  bool success = MahoBrowserToolRegistry::ExecuteRoutineCreate(
      event_routine, nullptr, &error);
  EXPECT_FALSE(success);
  EXPECT_TRUE(error.find("Event triggers are deferred and currently disabled") !=
              std::string::npos);

  // Attempting to pass script payload fails closed
  base::DictValue script_routine;
  script_routine.Set("name", "Malicious Script");
  script_routine.Set("prompt", "Do something");
  script_routine.Set("script", "console.log('pwn')");

  std::string script_error;
  bool script_success = MahoBrowserToolRegistry::ExecuteRoutineCreate(
      script_routine, nullptr, &script_error);
  EXPECT_FALSE(script_success);
  EXPECT_TRUE(script_error.find("Script and code payloads are not accepted") !=
              std::string::npos);
}

TEST(MahoBrowserToolRegistryTest, RoutineRegistryHandlersBridgeToRoutinesFFI) {
  // Creation fails with null core
  base::DictValue cron_routine;
  cron_routine.Set("name", "Daily Summary");
  cron_routine.Set("prompt", "Summarize my tabs");
  cron_routine.Set("trigger", "cron");
  cron_routine.Set("schedule", "0 9 * * *");

  std::string error;
  bool ok = MahoBrowserToolRegistry::ExecuteRoutineCreate(
      cron_routine, nullptr, &error);
  EXPECT_FALSE(ok);
  EXPECT_EQ(error, "MahoCore is not initialized");

  // Deletion with empty id fails validation
  std::string del_err;
  bool del_ok = MahoBrowserToolRegistry::ExecuteRoutineDelete("", nullptr, &del_err);
  EXPECT_FALSE(del_ok);
  EXPECT_EQ(del_err, "Routine ID cannot be empty");

  // History with null core returns safe empty array JSON
  std::string history_json =
      MahoBrowserToolRegistry::ExecuteRoutineHistory(std::nullopt, 50, nullptr);
  EXPECT_EQ(history_json, "[]");
}

TEST(MahoBrowserToolRegistryTest,
     BrowserHistorySearchDesktopDiscoverableOnDesktopAgentWithoutBrowserMcp) {
  const auto* desc =
      MahoBrowserToolRegistry::FindCapability("browser_history_search_desktop");
  ASSERT_TRUE(desc);
  EXPECT_EQ(desc->canonical_id, "history.search_desktop");
  EXPECT_EQ(desc->category, MahoBrowserToolRegistry::Category::kHistory);
  EXPECT_EQ(desc->mutability, MahoBrowserToolRegistry::Mutability::kReadOnly);
  EXPECT_EQ(desc->sensitivity, MahoBrowserToolRegistry::Sensitivity::kSensitive);
  EXPECT_FALSE(desc->changes_authority);

  // Surface membership: DesktopAgent surface enabled, BrowserMcp disabled
  EXPECT_TRUE(MahoBrowserToolRegistry::HasSurface(
      *desc, MahoBrowserToolRegistry::kDesktopAgent));
  EXPECT_FALSE(MahoBrowserToolRegistry::HasSurface(
      *desc, MahoBrowserToolRegistry::kBrowserMcp));

  // DesktopAgent projection contains it
  const auto desktop_caps =
      MahoBrowserToolRegistry::GetCapabilitiesForSurface(
          MahoBrowserToolRegistry::kDesktopAgent);
  bool found_in_desktop = false;
  for (const auto* cap : desktop_caps) {
    if (cap->tool_name == "browser_history_search_desktop") {
      found_in_desktop = true;
      break;
    }
  }
  EXPECT_TRUE(found_in_desktop);

  // BrowserMcp projection does NOT contain it
  const auto mcp_caps =
      MahoBrowserToolRegistry::GetCapabilitiesForSurface(
          MahoBrowserToolRegistry::kBrowserMcp);
  for (const auto* cap : mcp_caps) {
    EXPECT_NE(cap->tool_name, "browser_history_search_desktop");
  }

  // Serialized desktop agent tools expose it with query and max_results schema
  base::ListValue agent_tools =
      MahoBrowserToolRegistry::SerializeAgentCapabilities();
  const base::DictValue* serialized =
      FindSerializedTool(agent_tools, "browser_history_search_desktop");
  ASSERT_TRUE(serialized);
  EXPECT_EQ(*serialized->FindString("capabilityId"), "history.search_desktop");
  const base::DictValue* schema = serialized->FindDict("inputSchema");
  ASSERT_TRUE(schema);
  EXPECT_EQ(PropertyNames(*schema),
            std::set<std::string>({"max_results", "query"}));
}

TEST(MahoBrowserToolRegistryTest,
     ScreenshotProofMarkedSensitiveAndConfirmationGated) {
  const auto* desc =
      MahoBrowserToolRegistry::FindCapability("screenshot_proof");
  ASSERT_TRUE(desc);
  EXPECT_EQ(desc->canonical_id, "capture.screenshot_proof");
  EXPECT_EQ(desc->category, MahoBrowserToolRegistry::Category::kCapture);
  EXPECT_EQ(desc->mutability, MahoBrowserToolRegistry::Mutability::kReadOnly);
  EXPECT_EQ(desc->sensitivity, MahoBrowserToolRegistry::Sensitivity::kSensitive);
  EXPECT_FALSE(desc->changes_authority);

  // Surface membership: DesktopAgent surface enabled, BrowserMcp disabled
  EXPECT_TRUE(MahoBrowserToolRegistry::HasSurface(
      *desc, MahoBrowserToolRegistry::kDesktopAgent));
  EXPECT_FALSE(MahoBrowserToolRegistry::HasSurface(
      *desc, MahoBrowserToolRegistry::kBrowserMcp));

  // Check serialized agent capability policy confirms sensitivity and always_ask gate
  base::ListValue agent_tools =
      MahoBrowserToolRegistry::SerializeAgentCapabilities();
  const base::DictValue* proof_tool =
      FindSerializedTool(agent_tools, "screenshot_proof");
  ASSERT_TRUE(proof_tool);
  EXPECT_EQ(*proof_tool->FindString("capabilityId"), "capture.screenshot_proof");
  const base::DictValue* policy = proof_tool->FindDict("policy");
  ASSERT_TRUE(policy);
  EXPECT_TRUE(policy->FindBool("sensitive").value());
  EXPECT_EQ(*policy->FindString("permission"), "always_ask");

  const base::DictValue* schema = proof_tool->FindDict("inputSchema");
  ASSERT_TRUE(schema);
  EXPECT_EQ(PropertyNames(*schema), std::set<std::string>({"tab_id"}));
  EXPECT_TRUE(schema->FindList("required")->empty());
}

TEST(MahoBrowserToolRegistryTest,
     PlainBrowserHistorySearchRestrictedToBrowserMcp) {
  // 1. Plain browser_history_search descriptor is BrowserMcp ONLY
  const auto* mcp_desc =
      MahoBrowserToolRegistry::FindCapability("browser_history_search");
  ASSERT_TRUE(mcp_desc);
  EXPECT_EQ(mcp_desc->canonical_id, "history.search");
  EXPECT_TRUE(MahoBrowserToolRegistry::HasSurface(
      *mcp_desc, MahoBrowserToolRegistry::kBrowserMcp));
  EXPECT_FALSE(MahoBrowserToolRegistry::HasSurface(
      *mcp_desc, MahoBrowserToolRegistry::kDesktopAgent));

  // 2. Both-direction surface assertion
  const auto desktop_caps =
      MahoBrowserToolRegistry::GetCapabilitiesForSurface(
          MahoBrowserToolRegistry::kDesktopAgent);
  const auto mcp_caps =
      MahoBrowserToolRegistry::GetCapabilitiesForSurface(
          MahoBrowserToolRegistry::kBrowserMcp);

  std::set<std::string> desktop_names;
  for (const auto* cap : desktop_caps) {
    desktop_names.insert(std::string(cap->tool_name));
  }
  std::set<std::string> mcp_names;
  for (const auto* cap : mcp_caps) {
    mcp_names.insert(std::string(cap->tool_name));
  }

  // DesktopAgent has browser_history_search_desktop but NOT plain browser_history_search
  EXPECT_EQ(desktop_names.count("browser_history_search_desktop"), 1u);
  EXPECT_EQ(desktop_names.count("browser_history_search"), 0u);

  // BrowserMcp has plain browser_history_search but NOT browser_history_search_desktop
  EXPECT_EQ(mcp_names.count("browser_history_search"), 1u);
  EXPECT_EQ(mcp_names.count("browser_history_search_desktop"), 0u);

  // Serialized Phase 1 tools check
  base::ListValue phase1_tools =
      MahoBrowserToolRegistry::SerializePhase1ToolSchemas();
  std::set<std::string> phase1_names = ToolNames(phase1_tools);
  EXPECT_EQ(phase1_names.count("browser_history_search_desktop"), 1u);
  EXPECT_EQ(phase1_names.count("browser_history_search"), 0u);
}

} // namespace
