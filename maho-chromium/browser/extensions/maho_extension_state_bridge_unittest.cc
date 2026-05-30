// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/extensions/maho_extension_state_bridge.h"

#include <memory>
#include <string>

#include "base/json/json_reader.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/browser_task_environment.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/management_policy.h"
#include "extensions/common/extension_builder.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

class ScopedCoreOverride {
 public:
  explicit ScopedCoreOverride(MahoCore* core) : saved_(GetCore()) {
    SetCore(core);
  }
  ~ScopedCoreOverride() { SetCore(saved_); }

 private:
  MahoCore* saved_;
};

class MahoExtensionStateBridgeTest : public testing::Test {
 protected:
  content::BrowserTaskEnvironment task_environment_;
};

TEST_F(MahoExtensionStateBridgeTest, FromProfileNullForNullProfile) {
  EXPECT_EQ(MahoExtensionStateBridge::FromProfile(nullptr), nullptr);
}

TEST_F(MahoExtensionStateBridgeTest, FromProfileNullBeforeConstruction) {
  TestingProfile profile;
  EXPECT_EQ(MahoExtensionStateBridge::FromProfile(&profile), nullptr);
}

TEST_F(MahoExtensionStateBridgeTest, FromProfileReturnsInstanceAfterConstruction) {
  TestingProfile profile;
  auto bridge = std::make_unique<MahoExtensionStateBridge>(&profile);
  EXPECT_EQ(MahoExtensionStateBridge::FromProfile(&profile), bridge.get());
}

TEST_F(MahoExtensionStateBridgeTest, FromProfileReturnsNullAfterDestruction) {
  TestingProfile profile;
  {
    auto bridge = std::make_unique<MahoExtensionStateBridge>(&profile);
    EXPECT_NE(MahoExtensionStateBridge::FromProfile(&profile), nullptr);
  }
  EXPECT_EQ(MahoExtensionStateBridge::FromProfile(&profile), nullptr);
}

TEST_F(MahoExtensionStateBridgeTest, FromProfileNullForIncognito) {
  TestingProfile profile;
  Profile* otr = profile.GetOffTheRecordProfile(
      Profile::OTRProfileID::PrimaryID(), true);
  ASSERT_NE(otr, nullptr);
  EXPECT_EQ(MahoExtensionStateBridge::FromProfile(otr), nullptr);
}

TEST_F(MahoExtensionStateBridgeTest, ConstructionObservesExtensionRegistry) {
  TestingProfile profile;
  auto bridge = std::make_unique<MahoExtensionStateBridge>(&profile);

  auto* registry = extensions::ExtensionRegistry::Get(&profile);
  ASSERT_NE(registry, nullptr);
  EXPECT_TRUE(registry->HasObserver(bridge.get()));
}

TEST_F(MahoExtensionStateBridgeTest,
       PushFullSnapshotPopulatesCoreWithEnabledExtension) {
  MahoCore* core = maho_core_new();
  ASSERT_NE(core, nullptr);
  ScopedCoreOverride scoped_core(core);

  TestingProfile profile;
  auto ext = extensions::ExtensionBuilder("TestExt").SetID("test_ext_id").Build();

  {
    auto bridge = std::make_unique<MahoExtensionStateBridge>(&profile);
    auto* registry = extensions::ExtensionRegistry::Get(&profile);
    registry->AddEnabled(ext);
    registry->TriggerOnLoaded(ext.get());

    char* json_str = maho_core_get_installed_extensions(core);
    ASSERT_NE(json_str, nullptr);
    std::string json(json_str);
    maho_string_free(json_str);

    auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->is_list());

    bool found = false;
    for (const auto& item : parsed->GetList()) {
      const auto* dict = item.GetIfDict();
      if (!dict) continue;
      const std::string* id = dict->FindString("id");
      if (id && *id == "test_ext_id") {
        found = true;
        const auto enabled = dict->FindBool("enabled");
        EXPECT_TRUE(enabled.has_value());
        EXPECT_TRUE(*enabled);
        break;
      }
    }
    EXPECT_TRUE(found) << "Extension 'test_ext_id' not found in core snapshot";
  }

  maho_core_free(core);
}

