// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/side_panel/maho_extension_side_panel_coordinator.h"

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/browser_task_environment.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension_builder.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

class ScopedCoreOverride {
 public:
  explicit ScopedCoreOverride(MahoCore* core) : saved_(GetCore()), core_(core) {
    SetCore(core);
  }
  ~ScopedCoreOverride() {
    SetCore(saved_);
    maho_core_free(core_);
  }

 private:
  raw_ptr<MahoCore> saved_;
  raw_ptr<MahoCore> core_;
};

class MahoExtensionSidePanelCoordinatorTest : public testing::Test {
 protected:
  content::BrowserTaskEnvironment task_environment_;
};

TEST_F(MahoExtensionSidePanelCoordinatorTest, GetForProfileNullForNullProfile) {
  EXPECT_EQ(MahoExtensionSidePanelCoordinator::GetForProfile(nullptr), nullptr);
}

TEST_F(MahoExtensionSidePanelCoordinatorTest, GetForProfileCreatesServiceForRegularProfile) {
  TestingProfile profile;
  EXPECT_NE(MahoExtensionSidePanelCoordinator::GetForProfile(&profile), nullptr);
}

TEST_F(MahoExtensionSidePanelCoordinatorTest, GetForProfileReturnsSameServiceForProfile) {
  TestingProfile profile;
  auto* first = MahoExtensionSidePanelCoordinator::GetForProfile(&profile);
  auto* second = MahoExtensionSidePanelCoordinator::GetForProfile(&profile);
  EXPECT_NE(first, nullptr);
  EXPECT_EQ(first, second);
}

TEST_F(MahoExtensionSidePanelCoordinatorTest, GetForProfileCreatesDistinctServicesForProfiles) {
  TestingProfile profile_a;
  TestingProfile profile_b;
  auto* first = MahoExtensionSidePanelCoordinator::GetForProfile(&profile_a);
  auto* second = MahoExtensionSidePanelCoordinator::GetForProfile(&profile_b);
  EXPECT_NE(first, nullptr);
  EXPECT_NE(second, nullptr);
  EXPECT_NE(first, second);
}

TEST_F(MahoExtensionSidePanelCoordinatorTest, OnPanelOptionsChangedRegistersSidePanel) {
  MahoCore* core = maho_core_new();
  ASSERT_NE(core, nullptr);
  ScopedCoreOverride scoped_core(core);

  TestingProfile profile;
  auto coordinator = std::make_unique<MahoExtensionSidePanelCoordinator>(&profile);

  // Trigger option change: register side panel
  extensions::api::side_panel::PanelOptions options;
  options.path = "panel.html";
  options.enabled = true;
  coordinator->OnPanelOptionsChanged("test_ext_id", options);

  char* out_path = nullptr;
  char* out_layout = nullptr;
  int32_t out_width = 0;
  bool registered = maho_core_get_side_panel_options(core, "test_ext_id", &out_path, &out_layout, &out_width);

  EXPECT_TRUE(registered);
  EXPECT_STREQ(out_path, "panel.html");
  EXPECT_STREQ(out_layout, "right");
  EXPECT_EQ(out_width, 320);

  if (out_path) maho_string_free(out_path);
  if (out_layout) maho_string_free(out_layout);

  // Trigger option change: disable side panel
  options.enabled = false;
  coordinator->OnPanelOptionsChanged("test_ext_id", options);

  registered = maho_core_get_side_panel_options(core, "test_ext_id", nullptr, nullptr, nullptr);
  EXPECT_FALSE(registered);

}

}  // namespace
}  // namespace maho
