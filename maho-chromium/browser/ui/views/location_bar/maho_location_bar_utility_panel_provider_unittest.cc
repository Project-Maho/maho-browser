// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h"

#include <optional>
#include <memory>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "maho/browser/maho_core_holder.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

class ScopedCoreOverride {
 public:
  explicit ScopedCoreOverride(MahoCore* core) : saved_core_(GetCore()) {
    SetCore(core);
  }

  ScopedCoreOverride(const ScopedCoreOverride&) = delete;
  ScopedCoreOverride& operator=(const ScopedCoreOverride&) = delete;

 ~ScopedCoreOverride() { SetCore(saved_core_); }

 private:
  base::raw_ptr<MahoCore> saved_core_ = nullptr;
};

std::optional<base::Value> ParseJson(const std::string& json) {
  return base::JSONReader::Read(json, base::JSON_PARSE_RFC);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     DeveloperModeActionReflectsParsedSettingsBranches) {
  const auto disabled_settings = ParseJson(R"({"advanced":{"developer_mode":false}})");
  ASSERT_TRUE(disabled_settings.has_value());
  MahoLocationBarUtilityPanelAction disabled =
      internal::BuildDeveloperModeAction(&*disabled_settings);
  EXPECT_EQ(disabled.title, u"Developer mode");
  EXPECT_EQ(disabled.subtitle, u"Disabled");
  EXPECT_EQ(disabled.data_state,
            MahoLocationBarUtilityPanelDataState::kLive);

  const auto missing_advanced = ParseJson(R"({"profile":"default"})");
  ASSERT_TRUE(missing_advanced.has_value());
  MahoLocationBarUtilityPanelAction unavailable =
      internal::BuildDeveloperModeAction(&*missing_advanced);
  EXPECT_EQ(unavailable.subtitle, u"Developer mode unavailable");
  EXPECT_EQ(unavailable.data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);

  MahoLocationBarUtilityPanelAction null_action =
      internal::BuildDeveloperModeAction(nullptr);
  EXPECT_EQ(null_action.subtitle, u"Developer mode unavailable");
  EXPECT_EQ(null_action.data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     InstalledExtensionsBuildsFallbackAndConcreteItems) {
  std::vector<MahoLocationBarUtilityPanelAction> invalid =
      internal::BuildInstalledExtensions("{}");
  ASSERT_EQ(invalid.size(), 1u);
  EXPECT_EQ(invalid[0].title, u"Extensions");
  EXPECT_EQ(invalid[0].subtitle, u"Extension list unavailable");
  EXPECT_EQ(invalid[0].data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);

  std::vector<MahoLocationBarUtilityPanelAction> extensions =
      internal::BuildInstalledExtensions(R"([
        {"id":"alpha","name":"Alpha","version":"1.2.3","enabled":true},
        {"id":"beta","enabled":false}
      ])");
  ASSERT_EQ(extensions.size(), 2u);
  EXPECT_EQ(extensions[0].title, u"Alpha");
  EXPECT_EQ(extensions[0].subtitle, u"v1.2.3 · Enabled");
  EXPECT_EQ(extensions[1].title, u"beta");
  EXPECT_EQ(extensions[1].subtitle, u"Disabled");
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ExtensionsSummaryPreservesSingleFallbackAndCountsMultipleItems) {
  std::vector<MahoLocationBarUtilityPanelAction> fallback =
      internal::BuildInstalledExtensions("{}");
  MahoLocationBarUtilityPanelAction summary =
      internal::BuildExtensionsSummaryAction(fallback);
  EXPECT_EQ(summary.title, u"Extensions");
  EXPECT_EQ(summary.subtitle, u"Extension list unavailable");
  EXPECT_EQ(summary.data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);

  std::vector<MahoLocationBarUtilityPanelAction> multiple(2);
  multiple[0].title = u"One";
  multiple[1].title = u"Two";
  summary = internal::BuildExtensionsSummaryAction(multiple);
  EXPECT_EQ(summary.title, u"Extensions");
  EXPECT_EQ(summary.subtitle, u"2 installed");
  EXPECT_EQ(summary.data_state, MahoLocationBarUtilityPanelDataState::kLive);
}

TEST(MahoLocationBarUtilityPanelProviderTest,
     PublicModelUsesCoreFallbackAndStillBuildsSections) {
  ScopedCoreOverride scoped_core(nullptr);

  MahoLocationBarUtilityPanelModel model =
      BuildMahoLocationBarUtilityPanelModel(nullptr);

  EXPECT_EQ(model.extensions.size(), 1u);
  EXPECT_EQ(model.extensions[0].subtitle, u"Core unavailable");
  ASSERT_EQ(model.quick_actions.size(), 3u);
  EXPECT_EQ(model.quick_actions[0].subtitle, u"Core unavailable");
  EXPECT_EQ(model.quick_actions[1].subtitle, u"Developer mode unavailable");
  EXPECT_EQ(model.quick_actions[2].subtitle,
            u"Unsupported in this window");
  EXPECT_EQ(model.footer.title, u"Security state unavailable");
}

}  // namespace
}  // namespace maho
