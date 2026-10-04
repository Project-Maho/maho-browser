// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/site_control/maho_page_info_ui.h"

#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace {

class MahoPageInfoUITest : public testing::Test {
 protected:
  content::BrowserTaskEnvironment task_environment_;
};

TEST_F(MahoPageInfoUITest, ShowShieldBubbleNullBrowserIsNoOp) {
  MahoPageInfoUI::ShowShieldBubble(nullptr);
}

TEST_F(MahoPageInfoUITest, ShowShieldBubbleViewOverloadNullBrowserIsNoOp) {
  MahoPageInfoUI::ShowShieldBubble(nullptr, nullptr, "https://example.com");
}

TEST_F(MahoPageInfoUITest, ShowShieldBubbleViewOverloadNullAnchorViewIsNoOp) {
  MahoPageInfoUI::ShowShieldBubble(nullptr, nullptr, "https://maho.test");
}

TEST_F(MahoPageInfoUITest, ShowSiteSettingsNullBrowserIsNoOp) {
  MahoPageInfoUI::ShowSiteSettings(nullptr, GURL("https://example.com"));
}

TEST_F(MahoPageInfoUITest, ShowSiteSettingsEmptyUrlIsNoOp) {
  MahoPageInfoUI::ShowSiteSettings(nullptr, GURL());
}

TEST_F(MahoPageInfoUITest, ShowSiteSettingsInvalidUrlIsNoOp) {
  MahoPageInfoUI::ShowSiteSettings(nullptr, GURL("not-a-valid-url"));
}

TEST_F(MahoPageInfoUITest, ShowCookiesSettingsNullBrowserIsNoOp) {
  MahoPageInfoUI::ShowCookiesSettings(nullptr);
}

TEST_F(MahoPageInfoUITest, ShowBoostEditorNullBrowserIsNoOp) {
  MahoPageInfoUI::ShowBoostEditor(nullptr);
}

}  // namespace
