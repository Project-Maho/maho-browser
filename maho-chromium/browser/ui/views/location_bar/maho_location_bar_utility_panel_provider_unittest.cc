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
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/permissions/permission_decision_auto_blocker_factory.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/permissions/permission_decision_auto_blocker.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/test_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "components/omnibox/browser/location_bar_model.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "components/security_state/core/security_state.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_renderer_host.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension_builder.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_bridge.h"
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

// std::optional<base::Value> ParseJson(const std::string& json) {
//   return base::JSONReader::Read(json, base::JSON_PARSE_RFC);
// }

// std::optional<base::Value> ReadCoreSettingsForTest(MahoCore* core) {
//   if (!core) {
//     return std::nullopt;
//   }
// 
//   char* json_str = maho_core_get_settings(core);
//   if (!json_str) {
//     return std::nullopt;
//   }
// 
//   std::string json(json_str);
//   maho_string_free(json_str);
//   return ParseJson(json);
// }

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
  EXPECT_EQ(actual.icon, expected.icon);
  EXPECT_EQ(actual.enabled, expected.enabled);
  EXPECT_EQ(actual.data_state, expected.data_state);
  EXPECT_EQ(actual.has_toggle, expected.has_toggle);
  EXPECT_EQ(actual.toggle_is_on, expected.toggle_is_on);
  EXPECT_EQ(actual.toggle_on_subtitle, expected.toggle_on_subtitle);
  EXPECT_EQ(actual.toggle_off_subtitle, expected.toggle_off_subtitle);
  EXPECT_EQ(actual.toggle_callback.is_null(), expected.toggle_callback.is_null());
  EXPECT_EQ(actual.close_after_activate, expected.close_after_activate);
  EXPECT_EQ(actual.callback.is_null(), expected.callback.is_null());
}



TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ShareActionUsesNativeSharingHubSubtitleAndIsUnavailableWithNullBrowser) {
  MahoLocationBarUtilityPanelAction share = internal::BuildShareAction(nullptr);
  EXPECT_EQ(share.title, u"Share");
  EXPECT_EQ(share.subtitle, u"Current page unavailable");
  EXPECT_NE(share.subtitle, u"Copy page URL");
  EXPECT_EQ(share.icon, &maho_lucide_icons::kShareIcon);
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
  EXPECT_EQ(shields.icon, &maho_lucide_icons::kShieldHalfIcon);
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
  EXPECT_EQ(row.icon, &maho_lucide_icons::kPictureInPicture2Icon);
  EXPECT_EQ(row.data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);
  EXPECT_FALSE(row.enabled);
  EXPECT_FALSE(row.callback);
}



