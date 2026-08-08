#include "maho/browser/ai/maho_browser_tool_registry.h"

#include <set>
#include <string>

#include "base/values.h"
#include "maho/browser/ai/maho_browser_action_contract.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

std::set<std::string> ToolNames(const base::ListValue& tools) {
  std::set<std::string> names;
  for (const base::Value& tool : tools) {
    const std::string* name = tool.GetDict().FindString("name");
    if (name) {
      names.insert(*name);
    }
  }
  return names;
}

size_t VaultToolCount(const base::ListValue& tools) {
  size_t count = 0;
  for (const base::Value& tool : tools) {
    const std::string* name = tool.GetDict().FindString("name");
    if (name && name->rfind("vault_", 0) == 0) {
      ++count;
    }
  }
  return count;
}

const base::DictValue* FindSerializedTool(const base::ListValue& tools,
                                          const std::string& wanted_name) {
  for (const base::Value& tool : tools) {
    const base::DictValue& dict = tool.GetDict();
    const std::string* name = dict.FindString("name");
    if (name && *name == wanted_name) {
      return &dict;
    }
  }
  return nullptr;
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

  const base::DictValue* send = FindSerializedTool(tools, "mail_send");
  ASSERT_TRUE(send);
  const base::DictValue* send_schema = send->FindDict("input_schema");
  ASSERT_TRUE(send_schema);
  const base::ListValue* send_required = send_schema->FindList("required");
  ASSERT_TRUE(send_required);
  std::set<std::string> send_required_names;
  for (const base::Value& value : *send_required) {
    if (value.is_string()) {
      send_required_names.insert(value.GetString());
    }
  }
  EXPECT_EQ(send_required_names.count("account_id"), 1u);
  EXPECT_EQ(send_required_names.count("to"), 1u);
  EXPECT_EQ(send_required_names.count("subject"), 1u);

  const base::DictValue* update =
      FindSerializedTool(tools, "mail_update_draft");
  ASSERT_TRUE(update);
  const base::DictValue* update_schema = update->FindDict("input_schema");
  ASSERT_TRUE(update_schema);
  const base::ListValue* update_required = update_schema->FindList("required");
  ASSERT_TRUE(update_required);
  bool requires_draft_id = false;
  for (const base::Value& value : *update_required) {
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
  for (const maho::ai::BrowserActionContract& contract :
       maho::ai::kMahoBrowserActionContracts) {
    EXPECT_EQ(names.count(std::string(contract.tool_name)), 1u)
        << contract.tool_name;
  }

  const base::DictValue* click = FindSerializedTool(tools, "browser_click");
  ASSERT_TRUE(click);
  const base::DictValue* click_metadata = click->FindDict("approval_metadata");
  ASSERT_TRUE(click_metadata);
  EXPECT_EQ(*click_metadata->FindString("kind"), "action");
  EXPECT_EQ(*click_metadata->FindString("sensitivity"), "sensitive");
  EXPECT_TRUE(click_metadata->FindBool("requires_approval").value());
  EXPECT_TRUE(click_metadata->FindBool("requires_lease").value());
  EXPECT_EQ(*click_metadata->FindString("empty_allowlist_policy"),
            "fail_closed");

  const base::DictValue* hover = FindSerializedTool(tools, "browser_hover");
  ASSERT_TRUE(hover);
  const base::DictValue* hover_metadata = hover->FindDict("approval_metadata");
  ASSERT_TRUE(hover_metadata);
  EXPECT_TRUE(hover_metadata->FindBool("requires_approval").value());
  EXPECT_FALSE(hover_metadata->FindBool("requires_lease").value());
}

TEST(MahoBrowserToolRegistryTest, DisabledRegistryReturnsZeroSchemas) {
  base::ListValue tools =
      MahoBrowserToolRegistry::SerializePhase1ToolSchemas(false);

  EXPECT_TRUE(tools.empty());
}

}
