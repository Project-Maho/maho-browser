// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "components/omnibox/browser/location_bar_model.h"
#include "components/security_state/core/security_state.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/vector_icon_types.h"
#include "url/gurl.h"

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

class FakeLocationBarModel : public LocationBarModel {
 public:
  explicit FakeLocationBarModel(
      GURL url,
      security_state::SecurityLevel level = security_state::SECURE)
      : url_(std::move(url)), level_(level) {}

  std::u16string GetFormattedFullURL() const override { return {}; }
  std::u16string GetURLForDisplay() const override { return {}; }
  GURL GetURL() const override { return url_; }
  bool IsContextualTasksPage() const override { return false; }
  GURL GetContextualTasksInnerFrameURL() const override { return {}; }
  security_state::SecurityLevel GetSecurityLevel() const override {
    return level_;
  }
  net::CertStatus GetCertStatus() const override { return 0; }
  metrics::OmniboxEventProto::PageClassification GetPageClassification(
      bool) const override {
    return metrics::OmniboxEventProto::OTHER;
  }
  metrics::OmniboxEventProto::PageClassification
  GetOmniboxComposeboxPageClassification() const override {
    return metrics::OmniboxEventProto::OTHER;
  }
  const gfx::VectorIcon& GetVectorIcon() const override {
    return gfx::VectorIcon::EmptyIcon();
  }
  std::u16string GetSecureDisplayText() const override { return {}; }
  std::u16string GetSecureAccessibilityText() const override { return {}; }
  bool ShouldDisplayURL() const override { return true; }
  bool IsOfflinePage() const override { return false; }
  bool ShouldPreventElision() const override { return false; }

 private:
  GURL url_;
  security_state::SecurityLevel level_;
};

std::optional<base::Value> ParseJson(const std::string& json) {
  return base::JSONReader::Read(json, base::JSON_PARSE_RFC);
}

std::optional<base::Value> ReadCoreSettingsForTest(MahoCore* core) {
  if (!core) {
    return std::nullopt;
  }

  char* json_str = maho_core_get_settings(core);
  if (!json_str) {
    return std::nullopt;
  }

  std::string json(json_str);
  maho_string_free(json_str);
  return ParseJson(json);
}

std::vector<std::u16string> TitlesFrom(
    const std::vector<MahoLocationBarUtilityPanelAction>& actions) {
  std::vector<std::u16string> titles;
  titles.reserve(actions.size());
  for (const auto& action : actions) {
    titles.push_back(action.title);
  }
  return titles;
}

const MahoUtilityPanelZone* FindZone(const MahoLocationBarUtilityPanelModel& model,
                                     MahoUtilityPanelZoneId id) {
  for (const auto& zone : model.zones) {
    if (zone.id == id) {
      return &zone;
    }
  }
  return nullptr;
}