TEST_F(MahoExtensionStateBridgeTest,
       ObserverNotifiedOnExtensionRegistryChange) {
  TestingProfile profile;

  class TestObserver : public MahoExtensionStateBridge::Observer {
   public:
    int call_count = 0;
    void OnInstalledExtensionsChanged() override { ++call_count; }
  };

  MahoCore* core = maho_core_new();
  ASSERT_NE(core, nullptr);
  ScopedCoreOverride scoped_core(core);

  TestObserver observer;
  auto bridge = std::make_unique<MahoExtensionStateBridge>(&profile);
  bridge->AddObserver(&observer);

  auto ext = extensions::ExtensionBuilder("Foo").SetID("foo_id").Build();
  auto* registry = extensions::ExtensionRegistry::Get(&profile);
  registry->AddEnabled(ext);
  registry->TriggerOnLoaded(ext.get());

  task_environment_.RunUntilIdle();

  EXPECT_GT(observer.call_count, 0);

  bridge->RemoveObserver(&observer);
  maho_core_free(core);
}

TEST_F(MahoExtensionStateBridgeTest,
       ComponentExtensionExcludedNormalExtensionIncluded) {
  MahoCore* core = maho_core_new();
  ASSERT_NE(core, nullptr);
  ScopedCoreOverride scoped_core(core);

  TestingProfile profile;
  auto bridge = std::make_unique<MahoExtensionStateBridge>(&profile);
  auto* registry = extensions::ExtensionRegistry::Get(&profile);

  auto normal_ext =
      extensions::ExtensionBuilder("NormalExt").SetID("normal_ext_id").Build();

  auto component_ext =
      extensions::ExtensionBuilder("ComponentExt")
          .SetLocation(extensions::mojom::ManifestLocation::kComponent)
          .SetID("component_ext_id")
          .Build();

  registry->AddEnabled(normal_ext);
  registry->TriggerOnLoaded(normal_ext.get());
  registry->AddEnabled(component_ext);
  registry->TriggerOnLoaded(component_ext.get());

  char* json_str = maho_core_get_installed_extensions(core);
  ASSERT_NE(json_str, nullptr);
  std::string json(json_str);
  maho_string_free(json_str);

  auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());
  ASSERT_TRUE(parsed->is_list());

  bool normal_found = false;
  bool component_found = false;
  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) continue;
    const std::string* id = dict->FindString("id");
    if (!id) continue;
    if (*id == "normal_ext_id") {
      normal_found = true;
    }
    if (*id == "component_ext_id") {
      component_found = true;
    }
  }

  EXPECT_TRUE(normal_found)
      << "Normal extension 'normal_ext_id' should appear in snapshot";
  EXPECT_FALSE(component_found)
      << "Component extension 'component_ext_id' must be excluded from snapshot";

  maho_core_free(core);
}

TEST_F(MahoExtensionStateBridgeTest,
       RequestEnableReturnsFalseForUnknownExtension) {
  TestingProfile profile;
  auto bridge = std::make_unique<MahoExtensionStateBridge>(&profile);
  EXPECT_FALSE(bridge->RequestEnable("does_not_exist"));
}

TEST_F(MahoExtensionStateBridgeTest,
       RequestDisableReturnsFalseForUnknownExtension) {
  TestingProfile profile;
  auto bridge = std::make_unique<MahoExtensionStateBridge>(&profile);
  EXPECT_FALSE(bridge->RequestDisable("does_not_exist"));
}

TEST_F(MahoExtensionStateBridgeTest,
       RequestUninstallReturnsFalseForUnknownExtension) {
  TestingProfile profile;
  auto bridge = std::make_unique<MahoExtensionStateBridge>(&profile);
  EXPECT_FALSE(bridge->RequestUninstall("does_not_exist"));
}

TEST_F(MahoExtensionStateBridgeTest,
       RequestUninstallReturnsTrueForKnownUnpoliciedExtension) {
  MahoCore* core = maho_core_new();
  ASSERT_NE(core, nullptr);
  ScopedCoreOverride scoped_core(core);

  TestingProfile profile;
  auto bridge = std::make_unique<MahoExtensionStateBridge>(&profile);
  auto* registry = extensions::ExtensionRegistry::Get(&profile);

  auto ext = extensions::ExtensionBuilder("UninstallExt")
                 .SetID("uninstall_ext_id")
                 .Build();
  registry->AddEnabled(ext);
  registry->TriggerOnLoaded(ext.get());

  EXPECT_TRUE(bridge->RequestUninstall("uninstall_ext_id"));

  maho_core_free(core);
}

}  // namespace
}  // namespace maho
