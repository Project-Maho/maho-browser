#include "maho/browser/ai/maho_browser_tool_registry.h"

#include <set>
#include <string>

#include "base/test/scoped_feature_list.h"
#include "base/values.h"
#include "maho/browser/ai/maho_browser_action_contract.h"
#include "maho/browser/maho_features.h"
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
     DefaultFlagAdvertisesReadOnlyToolsAndNoVaultTools) {
  base::test::ScopedFeatureList scoped_features;
  scoped_features.InitAndDisableFeature(maho::features::kMahoAgentBrowserActions);

  base::ListValue tools = MahoBrowserToolRegistry::SerializePhase1ToolSchemas();
  std::set<std::string> names = ToolNames(tools);

  EXPECT_EQ(VaultToolCount(tools), 0u);
  EXPECT_EQ(names.count("read_current_page"), 1u);
  EXPECT_EQ(names.count("get_selected_text"), 1u);
  EXPECT_EQ(names.count("get_active_tab"), 1u);
  EXPECT_EQ(names.count("search_in_page"), 1u);
  EXPECT_EQ(names.count("extract_structured_page_context"), 1u);
  EXPECT_EQ(names.count("browser_click"), 0u);
  EXPECT_EQ(names.count("browser_navigate"), 0u);
  EXPECT_EQ(names.count("browser_accessibility_snapshot"), 0u);
}

TEST(MahoBrowserToolRegistryTest,
     EnabledFlagAdvertisesCanonicalBrowserActionContracts) {
  base::test::ScopedFeatureList scoped_features;
  scoped_features.InitAndEnableFeature(maho::features::kMahoAgentBrowserActions);

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
  base::test::ScopedFeatureList scoped_features;
  scoped_features.InitAndEnableFeature(maho::features::kMahoAgentBrowserActions);

  base::ListValue tools =
      MahoBrowserToolRegistry::SerializePhase1ToolSchemas(false);

  EXPECT_TRUE(tools.empty());
}

}
