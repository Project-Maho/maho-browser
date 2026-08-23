// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_page_adapter_registry.h"

#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace maho::ai {
namespace {

struct CatalogEntryForTest {
  std::string id;
  std::string canonical_id;
  std::string tool_name;
  std::string category;
  std::string mutability;
  bool authority_change = false;
  std::string broker;
  std::string boundary;
  std::string sensitivity;
  std::string feature_gate;
  uint32_t surfaces = 0;
  std::string missing_policy;
  std::string description;
};

constexpr uint32_t kDesktopAgentSurface = 1 << 0;
constexpr uint32_t kBrowserMcpSurface = 1 << 1;
constexpr uint32_t kControlPlaneSurface = 1 << 2;

constexpr uint32_t DesktopAgent = kDesktopAgentSurface;
constexpr uint32_t BrowserMcp = kBrowserMcpSurface;
constexpr uint32_t ControlPlane = kControlPlaneSurface;
constexpr uint32_t DesktopAgentAndBrowserMcp =
    kDesktopAgentSurface | kBrowserMcpSurface;

std::vector<CatalogEntryForTest> LoadCatalogEntriesForTest() {
  std::vector<CatalogEntryForTest> entries;

#define MAHO_BROWSER_CAPABILITY(id_val, canonical_id_val, tool_name_val,        \
                                category_val, mutability_val, authority_val,   \
                                broker_val, boundary_val, sensitivity_val,     \
                                gate_val, surfaces_val, missing_val, desc_val) \
  entries.push_back(CatalogEntryForTest{                                       \
      .id = #id_val,                                                           \
      .canonical_id = canonical_id_val,                                        \
      .tool_name = tool_name_val,                                              \
      .category = #category_val,                                               \
      .mutability = #mutability_val,                                           \
      .authority_change = (std::string_view(#authority_val) == "Yes"),         \
      .broker = #broker_val,                                                   \
      .boundary = #boundary_val,                                               \
      .sensitivity = #sensitivity_val,                                        \
      .feature_gate = #gate_val,                                               \
      .surfaces = surfaces_val,                                                \
      .missing_policy = #missing_val,                                          \
      .description = desc_val,                                                 \
  });

#define MAHO_BROWSER_ACTION_CAPABILITY(id_val, canonical_id_val, tool_name_val, \
                                       category_val, broker_val, boundary_val, \
                                       gate_val, surfaces_val, desc_val)       \
  entries.push_back(CatalogEntryForTest{                                       \
      .id = #id_val,                                                           \
      .canonical_id = canonical_id_val,                                        \
      .tool_name = tool_name_val,                                              \
      .category = #category_val,                                               \
      .mutability = "Mutable",                                                 \
      .authority_change = true,                                                \
      .broker = #broker_val,                                                   \
      .boundary = #boundary_val,                                               \
      .sensitivity = "Sensitive",                                              \
      .feature_gate = #gate_val,                                               \
      .surfaces = surfaces_val,                                                \
      .missing_policy = "FailClosed",                                          \
      .description = desc_val,                                                 \
  });

#include "maho/browser/ai/maho_browser_capability_catalog.def"

#undef MAHO_BROWSER_CAPABILITY
#undef MAHO_BROWSER_ACTION_CAPABILITY

  return entries;
}

const CatalogEntryForTest* FindCatalogEntry(
    const std::vector<CatalogEntryForTest>& entries,
    std::string_view tool_name) {
  for (const auto& entry : entries) {
    if (entry.tool_name == tool_name) {
      return &entry;
    }
  }
  return nullptr;
}

}  // namespace
}  // namespace maho::ai

