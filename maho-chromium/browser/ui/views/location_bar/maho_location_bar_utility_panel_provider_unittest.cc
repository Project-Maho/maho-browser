// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/permissions/permission_decision_auto_blocker_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "chrome/test/base/test_browser_window.h"
#include "chrome/test/base/testing_profile.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/omnibox/browser/location_bar_model.h"
#include "components/permissions/permission_decision_auto_blocker.h"
#include "components/security_state/core/security_state.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension_builder.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/net/maho_ad_block_tab_helper.h"
#include "maho/browser/net/maho_shield_site_state.h"
#include "maho/browser/net/maho_shield_site_state_factory.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
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

void SetUnknownContentBlockingModeForTest(MahoCore* core) {
  char* result = maho_core_handle_event(
      core, R"({"kind":"set_content_blocking_mode","mode":"unknown"})");
  ASSERT_NE(result, nullptr);
  maho_string_free(result);
  ASSERT_EQ(maho::core::GetContentBlockingMode(core), 3);
}

MahoBlockedRequestInfo MakeMainFrameDocumentBlockedRequestInfoForTest(
    content::WebContents* contents) {
  MahoBlockedRequestInfo info;
  info.page_id = contents->GetPrimaryPage().GetMainDocument().GetGlobalId();
  info.destination = network::mojom::RequestDestination::kDocument;
  info.is_outermost_main_frame = true;
  info.request_type = "document";
  return info;
}

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

const MahoUtilityPanelZone* FindZone(
    const MahoLocationBarUtilityPanelModel& model,
    MahoUtilityPanelZoneId id) {
  for (const auto& zone : model.zones) {
    if (zone.id == id) {
      return &zone;
    }
  }
  return nullptr;
}