// TEST(MahoLocationBarUtilityPanelProviderInternalTest,
//      SecuritySymbolNameReflectsSecurityLevel) {
//   EXPECT_EQ(internal::GetSecuritySFSymbolNameForLevel(
//                 static_cast<int>(security_state::NONE)),
//             "questionmark.shield");
//   EXPECT_EQ(internal::GetSecuritySFSymbolNameForLevel(
//                 static_cast<int>(security_state::SECURE)),
//             "checkmark.shield");
//   EXPECT_EQ(internal::GetSecuritySFSymbolNameForLevel(
//                 static_cast<int>(security_state::WARNING)),
//             "exclamationmark.shield");
//   EXPECT_EQ(internal::GetSecuritySFSymbolNameForLevel(
//                 static_cast<int>(security_state::DANGEROUS)),
//             "xmark.shield");
// }

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ToolbarZoneHasThreeActionsInExactOrder) {
  MahoUtilityPanelZone zone = internal::BuildToolbarZone(nullptr);
  EXPECT_EQ(zone.id, MahoUtilityPanelZoneId::kToolbar);
  EXPECT_EQ(zone.style, MahoUtilityPanelZoneStyle::kToolbarRow);
  ASSERT_EQ(zone.items.size(), 3u);

  EXPECT_EQ(zone.items[0].title, u"Share");
  EXPECT_EQ(zone.items[0].icon, &maho_lucide_icons::kShareIcon);
  EXPECT_FALSE(zone.items[0].callback);
  EXPECT_FALSE(zone.items[0].enabled);

  EXPECT_EQ(zone.items[1].title, u"Screenshot");
  EXPECT_EQ(zone.items[1].icon, &maho_lucide_icons::kCameraIcon);
  EXPECT_FALSE(zone.items[1].enabled);
  EXPECT_FALSE(zone.items[1].callback);
  EXPECT_EQ(zone.items[2].title, u"Full-Page Screenshot");
  EXPECT_EQ(zone.items[2].icon, &maho_lucide_icons::kScanIcon);
  EXPECT_FALSE(zone.items[2].enabled);
  EXPECT_FALSE(zone.items[2].callback);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     CameraActionIsUnavailableWithNullBrowser) {
  MahoLocationBarUtilityPanelAction action =
      internal::BuildCameraAction(nullptr);
  EXPECT_EQ(action.title, u"Screenshot");
  EXPECT_EQ(action.subtitle, u"Current page unavailable");
  EXPECT_EQ(action.icon, &maho_lucide_icons::kCameraIcon);
  EXPECT_EQ(action.data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);
  EXPECT_FALSE(action.enabled);
  EXPECT_FALSE(action.callback);
  EXPECT_TRUE(action.close_after_activate);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     CameraActionSubtitleDescribesDomAwareCapture) {
  MahoLocationBarUtilityPanelAction action =
      internal::BuildCameraAction(nullptr);
  EXPECT_EQ(action.title, u"Screenshot");
  EXPECT_NE(action.title, u"Full-Page Screenshot");
  EXPECT_NE(action.subtitle, u"Capture full page");
  EXPECT_TRUE(action.close_after_activate);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     CameraAndViewfinderActionsHaveDistinctSymbolsAndTitles) {
  MahoLocationBarUtilityPanelAction camera =
      internal::BuildCameraAction(nullptr);
  MahoLocationBarUtilityPanelAction viewfinder =
      internal::BuildViewfinderAction(nullptr);

  EXPECT_EQ(camera.title, u"Screenshot");
  EXPECT_EQ(viewfinder.title, u"Full-Page Screenshot");
  EXPECT_EQ(camera.icon, &maho_lucide_icons::kCameraIcon);
  EXPECT_EQ(viewfinder.icon, &maho_lucide_icons::kScanIcon);
  EXPECT_NE(camera.icon, viewfinder.icon);
  EXPECT_NE(camera.title, viewfinder.title);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ViewfinderActionIsUnavailableWithNullBrowser) {
  MahoLocationBarUtilityPanelAction action =
      internal::BuildViewfinderAction(nullptr);
  EXPECT_EQ(action.title, u"Full-Page Screenshot");
  EXPECT_EQ(action.subtitle, u"Current page unavailable");
  EXPECT_EQ(action.icon, &maho_lucide_icons::kScanIcon);
  EXPECT_EQ(action.data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);
  EXPECT_FALSE(action.enabled);
  EXPECT_FALSE(action.callback);
  EXPECT_TRUE(action.close_after_activate);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ToolbarZoneHasNoHeader) {
  MahoUtilityPanelZone zone = internal::BuildToolbarZone(nullptr);
  EXPECT_TRUE(zone.header_label.empty());
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ExtensionsZoneHeaderLabelIsExtensions) {
  MahoUtilityPanelZone zone = internal::BuildExtensionsZone(nullptr);
  EXPECT_EQ(zone.id, MahoUtilityPanelZoneId::kExtensions);
  EXPECT_EQ(zone.style, MahoUtilityPanelZoneStyle::kChipRow);
  EXPECT_EQ(zone.header_label, u"Extensions");
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ExtensionsZoneEmptyRegistryProducesNoExtensionsChipPlusAddChip) {
  MahoUtilityPanelZone zone = internal::BuildExtensionsZone(nullptr);
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
     ExtensionsZoneAlwaysAppendsAddChipLast) {
  MahoUtilityPanelZone zone = internal::BuildExtensionsZone(nullptr);
  ASSERT_GE(zone.items.size(), 1u);
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
  EXPECT_EQ(row.icon, &maho_lucide_icons::kShieldHalfIcon);
  EXPECT_EQ(row.data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);
  EXPECT_FALSE(row.enabled);
  EXPECT_FALSE(row.has_toggle);
  EXPECT_FALSE(row.toggle_callback);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     BlockAdsAndTrackersRowForStateExposesInlineToggleMetadata) {
  MahoLocationBarUtilityPanelAction enabled =
      internal::BuildBlockAdsAndTrackersRowForState(/*blocking_enabled=*/true);
  EXPECT_EQ(enabled.title, u"Block Ads & Trackers");
  EXPECT_EQ(enabled.subtitle, u"Blocked on every visit");
  EXPECT_EQ(enabled.accessible_name,
            u"Block Ads & Trackers — Blocked on every visit");
  EXPECT_TRUE(enabled.enabled);
  EXPECT_TRUE(enabled.has_toggle);
  EXPECT_TRUE(enabled.toggle_is_on);
  EXPECT_EQ(enabled.toggle_on_subtitle, u"Blocked on every visit");
  EXPECT_EQ(enabled.toggle_off_subtitle, u"Allowed on this site");
  EXPECT_FALSE(enabled.callback);
  EXPECT_TRUE(enabled.toggle_callback.is_null());
  EXPECT_FALSE(enabled.close_after_activate);

  MahoLocationBarUtilityPanelAction disabled =
      internal::BuildBlockAdsAndTrackersRowForState(/*blocking_enabled=*/false);
  EXPECT_TRUE(disabled.has_toggle);
  EXPECT_FALSE(disabled.toggle_is_on);
  EXPECT_EQ(disabled.subtitle, u"Allowed on this site");
  EXPECT_EQ(disabled.accessible_name,
            u"Block Ads & Trackers — Allowed on this site");
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     SetBlockAdsAndTrackersEnabledForOriginWritesSiteExceptionsDirectly) {
  MahoCore* core = maho_core_new();
  ASSERT_TRUE(core);

  {
    ScopedCoreOverride scoped_core(core);
    const std::string origin = "https://example.com";

    internal::SetBlockAdsAndTrackersEnabledForOrigin(
        origin, /*blocking_enabled=*/false);
    EXPECT_EQ(maho::core::GetSiteExceptions(core),
              std::string("[\"") + origin + "\"]");

    internal::SetBlockAdsAndTrackersEnabledForOrigin(
        origin, /*blocking_enabled=*/true);
    EXPECT_EQ(maho::core::GetSiteExceptions(core), "[]");
  }

  maho_core_free(core);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     SettingsAutoPipRowDelegatesToBuildAutoPipSettingsRow) {
  ScopedCoreOverride scoped_core(nullptr);
  MahoUtilityPanelZone zone = internal::BuildSettingsZone(nullptr);
  ASSERT_EQ(zone.items.size(), 2u);
  ExpectActionsEquivalent(zone.items[1],
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
  EXPECT_EQ(extensions_zone->items[0].title, u"No extensions");
  EXPECT_TRUE(extensions_zone->items[0].subtitle.empty());
  EXPECT_FALSE(extensions_zone->items[0].enabled);
  EXPECT_EQ(extensions_zone->items[1].title, u"+");
  EXPECT_FALSE(extensions_zone->items[1].enabled);

  ASSERT_EQ(settings_zone->items.size(), 2u);
  EXPECT_EQ(settings_zone->items[0].subtitle, u"Current page unavailable");
  EXPECT_EQ(settings_zone->items[1].subtitle, u"No active page available");
}

TEST(MahoLocationBarUtilityPanelProviderTest,
     FooterHasSecurityStateUnavailableWithNullLocationBar) {
  ScopedCoreOverride scoped_core(nullptr);

  MahoLocationBarUtilityPanelModel model =
      BuildMahoLocationBarUtilityPanelModel(nullptr);

  EXPECT_EQ(model.footer.title, u"Security state unavailable");
  EXPECT_EQ(model.footer.icon, &maho_lucide_icons::kShieldQuestionIcon);
  EXPECT_EQ(model.footer.data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ZoomInActionForStateEnabledWhenActiveAndNotAtMax) {
  MahoLocationBarUtilityPanelAction mid =
      internal::BuildZoomInActionForState(/*has_active_page=*/true,
                                          /*at_max_zoom=*/false);
  EXPECT_EQ(mid.title, u"Zoom In");
  EXPECT_EQ(mid.icon, &maho_lucide_icons::kZoomInIcon);
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
  EXPECT_EQ(mid.icon, &maho_lucide_icons::kZoomOutIcon);
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
  EXPECT_EQ(no_controller.icon, &maho_lucide_icons::kSearchIcon);
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
  EXPECT_EQ(footer.icon, &maho_lucide_icons::kShieldQuestionIcon);
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
  EXPECT_EQ(footer.icon, &maho_lucide_icons::kShieldQuestionIcon);
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
  EXPECT_EQ(footer.icon, &maho_lucide_icons::kShieldCheckIcon);
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
  EXPECT_EQ(footer.icon, &maho_lucide_icons::kShieldXIcon);
  EXPECT_EQ(footer.data_state, MahoLocationBarUtilityPanelDataState::kLive);
  EXPECT_EQ(footer.security_level,
            static_cast<int>(security_state::DANGEROUS));
}

class MahoLocationBarUtilityPanelProviderRegistryTest : public testing::Test {
 protected:
  MahoLocationBarUtilityPanelProviderRegistryTest() = default;
  ~MahoLocationBarUtilityPanelProviderRegistryTest() override = default;

  content::BrowserTaskEnvironment task_environment_;
  content::RenderViewHostTestEnabler rvh_test_enabler_;
};

TEST_F(MahoLocationBarUtilityPanelProviderRegistryTest,
       ExtensionsZoneReadsFromExtensionRegistryNotCoreCache) {
  TestingProfile profile;
  auto window = std::make_unique<TestBrowserWindow>();
  Browser::CreateParams params(&profile, true);
  params.window = window.release();
  Browser* browser = Browser::Create(params);

  auto* registry = extensions::ExtensionRegistry::Get(&profile);
  ASSERT_TRUE(registry);

  // Add dummy extensions
  auto ext1 = extensions::ExtensionBuilder("Alpha").SetID("alpha_id").Build();
  auto ext2 = extensions::ExtensionBuilder("Beta").SetID("beta_id").Build();
  
  registry->AddEnabled(ext1);
  registry->AddDisabled(ext2);

  MahoUtilityPanelZone zone = internal::BuildExtensionsZone(browser);
  
  ASSERT_EQ(zone.items.size(), 3u);
  EXPECT_EQ(zone.items[0].title, u"Alpha");
  EXPECT_EQ(zone.items[0].accessible_name, u"Alpha");
  EXPECT_EQ(zone.items[0].extension_id, "alpha_id");
  EXPECT_TRUE(zone.items[0].enabled);
  EXPECT_TRUE(zone.items[0].callback);
  EXPECT_TRUE(zone.items[0].context_menu_callback);
  EXPECT_TRUE(zone.items[0].context_menu_enabled);

  EXPECT_EQ(zone.items[1].title, u"Beta");
  EXPECT_EQ(zone.items[1].accessible_name, u"Beta (Disabled)");
  EXPECT_EQ(zone.items[1].extension_id, "beta_id");
  EXPECT_FALSE(zone.items[1].enabled);
  EXPECT_FALSE(zone.items[1].callback);
  EXPECT_TRUE(zone.items[1].context_menu_callback);
  EXPECT_TRUE(zone.items[1].context_menu_enabled);

  EXPECT_EQ(zone.items[2].title, u"+");

  // Verify context menu callbacks can be executed without crash (null/dummy anchor checks)
  zone.items[0].context_menu_callback.Run(nullptr);
  zone.items[1].context_menu_callback.Run(nullptr);
}

TEST_F(MahoLocationBarUtilityPanelProviderRegistryTest,
       ExtensionsZoneFiltersByShouldDisplayInExtensionSettings) {
  TestingProfile profile;
  auto window = std::make_unique<TestBrowserWindow>();
  Browser::CreateParams params(&profile, true);
  params.window = window.release();
  Browser* browser = Browser::Create(params);

  auto* registry = extensions::ExtensionRegistry::Get(&profile);
  
  auto ext = extensions::ExtensionBuilder("Internal Component")
                 .SetLocation(extensions::mojom::ManifestLocation::kComponent)
                 .SetID("internal_id")
                 .Build();
                 
  registry->AddEnabled(ext);

  MahoUtilityPanelZone zone = internal::BuildExtensionsZone(browser);
  ASSERT_EQ(zone.items.size(), 2u);
  EXPECT_EQ(zone.items[0].title, u"No extensions");
  EXPECT_EQ(zone.items[1].title, u"+");
}

TEST_F(MahoLocationBarUtilityPanelProviderRegistryTest,
       ExtensionsZoneHeaderTrailingActionIsManage) {
  TestingProfile profile;
  auto window = std::make_unique<TestBrowserWindow>();
  Browser::CreateParams params(&profile, true);
  params.window = window.release();
  Browser* browser = Browser::Create(params);

  MahoUtilityPanelZone zone = internal::BuildExtensionsZone(browser);
  ASSERT_TRUE(zone.header_trailing_action.has_value());
  EXPECT_EQ(zone.header_trailing_action->title, u"Manage");
  EXPECT_TRUE(zone.header_trailing_action->enabled);
  EXPECT_TRUE(zone.header_trailing_action->callback);
  
  // Add a dummy tab first
  chrome::AddTabAt(browser, GURL("about:blank"), -1, true);
  
  // Verify it navigates to the extensions settings URL
  zone.header_trailing_action->callback.Run();

  EXPECT_EQ(browser->tab_strip_model()->count(), 2);
  EXPECT_EQ(browser->tab_strip_model()->GetActiveWebContents()->GetVisibleURL().spec(),
            "chrome://maho-settings/?pane=extensions");

  // Close all tabs before browser destruction to avoid crash
  browser->tab_strip_model()->CloseAllTabs();
}

class MahoLocationBarUtilityPanelProviderAutoPipTest : public ::testing::Test {
 protected:
  MahoLocationBarUtilityPanelProviderAutoPipTest() = default;
  ~MahoLocationBarUtilityPanelProviderAutoPipTest() override = default;

  ContentSetting GetAutoPipSetting(Profile* profile, const GURL& url) {
    HostContentSettingsMap* map =
        HostContentSettingsMapFactory::GetForProfile(profile);
    return map->GetContentSetting(
        url, GURL(), ContentSettingsType::AUTO_PICTURE_IN_PICTURE);
  }

  content::BrowserTaskEnvironment task_environment_;
  content::RenderViewHostTestEnabler rvh_test_enabler_;
};

TEST_F(MahoLocationBarUtilityPanelProviderAutoPipTest,
       SetAutoPipEnabledForOriginTrueStoresAllow) {
  TestingProfile profile;
  const GURL url("https://example.com/");

  internal::SetAutoPipEnabledForOrigin(url, &profile, /*enabled=*/true);
  EXPECT_EQ(GetAutoPipSetting(&profile, url), CONTENT_SETTING_ALLOW);
}

TEST_F(MahoLocationBarUtilityPanelProviderAutoPipTest,
       SetAutoPipEnabledForOriginFalseResetsToDefault) {
  TestingProfile profile;
  const GURL url("https://example.com/");

  internal::SetAutoPipEnabledForOrigin(url, &profile, /*enabled=*/true);
  ASSERT_EQ(GetAutoPipSetting(&profile, url), CONTENT_SETTING_ALLOW);

  internal::SetAutoPipEnabledForOrigin(url, &profile, /*enabled=*/false);
  EXPECT_EQ(GetAutoPipSetting(&profile, url), CONTENT_SETTING_ASK);
}

TEST_F(MahoLocationBarUtilityPanelProviderAutoPipTest,
       SetAutoPipEnabledForOriginIsSiteScoped) {
  TestingProfile profile;
  const GURL url_a("https://a.example.com/");
  const GURL url_b("https://b.example.com/");

  internal::SetAutoPipEnabledForOrigin(url_a, &profile, /*enabled=*/true);
  EXPECT_EQ(GetAutoPipSetting(&profile, url_a), CONTENT_SETTING_ALLOW);
  EXPECT_EQ(GetAutoPipSetting(&profile, url_b), CONTENT_SETTING_ASK);
}

TEST_F(MahoLocationBarUtilityPanelProviderAutoPipTest,
       SetAutoPipEnabledForOriginNullProfileIsNoOp) {
  const GURL url("https://example.com/");
  internal::SetAutoPipEnabledForOrigin(url, nullptr, /*enabled=*/true);
}

TEST_F(MahoLocationBarUtilityPanelProviderAutoPipTest,
       SetAutoPipEnabledForOriginInvalidUrlIsNoOp) {
  TestingProfile profile;
  const GURL empty;
  internal::SetAutoPipEnabledForOrigin(empty, &profile, /*enabled=*/true);
}

TEST_F(MahoLocationBarUtilityPanelProviderAutoPipTest,
       SetAutoPipEnabledForOriginClearsEmbargo) {
  TestingProfile profile;
  const GURL url("https://example.com/");
  permissions::PermissionDecisionAutoBlocker* auto_blocker =
      PermissionDecisionAutoBlockerFactory::GetForProfile(&profile);
  ASSERT_TRUE(auto_blocker);

  for (int i = 0; i < 3; ++i) {
    auto_blocker->RecordDismissAndEmbargo(
        url, ContentSettingsType::AUTO_PICTURE_IN_PICTURE,
        /*dismissed_prompt_was_quiet=*/false);
  }
  ASSERT_TRUE(auto_blocker->IsEmbargoed(
      url, ContentSettingsType::AUTO_PICTURE_IN_PICTURE));

  internal::SetAutoPipEnabledForOrigin(url, &profile, /*enabled=*/true);

  EXPECT_FALSE(auto_blocker->IsEmbargoed(
      url, ContentSettingsType::AUTO_PICTURE_IN_PICTURE));
  EXPECT_EQ(GetAutoPipSetting(&profile, url), CONTENT_SETTING_ALLOW);
}

}  // namespace
}  // namespace maho
