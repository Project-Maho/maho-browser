#include <iostream>

#include "maho/browser/ui/webui/maho_webui_private_boundary.h"

#include <memory>
#include <string>
#include <vector>

#include "base/strings/string_util.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/webui_config_map.h"
#include "testing/gtest/include/gtest/gtest.h"

#include "content/public/browser/webui_config.h"

class DummyMahoBoostConfig : public content::WebUIConfig {
 public:
  DummyMahoBoostConfig() : content::WebUIConfig("chrome-untrusted", "maho-boost") {}
  std::unique_ptr<content::WebUIController> CreateWebUIController(
      content::WebUI* web_ui, const GURL& url) override {
    return nullptr;
  }
  bool IsWebUIEnabled(content::BrowserContext* browser_context) override {
    return MahoIsWebUIEnabled(browser_context);
  }
};

class MahoWebUiPrivateBoundaryTest : public ChromeRenderViewHostTestHarness {
 protected:
  void SetUp() override {
    ChromeRenderViewHostTestHarness::SetUp();
    content::WebUIConfigMap::GetInstance().AddUntrustedWebUIConfig(
        std::make_unique<DummyMahoBoostConfig>());
  }

  void TearDown() override {
    content::WebUIConfigMap::GetInstance().RemoveConfig(
        GURL("chrome-untrusted://maho-boost/"));
    ChromeRenderViewHostTestHarness::TearDown();
  }
};

TEST_F(MahoWebUiPrivateBoundaryTest, RegisteredSurfaceInventoryIsComplete) {
  auto& map = content::WebUIConfigMap::GetInstance();
  std::vector<content::WebUIConfigInfo> configs = map.GetWebUIConfigList(profile());
  int maho_count = 0;
  for (const auto& config : configs) {
    if (base::StartsWith(config.origin.host(), "maho-", base::CompareCase::SENSITIVE)) {
      maho_count++;
    }
  }

  // Counted trusted surfaces: maho-ai, maho-live-folders, maho-mail,
  // maho-routines, maho-settings, maho-space-config, maho-space-create,
  // maho-sync, maho-welcome, and maho-test (10), plus the chrome-untrusted
  // maho-boost dummy added in SetUp.
  EXPECT_EQ(maho_count, 11);
}

TEST_F(MahoWebUiPrivateBoundaryTest, ConfigMatrixFailsClosedForAllOtrClasses) {
  // Test regular profile: all Maho WebUIs should be enabled
  {
    auto& map = content::WebUIConfigMap::GetInstance();
    std::vector<content::WebUIConfigInfo> configs = map.GetWebUIConfigList(profile());
    for (const auto& config : configs) {
      if (base::StartsWith(config.origin.host(), "maho-", base::CompareCase::SENSITIVE)) {
        EXPECT_TRUE(config.enabled) << "Failed for host: " << config.origin.host();
      }
    }
  }

  // Test Off-The-Record (OTR) profile
  Profile* otr_profile = profile()->GetPrimaryOTRProfile(true);
  ASSERT_TRUE(otr_profile);
  {
    auto& map = content::WebUIConfigMap::GetInstance();
    std::vector<content::WebUIConfigInfo> configs = map.GetWebUIConfigList(otr_profile);
    for (const auto& config : configs) {
      if (base::StartsWith(config.origin.host(), "maho-", base::CompareCase::SENSITIVE)) {
        EXPECT_FALSE(config.enabled) << "Failed for host: " << config.origin.host();
      }
    }
  }
}