#ifndef MAHO_STANDALONE_TEST
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::ai {

TEST(MahoBrowserCapabilityCatalogPromotionTest,
     PromotedTabCapabilitiesAreAvailableOnBothSurfaces) {
  const auto entries = LoadCatalogEntriesForTest();

  // 1. browser_tab_list promoted to DesktopAgentAndBrowserMcp
  const auto* tab_list = FindCatalogEntry(entries, "browser_tab_list");
  ASSERT_NE(tab_list, nullptr);
  EXPECT_EQ(tab_list->canonical_id, "tab.list");
  EXPECT_TRUE(tab_list->surfaces & kDesktopAgentSurface);
  EXPECT_TRUE(tab_list->surfaces & kBrowserMcpSurface);
  EXPECT_EQ(tab_list->mutability, "ReadOnly");
  EXPECT_FALSE(tab_list->authority_change);
  EXPECT_EQ(tab_list->sensitivity, "Low");

  // 2. browser_tab_new promoted to DesktopAgentAndBrowserMcp
  const auto* tab_new = FindCatalogEntry(entries, "browser_tab_new");
  ASSERT_NE(tab_new, nullptr);
  EXPECT_EQ(tab_new->canonical_id, "tab.new");
  EXPECT_TRUE(tab_new->surfaces & kDesktopAgentSurface);
  EXPECT_TRUE(tab_new->surfaces & kBrowserMcpSurface);
  EXPECT_EQ(tab_new->mutability, "Mutable");
  EXPECT_TRUE(tab_new->authority_change);
  EXPECT_EQ(tab_new->sensitivity, "Sensitive");

  // 3. browser_tab_switch promoted to DesktopAgentAndBrowserMcp
  const auto* tab_switch = FindCatalogEntry(entries, "browser_tab_switch");
  ASSERT_NE(tab_switch, nullptr);
  EXPECT_EQ(tab_switch->canonical_id, "tab.switch");
  EXPECT_TRUE(tab_switch->surfaces & kDesktopAgentSurface);
  EXPECT_TRUE(tab_switch->surfaces & kBrowserMcpSurface);
  EXPECT_EQ(tab_switch->mutability, "Mutable");
  EXPECT_FALSE(tab_switch->authority_change);
  EXPECT_EQ(tab_switch->sensitivity, "Low");

  // 4. Verify unpromoted tab capabilities remain BrowserMcp only (negative surface test)
  const auto* tab_get = FindCatalogEntry(entries, "browser_tab_get");
  ASSERT_NE(tab_get, nullptr);
  EXPECT_FALSE(tab_get->surfaces & kDesktopAgentSurface);
  EXPECT_TRUE(tab_get->surfaces & kBrowserMcpSurface);

  const auto* tab_close = FindCatalogEntry(entries, "browser_tab_close");
  ASSERT_NE(tab_close, nullptr);
  EXPECT_FALSE(tab_close->surfaces & kDesktopAgentSurface);
  EXPECT_TRUE(tab_close->surfaces & kBrowserMcpSurface);
}

TEST(MahoPageAdapterRegistryTest, CanonicalOriginParsing) {
  EXPECT_EQ(CanonicalOrigin("https://docs.google.com/document/d/123/edit"),
            std::optional<std::string>("https://docs.google.com"));
  EXPECT_EQ(CanonicalOrigin("https://docs.google.com"),
            std::optional<std::string>("https://docs.google.com"));
  EXPECT_EQ(CanonicalOrigin("https://docs.google.com:8443/test"),
            std::optional<std::string>("https://docs.google.com:8443"));
  EXPECT_EQ(CanonicalOrigin("https://docs.google.com:443/test"),
            std::optional<std::string>("https://docs.google.com"));
  EXPECT_EQ(CanonicalOrigin("http://insecure.local"),
            std::optional<std::string>("http://insecure.local"));
  EXPECT_EQ(CanonicalOrigin("http://insecure.local:80/path"),
            std::optional<std::string>("http://insecure.local"));
  EXPECT_EQ(CanonicalOrigin("HTTPS://Docs.Google.COM/path"),
            std::optional<std::string>("https://docs.google.com"));

  EXPECT_EQ(CanonicalOrigin("ftp://invalid.scheme"), std::nullopt);
  EXPECT_EQ(CanonicalOrigin("javascript:alert(1)"), std::nullopt);
  EXPECT_EQ(CanonicalOrigin("not a url"), std::nullopt);
  EXPECT_EQ(CanonicalOrigin(""), std::nullopt);
}

TEST(MahoPageAdapterRegistryTest, MatchesExactOriginProbes) {
  EXPECT_TRUE(MatchesExactOrigin("https://docs.google.com",
                                 "https://docs.google.com/doc/1"));
  EXPECT_FALSE(MatchesExactOrigin("https://docs.google.com",
                                  "https://docs.google.com.evil.io"));
  EXPECT_FALSE(MatchesExactOrigin("https://docs.google.com",
                                  "https://docs.google.com.evil.io/doc/1"));
  EXPECT_FALSE(MatchesExactOrigin("https://docs.google.com",
                                  "https://evil.io/?target=https://docs.google.com"));
  EXPECT_FALSE(MatchesExactOrigin("https://docs.google.com",
                                  "https://sheets.google.com"));
  EXPECT_FALSE(MatchesExactOrigin("https://docs.google.com",
                                  "http://docs.google.com"));
  EXPECT_FALSE(MatchesExactOrigin("https://docs.google.com",
                                  "https://docs.google.com:8443"));
}

TEST(MahoPageAdapterRegistryTest,
     ExactOriginLookupAndInsertionOrderDeterminism) {
  MahoPageAdapterRegistry registry;

  // Adapter 1 registered first
  registry.RegisterSimple("google-docs-v1", {"https://docs.google.com"},
                          {"get_selection", "insert_text"});

  // Adapter 2 registered second for same origin with overlapping + new op
  registry.RegisterSimple("google-docs-v2", {"https://docs.google.com"},
                          {"get_selection", "delete_range"});

  EXPECT_EQ(registry.size(), 2u);
  EXPECT_FALSE(registry.empty());

  // Insertion order determinism: first registered adapter matching (origin, op) wins
  auto adapter1 = registry.Lookup("https://docs.google.com", "get_selection");
  ASSERT_NE(adapter1, nullptr);
  EXPECT_EQ(adapter1->id(), "google-docs-v1");

  // Operation supported only by adapter 2 resolves to adapter 2
  auto adapter2 = registry.Lookup("https://docs.google.com", "delete_range");
  ASSERT_NE(adapter2, nullptr);
  EXPECT_EQ(adapter2->id(), "google-docs-v2");

  // Lookup on full document URL resolves to exact origin
  auto adapter_from_url = registry.Lookup(
      "https://docs.google.com/document/d/123/edit", "insert_text");
  ASSERT_NE(adapter_from_url, nullptr);
  EXPECT_EQ(adapter_from_url->id(), "google-docs-v1");
}

TEST(MahoPageAdapterRegistryTest,
     EvilOriginAndDifferentOriginProbesNotMatched) {
  MahoPageAdapterRegistry registry;
  registry.RegisterSimple("google-docs-official", {"https://docs.google.com"},
                          {"get_selection", "read_document"});

  // Evil origin subdomain attack: https://docs.google.com.evil.io must NOT match
  EXPECT_EQ(registry.Lookup("https://docs.google.com.evil.io", "get_selection"),
            nullptr);
  EXPECT_EQ(registry.Lookup("https://docs.google.com.evil.io/document/d/1",
                            "get_selection"),
            nullptr);

  // Evil target parameter probe
  EXPECT_EQ(registry.Lookup("https://evil.io/?target=https://docs.google.com",
                            "get_selection"),
            nullptr);

  // Different subdomain
  EXPECT_EQ(registry.Lookup("https://sheets.google.com", "get_selection"),
            nullptr);

  // Different scheme (http vs https)
  EXPECT_EQ(registry.Lookup("http://docs.google.com", "get_selection"),
            nullptr);

  // Different port
  EXPECT_EQ(registry.Lookup("https://docs.google.com:8443", "get_selection"),
            nullptr);

  // Exact matching origin matches
  auto match = registry.Lookup("https://docs.google.com", "get_selection");
  ASSERT_NE(match, nullptr);
  EXPECT_EQ(match->id(), "google-docs-official");
}

TEST(MahoPageAdapterRegistryTest, UnsupportedOperationFallsThrough) {
  MahoPageAdapterRegistry registry;
  registry.RegisterSimple("docs-adapter", {"https://docs.google.com"},
                          {"get_selection"});

  EXPECT_EQ(registry.Lookup("https://docs.google.com", "non_existent_op"),
            nullptr);
  EXPECT_EQ(registry.Lookup("https://docs.google.com", "insert_text"),
            nullptr);
}

TEST(MahoPageAdapterRegistryTest, MultiOriginAndDescriptorRoundtrip) {
  MahoPageAdapterRegistry registry;
  registry.RegisterSimple("google-suite-adapter",
                          {"https://docs.google.com", "https://sheets.google.com"},
                          {"export_content"}, "isolated_world_1", 1048576);

  auto doc_match =
      registry.Lookup("https://docs.google.com/document/d/1", "export_content");
  ASSERT_NE(doc_match, nullptr);
  EXPECT_EQ(doc_match->id(), "google-suite-adapter");
  EXPECT_EQ(doc_match->execution_world(),
            std::optional<std::string>("isolated_world_1"));
  EXPECT_EQ(doc_match->max_output_bytes(), std::optional<size_t>(1048576));

  auto sheet_match =
      registry.Lookup("https://sheets.google.com/spreadsheets/d/2", "export_content");
  ASSERT_NE(sheet_match, nullptr);
  EXPECT_EQ(sheet_match->id(), "google-suite-adapter");

  PageAdapterDescriptor desc = doc_match->descriptor();
  EXPECT_EQ(desc.adapter_id, "google-suite-adapter");
  EXPECT_EQ(desc.exact_origin_patterns.size(), 2u);
  EXPECT_EQ(desc.supported_operations.size(), 1u);
  EXPECT_EQ(desc.supported_operations[0], "export_content");

  // Direct registration via constructor
  registry.RegisterDescriptor(PageAdapterDescriptor(
      "custom-adapter",
      {"https://custom.app"},
      {"custom_op"},
      "custom_world",
      4096));
  auto custom_match = registry.Lookup("https://custom.app/index", "custom_op");
  ASSERT_NE(custom_match, nullptr);
  EXPECT_EQ(custom_match->id(), "custom-adapter");
}

TEST(MahoPageAdapterRegistryTest, GlobalSeamLookupIntegration) {
  auto& global = MahoPageAdapterRegistry::GetGlobalRegistry();
  global.Clear();
  EXPECT_TRUE(global.empty());

  global.RegisterSimple("seam-docs-adapter", {"https://docs.google.com"},
                        {"extract_canvas"});

  auto adapter =
      LookupPageAdapterForSeam("https://docs.google.com/doc/abc", "extract_canvas");
  ASSERT_NE(adapter, nullptr);
  EXPECT_EQ(adapter->id(), "seam-docs-adapter");

  EXPECT_EQ(
      LookupPageAdapterForSeam("https://docs.google.com.evil.io", "extract_canvas"),
      nullptr);
  EXPECT_EQ(
      LookupPageAdapterForSeam("https://docs.google.com", "unknown_op"),
      nullptr);

  global.Clear();
  EXPECT_EQ(
      LookupPageAdapterForSeam("https://docs.google.com", "extract_canvas"),
      nullptr);
}

}  // namespace maho::ai