void ExpectActionsEquivalent(
    const MahoLocationBarUtilityPanelAction& actual,
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
  EXPECT_EQ(actual.toggle_callback.is_null(),
            expected.toggle_callback.is_null());
  EXPECT_EQ(actual.close_after_activate, expected.close_after_activate);
  EXPECT_EQ(actual.callback.is_null(), expected.callback.is_null());
  EXPECT_EQ(actual.anchor_callback.is_null(),
            expected.anchor_callback.is_null());
  EXPECT_EQ(actual.show_disclosure_indicator,
            expected.show_disclosure_indicator);
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
     AutoPipSettingsRowNullContextIsUnavailable) {
  MahoLocationBarUtilityPanelAction row =
      internal::BuildAutoPipSettingsRow(nullptr, nullptr);
  EXPECT_EQ(row.title, u"Automatic Picture-In-Picture");
  EXPECT_EQ(row.subtitle, u"No active page available");
  EXPECT_EQ(row.icon, &maho_lucide_icons::kPictureInPicture2Icon);
  EXPECT_EQ(row.data_state, MahoLocationBarUtilityPanelDataState::kUnavailable);
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

TEST(MahoLocationBarUtilityPanelProviderInternalTest, ToolbarZoneHasNoHeader) {
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
     ExtensionsZoneEmptyRegistryProducesNoExtensionsChip) {
  MahoUtilityPanelZone zone = internal::BuildExtensionsZone(nullptr);
  ASSERT_EQ(zone.items.size(), 1u);
  EXPECT_EQ(zone.items.front().title, u"No extensions");
  EXPECT_TRUE(zone.items.front().subtitle.empty());
  EXPECT_FALSE(zone.items.front().enabled);
  EXPECT_FALSE(zone.items.front().callback);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     SiteControlsZoneHeaderLabelIsSiteControls) {
  ScopedCoreOverride scoped_core(nullptr);
  MahoUtilityPanelZone zone = internal::BuildSettingsZone(nullptr);
  EXPECT_EQ(zone.id, MahoUtilityPanelZoneId::kSettings);
  EXPECT_EQ(zone.style, MahoUtilityPanelZoneStyle::kSettingsList);
  EXPECT_EQ(zone.header_label, u"Site controls");
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest, SettingsZoneRowsInOrder) {
  ScopedCoreOverride scoped_core(nullptr);
  MahoUtilityPanelZone zone = internal::BuildSettingsZone(nullptr);
  ASSERT_EQ(zone.items.size(), 3u);
  EXPECT_EQ(TitlesFrom(zone.items),
            (std::vector<std::u16string>{u"Block Ads & Trackers",
                                         u"Automatic Picture-In-Picture",
                                         u"Site settings"}));
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     SettingsBlockAdsRowIsUnavailableWithoutActiveOrigin) {
  MahoLocationBarUtilityPanelAction row =
      internal::BuildBlockAdsAndTrackersRow(nullptr);
  EXPECT_EQ(row.title, u"Block Ads & Trackers");
  EXPECT_EQ(row.subtitle, u"Current page unavailable");
  EXPECT_EQ(row.icon, &maho_lucide_icons::kShieldHalfIcon);
  EXPECT_EQ(row.data_state, MahoLocationBarUtilityPanelDataState::kUnavailable);
  EXPECT_FALSE(row.enabled);
  EXPECT_FALSE(row.has_toggle);
  EXPECT_FALSE(row.toggle_callback);
  EXPECT_FALSE(row.show_disclosure_indicator);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     NativeShieldRowForOriginUsesLauncherMetadataByDefault) {
  content::BrowserTaskEnvironment task_environment;
  content::RenderViewHostTestEnabler rvh_test_enabler;
  MahoCore* core = maho_core_new();
  ASSERT_TRUE(core);

  {
    ScopedCoreOverride scoped_core(core);
    ASSERT_TRUE(maho::core::SetContentBlockingMode(core, 0));

    TestingProfile profile;
    auto window = std::make_unique<TestBrowserWindow>();
    BrowserWindowCreateParams params(&profile, true);
    params.window = window.release();
    std::unique_ptr<Browser> owned_browser =
        DeprecatedCreateOwnedBrowserWindowForTesting(std::move(params));
    Browser* browser = owned_browser.get();
    ASSERT_NE(browser, nullptr);

    std::unique_ptr<content::WebContents> contents =
        content::WebContentsTester::CreateTestWebContents(&profile, nullptr);
    const std::string origin = "https://example.com";

    MahoLocationBarUtilityPanelAction row =
        internal::BuildBlockAdsAndTrackersRowForOrigin(
            browser, contents->GetWeakPtr(), origin);

    EXPECT_EQ(row.title, u"Block Ads & Trackers");
    EXPECT_EQ(row.subtitle, u"0 blocked · Shields on");
    EXPECT_EQ(row.accessible_name,
              u"Block Ads & Trackers — " + row.subtitle);
    EXPECT_EQ(row.icon, &maho_lucide_icons::kShieldHalfIcon);
    EXPECT_EQ(row.data_state, MahoLocationBarUtilityPanelDataState::kLive);
    EXPECT_TRUE(row.enabled);
    EXPECT_FALSE(row.has_toggle);
    EXPECT_FALSE(row.toggle_is_on);
    EXPECT_TRUE(row.toggle_on_subtitle.empty());
    EXPECT_TRUE(row.toggle_off_subtitle.empty());
    EXPECT_FALSE(row.toggle_callback);
    EXPECT_TRUE(row.close_after_activate);
    EXPECT_FALSE(row.callback);
    EXPECT_TRUE(row.anchor_callback);
    EXPECT_TRUE(row.show_disclosure_indicator);
  }

  maho_core_free(core);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ShieldLauncherRowShowsCountAndOpensBubbleByDefault) {
  content::BrowserTaskEnvironment task_environment;
  content::RenderViewHostTestEnabler rvh_test_enabler;
  MahoCore* core = maho_core_new();
  ASSERT_TRUE(core);

  {
    ScopedCoreOverride scoped_core(core);
    ASSERT_TRUE(maho::core::SetContentBlockingMode(core, 0));

    TestingProfile profile;
    auto window = std::make_unique<TestBrowserWindow>();
    BrowserWindowCreateParams params(&profile, true);
    params.window = window.release();
    std::unique_ptr<Browser> owned_browser =
        DeprecatedCreateOwnedBrowserWindowForTesting(std::move(params));
    Browser* browser = owned_browser.get();
    ASSERT_NE(browser, nullptr);

    const GURL url("https://example.com/");
    chrome::AddTabAt(browser, GURL("about:blank"), -1, true);
    content::WebContents* contents =
        browser->GetTabStripModel()->GetActiveWebContents();
    ASSERT_NE(contents, nullptr);

    MahoAdBlockTabHelper::CreateForWebContents(contents);
    content::NavigationSimulator::NavigateAndCommitFromBrowser(contents, url);
    MahoAdBlockTabHelper* helper =
        MahoAdBlockTabHelper::FromWebContents(contents);
    ASSERT_NE(helper, nullptr);

    for (int i = 0; i < 3; ++i) {
      helper->RecordBlockedRequest(
          MakeMainFrameDocumentBlockedRequestInfoForTest(contents));
    }
    ASSERT_EQ(helper->blocked_count(), 3u);

    MahoLocationBarUtilityPanelAction row =
        internal::BuildBlockAdsAndTrackersRow(browser);

    EXPECT_EQ(row.title, u"Block Ads & Trackers");
    EXPECT_FALSE(row.has_toggle);
    EXPECT_FALSE(row.toggle_callback);
    EXPECT_TRUE(row.close_after_activate);
    EXPECT_FALSE(row.callback);
    EXPECT_TRUE(row.anchor_callback);
    EXPECT_TRUE(row.show_disclosure_indicator);
    EXPECT_EQ(row.subtitle, u"3 blocked · Shields on");
    EXPECT_EQ(row.accessible_name,
              u"Block Ads & Trackers — " + row.subtitle);

    browser->GetTabStripModel()->CloseAllTabs();
  }

  maho_core_free(core);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ShieldLauncherRowShowsAllowedStateWithoutInlineToggle) {
  content::BrowserTaskEnvironment task_environment;
  content::RenderViewHostTestEnabler rvh_test_enabler;
  MahoCore* core = maho_core_new();
  ASSERT_TRUE(core);

  {
    ScopedCoreOverride scoped_core(core);
    ASSERT_TRUE(maho::core::SetContentBlockingMode(core, 0));
    TestingProfile profile;
    auto window = std::make_unique<TestBrowserWindow>();
    BrowserWindowCreateParams params(&profile, true);
    params.window = window.release();
    std::unique_ptr<Browser> owned_browser =
        DeprecatedCreateOwnedBrowserWindowForTesting(std::move(params));
    Browser* browser = owned_browser.get();
    ASSERT_NE(browser, nullptr);

    const GURL url("https://example.com/");
    const std::string origin = "https://example.com";
    MahoShieldSiteState* state =
        MahoShieldSiteStateFactory::GetForProfile(&profile);
    ASSERT_NE(state, nullptr);
    state->AddSiteException(origin);

    chrome::AddTabAt(browser, GURL("about:blank"), -1, true);
    content::WebContents* contents =
        browser->GetTabStripModel()->GetActiveWebContents();
    ASSERT_NE(contents, nullptr);
    content::NavigationSimulator::NavigateAndCommitFromBrowser(contents, url);

    MahoLocationBarUtilityPanelAction row =
        internal::BuildBlockAdsAndTrackersRow(browser);

    EXPECT_EQ(row.title, u"Block Ads & Trackers");
    EXPECT_TRUE(row.enabled);
    EXPECT_FALSE(row.has_toggle);
    EXPECT_FALSE(row.toggle_callback);
    EXPECT_FALSE(row.callback);
    EXPECT_TRUE(row.anchor_callback);
    EXPECT_TRUE(row.show_disclosure_indicator);
    EXPECT_TRUE(row.close_after_activate);
    EXPECT_EQ(row.subtitle, u"0 blocked · Shields off");

    browser->GetTabStripModel()->CloseAllTabs();
  }

  maho_core_free(core);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     ShieldLauncherRowReadsProfileScopedShieldSiteState) {
  content::BrowserTaskEnvironment task_environment;
  content::RenderViewHostTestEnabler rvh_test_enabler;
  MahoCore* core = maho::core::Create();
  ASSERT_TRUE(core);

  {
    ScopedCoreOverride scoped_core(core);
    ASSERT_TRUE(maho::core::SetContentBlockingMode(core, 0));
    TestingProfile profile;
    Profile* otr_profile =
        profile.GetPrimaryOTRProfile(/*create_if_needed=*/true);
    std::unique_ptr<content::WebContents> otr_contents =
        content::WebContentsTester::CreateTestWebContents(otr_profile,
                                                          nullptr);
    MahoShieldSiteState* otr_state =
        MahoShieldSiteStateFactory::GetForProfile(otr_profile);
    ASSERT_NE(otr_state, nullptr);

    const std::string origin = "https://example.com";
    otr_state->AddSiteException(origin);

    MahoLocationBarUtilityPanelAction row =
        internal::BuildBlockAdsAndTrackersRowForOrigin(
            nullptr, otr_contents->GetWeakPtr(), origin);

    EXPECT_EQ(row.subtitle, u"0 blocked · Shields off");
    EXPECT_FALSE(row.enabled);
    EXPECT_FALSE(row.has_toggle);
    EXPECT_FALSE(row.toggle_callback);
    EXPECT_FALSE(row.callback);
    EXPECT_FALSE(row.anchor_callback);
    EXPECT_FALSE(row.show_disclosure_indicator);
    EXPECT_TRUE(row.close_after_activate);
    EXPECT_TRUE(otr_state->IsSiteExceptedForOrigin(origin));
    EXPECT_EQ(maho::core::GetSiteExceptions(core), "[]");
  }

  maho::core::Destroy(core);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     NonNativeModesDoNotMutateSiteExceptions) {
  content::BrowserTaskEnvironment task_environment;
  content::RenderViewHostTestEnabler rvh_test_enabler;
  MahoCore* core = maho_core_new();
  ASSERT_TRUE(core);

  {
    ScopedCoreOverride scoped_core(core);
    TestingProfile profile;
    std::unique_ptr<content::WebContents> contents =
        content::WebContentsTester::CreateTestWebContents(&profile, nullptr);
    const std::string origin = "https://example.com";

    for (int mode : {1, 2}) {
      ASSERT_TRUE(maho::core::SetContentBlockingMode(core, mode));
      const std::string exceptions_before = maho::core::GetSiteExceptions(core);
      MahoLocationBarUtilityPanelAction row =
          internal::BuildBlockAdsAndTrackersRowForOrigin(
              nullptr, contents->GetWeakPtr(), origin);

      EXPECT_EQ(maho::core::GetSiteExceptions(core), exceptions_before);
      EXPECT_FALSE(row.enabled);
      EXPECT_FALSE(row.has_toggle);
      EXPECT_FALSE(row.toggle_callback);
      EXPECT_FALSE(row.callback);
      EXPECT_FALSE(row.anchor_callback);
      EXPECT_FALSE(row.show_disclosure_indicator);
    }

    ASSERT_TRUE(maho::core::SetContentBlockingMode(core, 0));
    SetUnknownContentBlockingModeForTest(core);
    const std::string exceptions_before = maho::core::GetSiteExceptions(core);

    MahoLocationBarUtilityPanelAction row =
        internal::BuildBlockAdsAndTrackersRowForOrigin(
            nullptr, contents->GetWeakPtr(), origin);

    EXPECT_EQ(maho::core::GetSiteExceptions(core), exceptions_before);
    EXPECT_FALSE(row.enabled);
    EXPECT_FALSE(row.has_toggle);
    EXPECT_FALSE(row.toggle_callback);
    EXPECT_FALSE(row.callback);
    EXPECT_FALSE(row.anchor_callback);
    EXPECT_FALSE(row.show_disclosure_indicator);
  }

  maho_core_free(core);
}

TEST(MahoLocationBarUtilityPanelProviderInternalTest,
     SettingsAutoPipRowDelegatesToBuildAutoPipSettingsRow) {
  ScopedCoreOverride scoped_core(nullptr);
  MahoUtilityPanelZone zone = internal::BuildSettingsZone(nullptr);
  ASSERT_EQ(zone.items.size(), 3u);
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
  EXPECT_EQ(settings_zone->header_label, u"Site controls");

  ASSERT_EQ(extensions_zone->items.size(), 1u);
  EXPECT_EQ(extensions_zone->items[0].title, u"No extensions");
  EXPECT_TRUE(extensions_zone->items[0].subtitle.empty());
  EXPECT_FALSE(extensions_zone->items[0].enabled);

  ASSERT_EQ(settings_zone->items.size(), 3u);
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
     BuildFooterFromWebContentsNullIsUnavailableWithPageSecuritySubtitle) {
  MahoLocationBarUtilityPanelFooter footer =
      internal::BuildFooterFromWebContents(nullptr);
  EXPECT_EQ(footer.title, u"Security state unavailable");
  EXPECT_EQ(footer.subtitle, u"Page security");
  EXPECT_EQ(footer.icon, &maho_lucide_icons::kShieldQuestionIcon);
  EXPECT_EQ(footer.data_state,
            MahoLocationBarUtilityPanelDataState::kUnavailable);
  EXPECT_EQ(footer.security_level, static_cast<int>(security_state::NONE));
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
  BrowserWindowCreateParams params(&profile, true);
  params.window = window.release();
  std::unique_ptr<Browser> owned_browser =
        DeprecatedCreateOwnedBrowserWindowForTesting(std::move(params));
    Browser* browser = owned_browser.get();

  auto* registry = extensions::ExtensionRegistry::Get(&profile);
  ASSERT_TRUE(registry);

  // Add dummy extensions
  auto ext1 = extensions::ExtensionBuilder("Alpha").SetID("alpha_id").Build();
  auto ext2 = extensions::ExtensionBuilder("Beta").SetID("beta_id").Build();

  registry->AddEnabled(ext1);
  registry->AddDisabled(ext2);

  MahoUtilityPanelZone zone = internal::BuildExtensionsZone(browser);

  ASSERT_EQ(zone.items.size(), 2u);
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

  // Verify context menu callbacks can be executed without crash (null/dummy
  // anchor checks)
  zone.items[0].context_menu_callback.Run(nullptr);
  zone.items[1].context_menu_callback.Run(nullptr);
}

TEST_F(MahoLocationBarUtilityPanelProviderRegistryTest,
       ExtensionsZoneFiltersByShouldDisplayInExtensionSettings) {
  TestingProfile profile;
  auto window = std::make_unique<TestBrowserWindow>();
  BrowserWindowCreateParams params(&profile, true);
  params.window = window.release();
  std::unique_ptr<Browser> owned_browser =
        DeprecatedCreateOwnedBrowserWindowForTesting(std::move(params));
    Browser* browser = owned_browser.get();

  auto* registry = extensions::ExtensionRegistry::Get(&profile);

  auto ext = extensions::ExtensionBuilder("Internal Component")
                 .SetLocation(extensions::mojom::ManifestLocation::kComponent)
                 .SetID("internal_id")
                 .Build();

  registry->AddEnabled(ext);

  MahoUtilityPanelZone zone = internal::BuildExtensionsZone(browser);
  ASSERT_EQ(zone.items.size(), 1u);
  EXPECT_EQ(zone.items[0].title, u"No extensions");
}

TEST_F(MahoLocationBarUtilityPanelProviderRegistryTest,
       ExtensionsZoneHeaderTrailingActionIsManage) {
  TestingProfile profile;
  auto window = std::make_unique<TestBrowserWindow>();
  BrowserWindowCreateParams params(&profile, true);
  params.window = window.release();
  std::unique_ptr<Browser> owned_browser =
        DeprecatedCreateOwnedBrowserWindowForTesting(std::move(params));
    Browser* browser = owned_browser.get();

  MahoUtilityPanelZone zone = internal::BuildExtensionsZone(browser);
  ASSERT_TRUE(zone.header_trailing_action.has_value());
  EXPECT_EQ(zone.header_trailing_action->title, u"Manage");
  EXPECT_TRUE(zone.header_trailing_action->enabled);
  EXPECT_TRUE(zone.header_trailing_action->callback);

  // Add a dummy tab first
  chrome::AddTabAt(browser, GURL("about:blank"), -1, true);

  zone.header_trailing_action->callback.Run();

  EXPECT_EQ(browser->GetTabStripModel()->count(), 2);
  EXPECT_EQ(browser->GetTabStripModel()
                ->GetActiveWebContents()
                ->GetVisibleURL()
                .spec(),
            "chrome://extensions/");

  // Close all tabs before browser destruction to avoid crash
  browser->GetTabStripModel()->CloseAllTabs();
}

TEST_F(MahoLocationBarUtilityPanelProviderRegistryTest,
       ExtensionsZonePinActionPersistsToolbarVisibility) {
  TestingProfile profile;
  auto window = std::make_unique<TestBrowserWindow>();
  BrowserWindowCreateParams params(&profile, true);
  params.window = window.release();
  std::unique_ptr<Browser> owned_browser =
        DeprecatedCreateOwnedBrowserWindowForTesting(std::move(params));
    Browser* browser = owned_browser.get();

  auto* registry = extensions::ExtensionRegistry::Get(&profile);
  ASSERT_TRUE(registry);
  auto extension =
      extensions::ExtensionBuilder("Alpha").SetID("alpha_id").Build();
  registry->AddEnabled(extension);

  auto* toolbar_model = ToolbarActionsModel::Get(&profile);
  ASSERT_TRUE(toolbar_model);

  MahoUtilityPanelZone zone = internal::BuildExtensionsZone(browser);
  ASSERT_EQ(zone.items.size(), 1u);
  EXPECT_FALSE(zone.items[0].is_pinned);
  EXPECT_TRUE(zone.items[0].pin_enabled);
  ASSERT_TRUE(zone.items[0].pin_callback);

  zone.items[0].pin_callback.Run(true);

  EXPECT_TRUE(toolbar_model->IsActionPinned("alpha_id"));
  EXPECT_TRUE(
      internal::BuildExtensionsZone(browser).items[0].is_pinned);

  browser->GetTabStripModel()->CloseAllTabs();
}

TEST_F(MahoLocationBarUtilityPanelProviderRegistryTest,
       ShieldLauncherRowDoesNotInstallReloadingToggle) {
  MahoCore* core = maho_core_new();
  ASSERT_TRUE(core);

  {
    ScopedCoreOverride scoped_core(core);
    ASSERT_TRUE(maho::core::SetContentBlockingMode(core, 0));
    TestingProfile profile;
    auto window = std::make_unique<TestBrowserWindow>();
    BrowserWindowCreateParams params(&profile, true);
    params.window = window.release();
    std::unique_ptr<Browser> owned_browser =
        DeprecatedCreateOwnedBrowserWindowForTesting(std::move(params));
    Browser* browser = owned_browser.get();
    ASSERT_NE(browser, nullptr);

    const GURL captured_url("https://captured.example/");
    std::unique_ptr<content::WebContents> captured_contents =
        content::WebContentsTester::CreateTestWebContents(&profile, nullptr);
    content::WebContentsTester::For(captured_contents.get())
        ->NavigateAndCommit(captured_url);

    MahoLocationBarUtilityPanelAction row =
        internal::BuildBlockAdsAndTrackersRowForOrigin(
            browser, captured_contents->GetWeakPtr(), captured_url.spec());
    EXPECT_FALSE(row.has_toggle);
    EXPECT_FALSE(row.toggle_callback);
    EXPECT_FALSE(row.callback);
    EXPECT_TRUE(row.anchor_callback);
    EXPECT_TRUE(row.show_disclosure_indicator);
    EXPECT_TRUE(row.close_after_activate);

    const GURL later_url("https://later.example/");
    std::unique_ptr<content::WebContents> later_contents =
        content::WebContentsTester::CreateTestWebContents(&profile, nullptr);
    content::WebContentsTester::For(later_contents.get())
        ->NavigateAndCommit(later_url);

    ASSERT_EQ(captured_contents->GetController().GetPendingEntry(), nullptr);
    ASSERT_EQ(later_contents->GetController().GetPendingEntry(), nullptr);
  }

  maho_core_free(core);
}

TEST_F(MahoLocationBarUtilityPanelProviderRegistryTest,
       ExpiredShieldTargetDoesNotFallbackToBrowserProfile) {
  MahoCore* core = maho_core_new();
  ASSERT_TRUE(core);

  {
    ScopedCoreOverride scoped_core(core);
    ASSERT_TRUE(maho::core::SetContentBlockingMode(core, 0));
    TestingProfile profile;
    auto window = std::make_unique<TestBrowserWindow>();
    BrowserWindowCreateParams params(&profile, true);
    params.window = window.release();
    std::unique_ptr<Browser> owned_browser =
        DeprecatedCreateOwnedBrowserWindowForTesting(std::move(params));
    Browser* browser = owned_browser.get();
    ASSERT_NE(browser, nullptr);

    std::unique_ptr<content::WebContents> captured_contents =
        content::WebContentsTester::CreateTestWebContents(&profile, nullptr);
    base::WeakPtr<content::WebContents> expired_target =
        captured_contents->GetWeakPtr();
    captured_contents.reset();

    chrome::AddTabAt(browser, GURL("https://later.example/"), -1, true);
    MahoLocationBarUtilityPanelAction row =
        internal::BuildBlockAdsAndTrackersRowForOrigin(
            browser, expired_target, "https://captured.example");

    EXPECT_FALSE(row.enabled);
    EXPECT_EQ(row.subtitle, u"Content blocker unavailable");
    EXPECT_FALSE(row.callback);
    EXPECT_FALSE(row.anchor_callback);
    EXPECT_FALSE(row.show_disclosure_indicator);
    browser->GetTabStripModel()->CloseAllTabs();
  }

  maho_core_free(core);
}

TEST_F(MahoLocationBarUtilityPanelProviderRegistryTest,
       NonNativeRowsNeverExposeSiteExceptionToggle) {
  MahoCore* core = maho_core_new();
  ASSERT_TRUE(core);

  {
    ScopedCoreOverride scoped_core(core);
    TestingProfile profile;
    auto window = std::make_unique<TestBrowserWindow>();
    BrowserWindowCreateParams params(&profile, true);
    params.window = window.release();
    std::unique_ptr<Browser> owned_browser =
        DeprecatedCreateOwnedBrowserWindowForTesting(std::move(params));
    Browser* browser = owned_browser.get();
    ASSERT_NE(browser, nullptr);
    std::unique_ptr<content::WebContents> contents =
        content::WebContentsTester::CreateTestWebContents(&profile, nullptr);
    const std::string origin = "https://example.com/";

    for (const auto& [mode, subtitle] :
         std::vector<std::pair<int, std::u16string>>{
             {1, u"Managed by extension"}, {2, u"Content blocking off"}}) {
      ASSERT_TRUE(maho::core::SetContentBlockingMode(core, mode));
      MahoLocationBarUtilityPanelAction row =
          internal::BuildBlockAdsAndTrackersRowForOrigin(
              browser, contents->GetWeakPtr(), origin);
      EXPECT_EQ(row.subtitle, subtitle);
      EXPECT_EQ(row.data_state,
                MahoLocationBarUtilityPanelDataState::kUnavailable);
      EXPECT_FALSE(row.enabled);
      EXPECT_FALSE(row.has_toggle);
      EXPECT_FALSE(row.toggle_callback);
      EXPECT_FALSE(row.anchor_callback);
      EXPECT_FALSE(row.show_disclosure_indicator);
    }

    ASSERT_TRUE(maho::core::SetContentBlockingMode(core, 0));
    SetUnknownContentBlockingModeForTest(core);
    MahoLocationBarUtilityPanelAction unknown_row =
        internal::BuildBlockAdsAndTrackersRowForOrigin(
            browser, contents->GetWeakPtr(), origin);
    EXPECT_EQ(unknown_row.subtitle, u"Content blocker unavailable");
    EXPECT_EQ(unknown_row.data_state,
              MahoLocationBarUtilityPanelDataState::kUnavailable);
    EXPECT_FALSE(unknown_row.enabled);
    EXPECT_FALSE(unknown_row.has_toggle);
    EXPECT_FALSE(unknown_row.toggle_callback);
    EXPECT_FALSE(unknown_row.anchor_callback);
    EXPECT_FALSE(unknown_row.show_disclosure_indicator);
  }

  maho_core_free(core);
}

class MahoLocationBarUtilityPanelProviderAutoPipTest : public ::testing::Test {
 protected:
  MahoLocationBarUtilityPanelProviderAutoPipTest() = default;
  ~MahoLocationBarUtilityPanelProviderAutoPipTest() override = default;

  ContentSetting GetAutoPipSetting(Profile* profile, const GURL& url) {
    HostContentSettingsMap* map =
        HostContentSettingsMapFactory::GetForProfile(profile);
    return map->GetContentSetting(url, GURL(),
                                  ContentSettingsType::AUTO_PICTURE_IN_PICTURE);
  }

  content::BrowserTaskEnvironment task_environment_;
  content::RenderViewHostTestEnabler rvh_test_enabler_;
};

TEST_F(MahoLocationBarUtilityPanelProviderAutoPipTest,
       SetAutoPipEnabledForOriginTrueStoresAllow) {
  TestingProfile profile;
  const GURL url("https://example.com/");

  bool out_on = false;
  std::u16string out_sub;
  internal::SetAutoPipEnabledForOrigin(url, &profile, /*enabled=*/true, out_on,
                                       out_sub);
  EXPECT_EQ(GetAutoPipSetting(&profile, url), CONTENT_SETTING_ALLOW);
}

TEST_F(MahoLocationBarUtilityPanelProviderAutoPipTest,
       SetAutoPipEnabledForOriginFalseResetsToDefault) {
  TestingProfile profile;
  const GURL url("https://example.com/");

  bool out_on = false;
  std::u16string out_sub;
  internal::SetAutoPipEnabledForOrigin(url, &profile, /*enabled=*/true, out_on,
                                       out_sub);
  ASSERT_EQ(GetAutoPipSetting(&profile, url), CONTENT_SETTING_ALLOW);

  internal::SetAutoPipEnabledForOrigin(url, &profile, /*enabled=*/false, out_on,
                                       out_sub);
  EXPECT_EQ(GetAutoPipSetting(&profile, url), CONTENT_SETTING_ASK);
}

TEST_F(MahoLocationBarUtilityPanelProviderAutoPipTest,
       SetAutoPipEnabledForOriginIsSiteScoped) {
  TestingProfile profile;
  const GURL url_a("https://a.example.com/");
  const GURL url_b("https://b.example.com/");

  bool out_on = false;
  std::u16string out_sub;
  internal::SetAutoPipEnabledForOrigin(url_a, &profile, /*enabled=*/true,
                                       out_on, out_sub);
  EXPECT_EQ(GetAutoPipSetting(&profile, url_a), CONTENT_SETTING_ALLOW);
  EXPECT_EQ(GetAutoPipSetting(&profile, url_b), CONTENT_SETTING_ASK);
}

TEST_F(MahoLocationBarUtilityPanelProviderAutoPipTest,
       SetAutoPipEnabledForOriginNullProfileIsNoOp) {
  const GURL url("https://example.com/");
  bool out_on = false;
  std::u16string out_sub;
  internal::SetAutoPipEnabledForOrigin(url, nullptr, /*enabled=*/true, out_on,
                                       out_sub);
}

TEST_F(MahoLocationBarUtilityPanelProviderAutoPipTest,
       SetAutoPipEnabledForOriginInvalidUrlIsNoOp) {
  TestingProfile profile;
  const GURL empty;
  bool out_on = false;
  std::u16string out_sub;
  internal::SetAutoPipEnabledForOrigin(empty, &profile, /*enabled=*/true,
                                       out_on, out_sub);
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

  bool out_on = false;
  std::u16string out_sub;
  internal::SetAutoPipEnabledForOrigin(url, &profile, /*enabled=*/true, out_on,
                                       out_sub);

  EXPECT_FALSE(auto_blocker->IsEmbargoed(
      url, ContentSettingsType::AUTO_PICTURE_IN_PICTURE));
  EXPECT_EQ(GetAutoPipSetting(&profile, url), CONTENT_SETTING_ALLOW);
}

}  // namespace
}  // namespace maho