void ExpectActionsEquivalent(const MahoLocationBarUtilityPanelAction& actual,
                             const MahoLocationBarUtilityPanelAction& expected) {
  EXPECT_EQ(actual.title, expected.title);
  EXPECT_EQ(actual.subtitle, expected.subtitle);
  EXPECT_EQ(actual.accessible_name, expected.accessible_name);
  EXPECT_EQ(actual.sf_symbol_name, expected.sf_symbol_name);
  EXPECT_EQ(actual.enabled, expected.enabled);
  EXPECT_EQ(actual.data_state, expected.data_state);
  EXPECT_EQ(actual.close_after_activate, expected.close_after_activate);
  EXPECT_EQ(actual.callback.is_null(), expected.callback.is_null());
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     DeveloperModeActionReflectsParsedSettingsBranches) {
  const auto disabled_settings =
      ParseJson(R"({"advanced":{"developer_mode":false}})");
  ASSERT_TRUE(disabled_settings.has_value());
  MahoLocationBarUtilityPanelAction disabled =
      internal::BuildDeveloperModeAction(&*disabled_settings);
  EXPECT_EQ(disabled.title, u"Developer mode");
  EXPECT_EQ(disabled.subtitle, u"Disabled");
  EXPECT_EQ(disabled.sf_symbol_name, "hammer");
  EXPECT_EQ(disabled.data_state,
            MahoLocationBarUtilityPanelDataState::kLive);
  EXPECT_FALSE(disabled.enabled);

  const auto missing_advanced = ParseJson(R"({"profile":"default"})");
  ASSERT_TRUE(missing_advanced.has_value());
  MahoLocationBarUtilityPanelAction unavailable =
      internal::BuildDeveloperModeAction(&*missing_advanced);
  EXPECT_EQ(unavailable.subtitle, u"Developer mode unavailable");
  EXPECT_EQ(unavailable.sf_symbol_name, "hammer");
  EXPECT_EQ(unavailable.data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);
  EXPECT_FALSE(unavailable.enabled);

  MahoLocationBarUtilityPanelAction null_action =
      internal::BuildDeveloperModeAction(nullptr);
  EXPECT_EQ(null_action.subtitle, u"Developer mode unavailable");
  EXPECT_EQ(null_action.sf_symbol_name, "hammer");
  EXPECT_EQ(null_action.data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);
  EXPECT_FALSE(null_action.enabled);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ShareActionUsesNativeSharingHubSubtitleAndIsUnavailableWithNullBrowser) {
  MahoLocationBarUtilityPanelAction share = internal::BuildShareAction(nullptr);
  EXPECT_EQ(share.title, u"Share");
  EXPECT_EQ(share.subtitle, u"Current page unavailable");
  EXPECT_NE(share.subtitle, u"Copy page URL");
  EXPECT_EQ(share.sf_symbol_name, "square.and.arrow.up");
  EXPECT_EQ(share.data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);
  EXPECT_FALSE(share.enabled);
  EXPECT_FALSE(share.callback);
  EXPECT_TRUE(share.close_after_activate);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ShieldsActionIsUnavailableWithNullBrowser) {
  MahoLocationBarUtilityPanelAction shields =
      internal::BuildShieldsAction(nullptr);
  EXPECT_EQ(shields.title, u"Shields");
  EXPECT_EQ(shields.subtitle, u"Current page unavailable");
  EXPECT_EQ(shields.sf_symbol_name, "shield.lefthalf.filled");
  EXPECT_EQ(shields.data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);
  EXPECT_FALSE(shields.enabled);
  EXPECT_FALSE(shields.callback);
  EXPECT_TRUE(shields.close_after_activate);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     AutoPipSettingsRowNullContextIsUnavailable) {
  MahoLocationBarUtilityPanelAction row =
      internal::BuildAutoPipSettingsRow(nullptr, nullptr);
  EXPECT_EQ(row.title, u"Automatic Picture-In-Picture");
  EXPECT_EQ(row.subtitle, u"No active page available");
  EXPECT_EQ(row.sf_symbol_name, "pip.enter");
  EXPECT_EQ(row.data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);
  EXPECT_FALSE(row.enabled);
  EXPECT_FALSE(row.callback);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     InstalledExtensionsBuildsFallbackAndConcreteItems) {
  std::vector<MahoLocationBarUtilityPanelAction> invalid =
      internal::BuildInstalledExtensions("{}");
  ASSERT_EQ(invalid.size(), 1u);
  EXPECT_EQ(invalid[0].title, u"Extensions");
  EXPECT_EQ(invalid[0].subtitle, u"Extension list unavailable");
  EXPECT_EQ(invalid[0].sf_symbol_name, "puzzlepiece.extension");
  EXPECT_EQ(invalid[0].data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);
  EXPECT_FALSE(invalid[0].enabled);
  EXPECT_FALSE(invalid[0].callback);

  std::vector<MahoLocationBarUtilityPanelAction> extensions =
      internal::BuildInstalledExtensions(R"([
        {"id":"alpha","name":"Alpha","version":"1.2.3","enabled":true},
        {"id":"beta","enabled":false}
      ])");
  ASSERT_EQ(extensions.size(), 2u);
  EXPECT_EQ(extensions[0].title, u"Alpha");
  EXPECT_EQ(extensions[0].subtitle, u"v1.2.3 · Enabled");
  EXPECT_EQ(extensions[0].sf_symbol_name, "puzzlepiece.extension");
  EXPECT_FALSE(extensions[0].enabled);
  EXPECT_FALSE(extensions[0].callback);
  EXPECT_EQ(extensions[1].title, u"beta");
  EXPECT_EQ(extensions[1].subtitle, u"Disabled");
  EXPECT_EQ(extensions[1].sf_symbol_name, "puzzlepiece.extension");
  EXPECT_FALSE(extensions[1].enabled);
  EXPECT_FALSE(extensions[1].callback);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ExtensionsSummaryPreservesSingleFallbackAndCountsMultipleItems) {
  std::vector<MahoLocationBarUtilityPanelAction> fallback =
      internal::BuildInstalledExtensions("{}");
  MahoLocationBarUtilityPanelAction summary =
      internal::BuildExtensionsSummaryAction(fallback);
  EXPECT_EQ(summary.title, u"Extensions");
  EXPECT_EQ(summary.subtitle, u"Extension list unavailable");
  EXPECT_EQ(summary.sf_symbol_name, "puzzlepiece.extension");
  EXPECT_EQ(summary.data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);
  EXPECT_FALSE(summary.enabled);
  EXPECT_FALSE(summary.callback);

  std::vector<MahoLocationBarUtilityPanelAction> multiple(2);
  multiple[0].title = u"One";
  multiple[1].title = u"Two";
  summary = internal::BuildExtensionsSummaryAction(multiple);
  EXPECT_EQ(summary.title, u"Extensions");
  EXPECT_EQ(summary.subtitle, u"2 installed");
  EXPECT_EQ(summary.sf_symbol_name, "puzzlepiece.extension");
  EXPECT_EQ(summary.data_state, MahoLocationBarUtilityPanelDataState::kLive);
  EXPECT_FALSE(summary.enabled);
  EXPECT_FALSE(summary.callback);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     SecuritySymbolNameReflectsSecurityLevel) {
  EXPECT_EQ(internal::GetSecuritySFSymbolNameForLevel(
                static_cast<int>(security_state::NONE)),
            "questionmark.shield");
  EXPECT_EQ(internal::GetSecuritySFSymbolNameForLevel(
                static_cast<int>(security_state::SECURE)),
            "checkmark.shield");
  EXPECT_EQ(internal::GetSecuritySFSymbolNameForLevel(
                static_cast<int>(security_state::WARNING)),
            "exclamationmark.shield");
  EXPECT_EQ(internal::GetSecuritySFSymbolNameForLevel(
                static_cast<int>(security_state::DANGEROUS)),
            "xmark.shield");
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ToolbarZoneHasFourActionsInExactOrder) {
  MahoUtilityPanelZone zone = internal::BuildToolbarZone(nullptr);
  EXPECT_EQ(zone.id, MahoUtilityPanelZoneId::kToolbar);
  EXPECT_EQ(zone.style, MahoUtilityPanelZoneStyle::kToolbarRow);
  ASSERT_EQ(zone.items.size(), 4u);

  EXPECT_EQ(zone.items[0].title, u"Share");
  EXPECT_EQ(zone.items[0].sf_symbol_name, "square.and.arrow.up");
  EXPECT_FALSE(zone.items[0].callback);
  EXPECT_FALSE(zone.items[0].enabled);

  EXPECT_EQ(zone.items[1].title, u"Unavailable action");
  EXPECT_EQ(zone.items[1].sf_symbol_name, "wand.and.stars");
  EXPECT_FALSE(zone.items[1].enabled);
  EXPECT_FALSE(zone.items[1].callback);
  EXPECT_EQ(zone.items[2].title, u"Unavailable action");
  EXPECT_EQ(zone.items[2].sf_symbol_name, "camera");
  EXPECT_FALSE(zone.items[2].enabled);
  EXPECT_FALSE(zone.items[2].callback);
  EXPECT_EQ(zone.items[3].title, u"Unavailable action");
  EXPECT_EQ(zone.items[3].sf_symbol_name, "viewfinder");
  EXPECT_FALSE(zone.items[3].enabled);
  EXPECT_FALSE(zone.items[3].callback);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ToolbarZoneHasNoHeader) {
  MahoUtilityPanelZone zone = internal::BuildToolbarZone(nullptr);
  EXPECT_TRUE(zone.header_label.empty());
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ExtensionsZoneHeaderLabelIsExtensions) {
  MahoUtilityPanelZone zone = internal::BuildExtensionsZone("[]", nullptr);
  EXPECT_EQ(zone.id, MahoUtilityPanelZoneId::kExtensions);
  EXPECT_EQ(zone.style, MahoUtilityPanelZoneStyle::kChipRow);
  EXPECT_EQ(zone.header_label, u"Extensions");
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ExtensionsZoneEmptyJsonProducesFallbackPlusAddChip) {
  MahoUtilityPanelZone zone = internal::BuildExtensionsZone("[]", nullptr);
  ASSERT_EQ(zone.items.size(), 2u);
  EXPECT_EQ(zone.items.front().title, u"No extensions");
  EXPECT_TRUE(zone.items.front().subtitle.empty());
  EXPECT_FALSE(zone.items.front().enabled);
  EXPECT_FALSE(zone.items.front().callback);
  EXPECT_EQ(zone.items.back().title, u"+");
  EXPECT_FALSE(zone.items.back().enabled);
  EXPECT_FALSE(zone.items.back().callback);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ExtensionsZoneInvalidJsonProducesUnavailableFallbackPlusAddChip) {
  MahoUtilityPanelZone zone = internal::BuildExtensionsZone("", nullptr);
  ASSERT_EQ(zone.items.size(), 2u);
  EXPECT_EQ(zone.items.front().title, u"Extensions");
  EXPECT_EQ(zone.items.front().subtitle, u"Extension list unavailable");
  EXPECT_FALSE(zone.items.front().enabled);
  EXPECT_FALSE(zone.items.front().callback);
  EXPECT_EQ(zone.items.back().title, u"+");
  EXPECT_FALSE(zone.items.back().enabled);
  EXPECT_FALSE(zone.items.back().callback);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ExtensionsZoneNonEmptyJsonAppendsOnlyAddChip) {
  MahoUtilityPanelZone zone = internal::BuildExtensionsZone(
      R"([{"id":"alpha","name":"Alpha","version":"1.2.3","enabled":true}])",
      nullptr);
  ASSERT_EQ(zone.items.size(), 2u);
  EXPECT_EQ(zone.items.front().title, u"Alpha");
  EXPECT_EQ(zone.items.back().title, u"+");
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     SettingsZoneHeaderLabelIsSettings) {
  ScopedCoreOverride scoped_core(nullptr);
  MahoUtilityPanelZone zone = internal::BuildSettingsZone(nullptr);
  EXPECT_EQ(zone.id, MahoUtilityPanelZoneId::kSettings);
  EXPECT_EQ(zone.style, MahoUtilityPanelZoneStyle::kSettingsList);
  EXPECT_EQ(zone.header_label, u"Settings");
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     SettingsZoneRowsInOrder) {
  ScopedCoreOverride scoped_core(nullptr);
  MahoUtilityPanelZone zone = internal::BuildSettingsZone(nullptr);
  ASSERT_EQ(zone.items.size(), 3u);
  EXPECT_EQ(TitlesFrom(zone.items),
            (std::vector<std::u16string>{u"Developer mode",
                                         u"Block Ads & Trackers",
                                         u"Automatic Picture-In-Picture"}));
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     SettingsBlockAdsRowUsesSiteExceptionsOriginMatchingHelper) {
  EXPECT_TRUE(internal::IsOriginExceptedInSiteExceptions(
      R"(["https://maho.com","https://example.com"])",
      "https://example.com"));
  EXPECT_FALSE(internal::IsOriginExceptedInSiteExceptions(
      R"(["https://maho.com"])", "https://example.com"));
  EXPECT_FALSE(internal::IsOriginExceptedInSiteExceptions("{}",
                                                           "https://example.com"));

  MahoLocationBarUtilityPanelAction row =
      internal::BuildBlockAdsAndTrackersRow(nullptr);
  EXPECT_EQ(row.title, u"Block Ads & Trackers");
  EXPECT_EQ(row.subtitle, u"Current page unavailable");
  EXPECT_EQ(row.sf_symbol_name, "shield.lefthalf.filled");
  EXPECT_EQ(row.data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);
  EXPECT_FALSE(row.enabled);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     SettingsDeveloperModeRowDelegatesToBuildDeveloperModeAction) {
  MahoCore* core = maho_core_new();
  ASSERT_TRUE(core);
  {
    ScopedCoreOverride scoped_core(core);
    std::optional<base::Value> parsed_settings = ReadCoreSettingsForTest(core);
    ASSERT_TRUE(parsed_settings.has_value());

    MahoUtilityPanelZone zone = internal::BuildSettingsZone(nullptr);
    ASSERT_GE(zone.items.size(), 1u);
    MahoLocationBarUtilityPanelAction expected =
        internal::BuildDeveloperModeAction(&*parsed_settings);
    EXPECT_EQ(zone.items[0].title, expected.title);
    EXPECT_EQ(zone.items[0].subtitle, expected.subtitle);
    EXPECT_EQ(zone.items[0].sf_symbol_name, expected.sf_symbol_name);
    EXPECT_EQ(zone.items[0].enabled, expected.enabled);
    EXPECT_EQ(zone.items[0].data_state, expected.data_state);
  }
  maho_core_free(core);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     SettingsAutoPipRowDelegatesToBuildAutoPipSettingsRow) {
  ScopedCoreOverride scoped_core(nullptr);
  MahoUtilityPanelZone zone = internal::BuildSettingsZone(nullptr);
  ASSERT_EQ(zone.items.size(), 3u);
  ExpectActionsEquivalent(zone.items[2],
                          internal::BuildAutoPipSettingsRow(nullptr, nullptr));
}

TEST(MahoLocationBarUtilityPanelProviderTest,
     BuildModelHasThreeArcShapeZonesInOrder) {
  ScopedCoreOverride scoped_core(nullptr);
  MahoLocationBarUtilityPanelModel model =
      BuildMahoLocationBarUtilityPanelModel(nullptr);
  ASSERT_EQ(model.zones.size(), 3u);
  EXPECT_EQ(model.zones[0].id, MahoUtilityPanelZoneId::kToolbar);
  EXPECT_EQ(model.zones[1].id, MahoUtilityPanelZoneId::kExtensions);
  EXPECT_EQ(model.zones[2].id, MahoUtilityPanelZoneId::kSettings);
}

TEST(MahoLocationBarUtilityPanelProviderTest,
     ModelUsesArcHeadersAndUnavailableNullContextRows) {
  ScopedCoreOverride scoped_core(nullptr);
  MahoLocationBarUtilityPanelModel model =
      BuildMahoLocationBarUtilityPanelModel(nullptr);

  const MahoUtilityPanelZone* toolbar_zone =
      FindZone(model, MahoUtilityPanelZoneId::kToolbar);
  const MahoUtilityPanelZone* extensions_zone =
      FindZone(model, MahoUtilityPanelZoneId::kExtensions);
  const MahoUtilityPanelZone* settings_zone =
      FindZone(model, MahoUtilityPanelZoneId::kSettings);

  ASSERT_NE(toolbar_zone, nullptr);
  ASSERT_NE(extensions_zone, nullptr);
  ASSERT_NE(settings_zone, nullptr);

  EXPECT_TRUE(toolbar_zone->header_label.empty());
  EXPECT_EQ(extensions_zone->header_label, u"Extensions");
  EXPECT_EQ(settings_zone->header_label, u"Settings");

  ASSERT_EQ(extensions_zone->items.size(), 2u);
  EXPECT_EQ(extensions_zone->items[0].title, u"Extensions");
  EXPECT_EQ(extensions_zone->items[0].subtitle, u"Extension list unavailable");
  EXPECT_FALSE(extensions_zone->items[0].enabled);
  EXPECT_EQ(extensions_zone->items[1].title, u"+");
  EXPECT_FALSE(extensions_zone->items[1].enabled);

  ASSERT_EQ(settings_zone->items.size(), 3u);
  EXPECT_EQ(settings_zone->items[0].subtitle, u"Developer mode unavailable");
  EXPECT_EQ(settings_zone->items[1].subtitle, u"Current page unavailable");
  EXPECT_EQ(settings_zone->items[2].subtitle, u"No active page available");
}

TEST(MahoLocationBarUtilityPanelProviderTest,
     FooterHasSecurityStateUnavailableWithNullLocationBar) {
  ScopedCoreOverride scoped_core(nullptr);

  MahoLocationBarUtilityPanelModel model =
      BuildMahoLocationBarUtilityPanelModel(nullptr);

  EXPECT_EQ(model.footer.title, u"Security state unavailable");
  EXPECT_EQ(model.footer.sf_symbol_name, "questionmark.shield");
  EXPECT_EQ(model.footer.data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ZoomInActionForStateEnabledWhenActiveAndNotAtMax) {
  MahoLocationBarUtilityPanelAction mid =
      internal::BuildZoomInActionForState(/*has_active_page=*/true,
                                          /*at_max_zoom=*/false);
  EXPECT_EQ(mid.title, u"Zoom In");
  EXPECT_EQ(mid.sf_symbol_name, "plus.magnifyingglass");
  EXPECT_EQ(mid.data_state, MahoLocationBarUtilityPanelDataState::kLive);
  EXPECT_TRUE(mid.enabled);
  EXPECT_FALSE(mid.close_after_activate);
  EXPECT_TRUE(mid.subtitle.empty());
  EXPECT_EQ(mid.accessible_name, u"Zoom In");

  MahoLocationBarUtilityPanelAction at_max =
      internal::BuildZoomInActionForState(/*has_active_page=*/true,
                                          /*at_max_zoom=*/true);
  EXPECT_FALSE(at_max.enabled);

  MahoLocationBarUtilityPanelAction no_page =
      internal::BuildZoomInActionForState(/*has_active_page=*/false,
                                          /*at_max_zoom=*/false);
  EXPECT_FALSE(no_page.enabled);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ZoomOutActionForStateEnabledWhenActiveAndNotAtMin) {
  MahoLocationBarUtilityPanelAction mid =
      internal::BuildZoomOutActionForState(/*has_active_page=*/true,
                                           /*at_min_zoom=*/false);
  EXPECT_EQ(mid.title, u"Zoom Out");
  EXPECT_EQ(mid.sf_symbol_name, "minus.magnifyingglass");
  EXPECT_EQ(mid.data_state, MahoLocationBarUtilityPanelDataState::kLive);
  EXPECT_TRUE(mid.enabled);
  EXPECT_FALSE(mid.close_after_activate);
  EXPECT_TRUE(mid.subtitle.empty());
  EXPECT_EQ(mid.accessible_name, u"Zoom Out");

  MahoLocationBarUtilityPanelAction at_min =
      internal::BuildZoomOutActionForState(/*has_active_page=*/true,
                                           /*at_min_zoom=*/true);
  EXPECT_FALSE(at_min.enabled);

  MahoLocationBarUtilityPanelAction no_page =
      internal::BuildZoomOutActionForState(/*has_active_page=*/false,
                                           /*at_min_zoom=*/false);
  EXPECT_FALSE(no_page.enabled);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ZoomResetActionForStateSubtitleAndEnabledBehavior) {
  MahoLocationBarUtilityPanelAction no_controller =
      internal::BuildZoomResetActionForState(/*has_active_page=*/false,
                                             /*at_default_zoom=*/true,
                                             /*zoom_percent=*/std::nullopt);
  EXPECT_EQ(no_controller.title, u"Reset Zoom");
  EXPECT_TRUE(no_controller.subtitle.empty());
  EXPECT_EQ(no_controller.accessible_name, u"Reset Zoom");
  EXPECT_FALSE(no_controller.enabled);
  EXPECT_EQ(no_controller.sf_symbol_name, "1.magnifyingglass");
  EXPECT_FALSE(no_controller.close_after_activate);

  MahoLocationBarUtilityPanelAction at_default =
      internal::BuildZoomResetActionForState(/*has_active_page=*/true,
                                             /*at_default_zoom=*/true,
                                             /*zoom_percent=*/100);
  EXPECT_EQ(at_default.subtitle, u"100%");
  EXPECT_EQ(at_default.accessible_name, u"Reset Zoom — 100%");
  EXPECT_FALSE(at_default.enabled);

  MahoLocationBarUtilityPanelAction not_at_default =
      internal::BuildZoomResetActionForState(/*has_active_page=*/true,
                                             /*at_default_zoom=*/false,
                                             /*zoom_percent=*/150);
  EXPECT_EQ(not_at_default.subtitle, u"150%");
  EXPECT_EQ(not_at_default.accessible_name, u"Reset Zoom — 150%");
  EXPECT_TRUE(not_at_default.enabled);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     IsAtMaxZoomAndIsAtMinZoomReturnTrueForNullController) {
  EXPECT_TRUE(internal::IsAtMaxZoom(nullptr));
  EXPECT_TRUE(internal::IsAtMinZoom(nullptr));
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ZoomInAtMaxProducesDisabledActionWithNoCallback) {
  MahoLocationBarUtilityPanelAction at_max =
      internal::BuildZoomInActionForState(/*has_active_page=*/true,
                                          /*at_max_zoom=*/true);
  EXPECT_FALSE(at_max.enabled);
  EXPECT_FALSE(at_max.callback);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ZoomOutAtMinProducesDisabledActionWithNoCallback) {
  MahoLocationBarUtilityPanelAction at_min =
      internal::BuildZoomOutActionForState(/*has_active_page=*/true,
                                           /*at_min_zoom=*/true);
  EXPECT_FALSE(at_min.enabled);
  EXPECT_FALSE(at_min.callback);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ZoomResetAtDefaultProducesDisabledActionWithNoCallback) {
  MahoLocationBarUtilityPanelAction at_default =
      internal::BuildZoomResetActionForState(/*has_active_page=*/true,
                                             /*at_default_zoom=*/true,
                                             /*zoom_percent=*/100);
  EXPECT_FALSE(at_default.enabled);
  EXPECT_FALSE(at_default.callback);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     BuildFooterFromWebContentsNullIsUnavailableWithPageSecuritySubtitle) {
  MahoLocationBarUtilityPanelFooter footer =
      internal::BuildFooterFromWebContents(nullptr);
  EXPECT_EQ(footer.title, u"Security state unavailable");
  EXPECT_EQ(footer.subtitle, u"Page security");
  EXPECT_EQ(footer.sf_symbol_name, "questionmark.shield");
  EXPECT_EQ(footer.data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);
  EXPECT_EQ(footer.security_level, static_cast<int>(security_state::NONE));
  EXPECT_EQ(footer.primary_action_label, u"Close");
  EXPECT_TRUE(footer.close_after_primary_action);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     BlockAdsAndTrackersSubtitleIsCorrectlyOrientedForExceptionState) {
  // is_excepted=true means the origin is in the site exceptions list, so
  // blocking is OFF (ads allowed). The subtitle should reflect "allowed".
  EXPECT_TRUE(internal::IsOriginExceptedInSiteExceptions(
      R"(["https://example.com"])", "https://example.com"));
  EXPECT_FALSE(internal::IsOriginExceptedInSiteExceptions(
      R"(["https://other.com"])", "https://example.com"));
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     BuildFooterFromModelNullIsUnavailableWithPageSecuritySubtitle) {
  MahoLocationBarUtilityPanelFooter footer =
      internal::BuildFooterFromModel(nullptr);
  EXPECT_EQ(footer.title, u"Security state unavailable");
  EXPECT_EQ(footer.subtitle, u"Page security");
  EXPECT_EQ(footer.sf_symbol_name, "questionmark.shield");
  EXPECT_EQ(footer.data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);
  EXPECT_EQ(footer.security_level, static_cast<int>(security_state::NONE));
  EXPECT_EQ(footer.primary_action_label, u"Close");
  EXPECT_TRUE(footer.close_after_primary_action);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     BuildFooterFromModelLivePathSetsSubtitleFromPageHost) {
  FakeLocationBarModel model(GURL("https://example.com/path?q=1"),
                             security_state::SECURE);
  MahoLocationBarUtilityPanelFooter footer =
      internal::BuildFooterFromModel(&model);
  EXPECT_EQ(footer.subtitle, u"example.com");
  EXPECT_EQ(footer.title, u"Secure");
  EXPECT_EQ(footer.sf_symbol_name, "checkmark.shield");
  EXPECT_EQ(footer.data_state, MahoLocationBarUtilityPanelDataState::kLive);
  EXPECT_EQ(footer.security_level,
            static_cast<int>(security_state::SECURE));
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     BuildFooterFromModelEmptyHostFallsBackToPageSecuritySubtitle) {
  FakeLocationBarModel model(GURL("about:blank"), security_state::NONE);
  MahoLocationBarUtilityPanelFooter footer =
      internal::BuildFooterFromModel(&model);
  EXPECT_EQ(footer.subtitle, u"Page security");
  EXPECT_EQ(footer.data_state, MahoLocationBarUtilityPanelDataState::kLive);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     BuildFooterFromModelDangerousPageReflectsHostAndSecurityLevel) {
  FakeLocationBarModel model(GURL("http://evil.example.org/"),
                             security_state::DANGEROUS);
  MahoLocationBarUtilityPanelFooter footer =
      internal::BuildFooterFromModel(&model);
  EXPECT_EQ(footer.subtitle, u"evil.example.org");
  EXPECT_EQ(footer.title, u"Dangerous");
  EXPECT_EQ(footer.sf_symbol_name, "xmark.shield");
  EXPECT_EQ(footer.data_state, MahoLocationBarUtilityPanelDataState::kLive);
  EXPECT_EQ(footer.security_level,
            static_cast<int>(security_state::DANGEROUS));
}

}  // namespace
}  // namespace maho