#else

int main() {
  using namespace maho::ai;
  std::cout << "[RUN] MahoPageAdapterRegistry standalone tests..." << std::endl;

  // 1. Catalog promotion test (both-direction)
  {
    const auto entries = LoadCatalogEntriesForTest();

    const auto* tab_list = FindCatalogEntry(entries, "browser_tab_list");
    assert(tab_list != nullptr);
    assert(tab_list->canonical_id == "tab.list");
    assert(tab_list->surfaces & kDesktopAgentSurface);
    assert(tab_list->surfaces & kBrowserMcpSurface);
    assert(tab_list->mutability == "ReadOnly");
    assert(!tab_list->authority_change);
    assert(tab_list->sensitivity == "Low");

    const auto* tab_new = FindCatalogEntry(entries, "browser_tab_new");
    assert(tab_new != nullptr);
    assert(tab_new->canonical_id == "tab.new");
    assert(tab_new->surfaces & kDesktopAgentSurface);
    assert(tab_new->surfaces & kBrowserMcpSurface);
    assert(tab_new->mutability == "Mutable");
    assert(tab_new->authority_change);
    assert(tab_new->sensitivity == "Sensitive");

    const auto* tab_switch = FindCatalogEntry(entries, "browser_tab_switch");
    assert(tab_switch != nullptr);
    assert(tab_switch->canonical_id == "tab.switch");
    assert(tab_switch->surfaces & kDesktopAgentSurface);
    assert(tab_switch->surfaces & kBrowserMcpSurface);
    assert(tab_switch->mutability == "Mutable");
    assert(!tab_switch->authority_change);
    assert(tab_switch->sensitivity == "Low");

    const auto* tab_get = FindCatalogEntry(entries, "browser_tab_get");
    assert(tab_get != nullptr);
    assert(!(tab_get->surfaces & kDesktopAgentSurface));
    assert(tab_get->surfaces & kBrowserMcpSurface);

    const auto* tab_close = FindCatalogEntry(entries, "browser_tab_close");
    assert(tab_close != nullptr);
    assert(!(tab_close->surfaces & kDesktopAgentSurface));
    assert(tab_close->surfaces & kBrowserMcpSurface);
  }

  // 2. Canonical origin parsing
  {
    assert(CanonicalOrigin("https://docs.google.com/document/d/123/edit") ==
           std::optional<std::string>("https://docs.google.com"));
    assert(CanonicalOrigin("https://docs.google.com") ==
           std::optional<std::string>("https://docs.google.com"));
    assert(CanonicalOrigin("https://docs.google.com:8443/test") ==
           std::optional<std::string>("https://docs.google.com:8443"));
    assert(CanonicalOrigin("https://docs.google.com:443/test") ==
           std::optional<std::string>("https://docs.google.com"));
    assert(CanonicalOrigin("http://insecure.local") ==
           std::optional<std::string>("http://insecure.local"));
    assert(CanonicalOrigin("http://insecure.local:80/path") ==
           std::optional<std::string>("http://insecure.local"));
    assert(CanonicalOrigin("HTTPS://Docs.Google.COM/path") ==
           std::optional<std::string>("https://docs.google.com"));

    assert(CanonicalOrigin("ftp://invalid.scheme") == std::nullopt);
    assert(CanonicalOrigin("javascript:alert(1)") == std::nullopt);
    assert(CanonicalOrigin("not a url") == std::nullopt);
    assert(CanonicalOrigin("") == std::nullopt);
  }

  // 3. Matches exact origin probes
  {
    assert(MatchesExactOrigin("https://docs.google.com",
                              "https://docs.google.com/doc/1"));
    assert(!MatchesExactOrigin("https://docs.google.com",
                               "https://docs.google.com.evil.io"));
    assert(!MatchesExactOrigin("https://docs.google.com",
                               "https://docs.google.com.evil.io/doc/1"));
    assert(!MatchesExactOrigin("https://docs.google.com",
                               "https://evil.io/?target=https://docs.google.com"));
    assert(!MatchesExactOrigin("https://docs.google.com",
                               "https://sheets.google.com"));
    assert(!MatchesExactOrigin("https://docs.google.com",
                               "http://docs.google.com"));
    assert(!MatchesExactOrigin("https://docs.google.com",
                               "https://docs.google.com:8443"));
  }

  // 4. Exact origin lookup and insertion order determinism
  {
    MahoPageAdapterRegistry registry;
    registry.RegisterSimple("google-docs-v1", {"https://docs.google.com"},
                            {"get_selection", "insert_text"});
    registry.RegisterSimple("google-docs-v2", {"https://docs.google.com"},
                            {"get_selection", "delete_range"});

    assert(registry.size() == 2);
    assert(!registry.empty());

    auto adapter1 = registry.Lookup("https://docs.google.com", "get_selection");
    assert(adapter1 != nullptr);
    assert(adapter1->id() == "google-docs-v1");

    auto adapter2 = registry.Lookup("https://docs.google.com", "delete_range");
    assert(adapter2 != nullptr);
    assert(adapter2->id() == "google-docs-v2");

    auto adapter_from_url = registry.Lookup(
        "https://docs.google.com/document/d/123/edit", "insert_text");
    assert(adapter_from_url != nullptr);
    assert(adapter_from_url->id() == "google-docs-v1");
  }

  // 5. Evil origin and different origin probes not matched
  {
    MahoPageAdapterRegistry registry;
    registry.RegisterSimple("google-docs-official", {"https://docs.google.com"},
                            {"get_selection", "read_document"});

    assert(registry.Lookup("https://docs.google.com.evil.io", "get_selection") ==
           nullptr);
    assert(registry.Lookup("https://docs.google.com.evil.io/document/d/1",
                           "get_selection") == nullptr);
    assert(registry.Lookup("https://evil.io/?target=https://docs.google.com",
                           "get_selection") == nullptr);
    assert(registry.Lookup("https://sheets.google.com", "get_selection") ==
           nullptr);
    assert(registry.Lookup("http://docs.google.com", "get_selection") ==
           nullptr);
    assert(registry.Lookup("https://docs.google.com:8443", "get_selection") ==
           nullptr);

    auto match = registry.Lookup("https://docs.google.com", "get_selection");
    assert(match != nullptr);
    assert(match->id() == "google-docs-official");
  }

  // 6. Unsupported operation falls through
  {
    MahoPageAdapterRegistry registry;
    registry.RegisterSimple("docs-adapter", {"https://docs.google.com"},
                            {"get_selection"});

    assert(registry.Lookup("https://docs.google.com", "non_existent_op") ==
           nullptr);
    assert(registry.Lookup("https://docs.google.com", "insert_text") ==
           nullptr);
  }

  // 7. Multi-origin and descriptor roundtrip
  {
    MahoPageAdapterRegistry registry;
    registry.RegisterSimple("google-suite-adapter",
                            {"https://docs.google.com", "https://sheets.google.com"},
                            {"export_content"}, "isolated_world_1", 1048576);

    auto doc_match = registry.Lookup("https://docs.google.com/document/d/1",
                                     "export_content");
    assert(doc_match != nullptr);
    assert(doc_match->id() == "google-suite-adapter");
    assert(doc_match->execution_world() ==
           std::optional<std::string>("isolated_world_1"));
    assert(doc_match->max_output_bytes() == std::optional<size_t>(1048576));

    auto sheet_match = registry.Lookup(
        "https://sheets.google.com/spreadsheets/d/2", "export_content");
    assert(sheet_match != nullptr);
    assert(sheet_match->id() == "google-suite-adapter");

    PageAdapterDescriptor desc = doc_match->descriptor();
    assert(desc.adapter_id == "google-suite-adapter");
    assert(desc.exact_origin_patterns.size() == 2);
    assert(desc.supported_operations.size() == 1);
    assert(desc.supported_operations[0] == "export_content");

    // Direct registration via helper constructor
    registry.RegisterDescriptor(PageAdapterDescriptor(
        "custom-adapter",
        {"https://custom.app"},
        {"custom_op"},
        "custom_world",
        4096));
    auto custom_match = registry.Lookup("https://custom.app/index", "custom_op");
    assert(custom_match != nullptr);
    assert(custom_match->id() == "custom-adapter");
  }

  // 8. Global seam lookup integration
  {
    auto& global = MahoPageAdapterRegistry::GetGlobalRegistry();
    global.Clear();
    assert(global.empty());

    global.RegisterSimple("seam-docs-adapter", {"https://docs.google.com"},
                          {"extract_canvas"});

    auto adapter =
        LookupPageAdapterForSeam("https://docs.google.com/doc/abc", "extract_canvas");
    assert(adapter != nullptr);
    assert(adapter->id() == "seam-docs-adapter");

    assert(LookupPageAdapterForSeam("https://docs.google.com.evil.io",
                                   "extract_canvas") == nullptr);
    assert(LookupPageAdapterForSeam("https://docs.google.com", "unknown_op") ==
           nullptr);

    global.Clear();
    assert(LookupPageAdapterForSeam("https://docs.google.com",
                                   "extract_canvas") == nullptr);
  }

  std::cout << "[PASS] All MahoPageAdapterRegistry standalone tests passed!"
            << std::endl;
  return 0;
}

#endif
