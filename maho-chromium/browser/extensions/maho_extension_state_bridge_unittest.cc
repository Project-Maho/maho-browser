// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/extensions/maho_extension_state_bridge.h"

#include <memory>
#include <string>

#include "base/json/json_reader.h"
#include "base/values.h"
#include "base/command_line.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/browser_task_environment.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/management_policy.h"
#include "extensions/common/extension_builder.h"
#include "chrome/browser/extensions/test_extension_system.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

class TestManagementPolicyProvider : public extensions::ManagementPolicy::Provider {
 public:
  TestManagementPolicyProvider() = default;
  ~TestManagementPolicyProvider() override = default;

  std::string GetDebugPolicyProviderName() const override {
    return "TestManagementPolicyProvider";
  }

  bool UserMayModifySettings(const extensions::Extension* extension,
                             std::u16string* error) const override {
    return user_may_modify_;
  }

  bool MustRemainInstalled(const extensions::Extension* extension,
                           std::u16string* error) const override {
    return must_remain_installed_;
  }

  bool MustRemainDisabled(const extensions::Extension* extension,
                          extensions::disable_reason::DisableReason* reason) const override {
    return must_remain_disabled_;
  }

  bool MustRemainEnabled(const extensions::Extension* extension,
                         std::u16string* error) const override {
    return must_remain_enabled_;
  }

  void SetUserMayModify(bool value) { user_may_modify_ = value; }
  void SetMustRemainInstalled(bool value) { must_remain_installed_ = value; }
  void SetMustRemainDisabled(bool value) { must_remain_disabled_ = value; }
  void SetMustRemainEnabled(bool value) { must_remain_enabled_ = value; }

 private:
  bool user_may_modify_ = true;
  bool must_remain_installed_ = false;
  bool must_remain_disabled_ = false;
  bool must_remain_enabled_ = false;
};

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

class MahoExtensionStateBridgeTest : public testing::Test {
 protected:
  content::BrowserTaskEnvironment task_environment_;
};

TEST_F(MahoExtensionStateBridgeTest,
       FactoryCanBeRegisteredBeforeProfileCreation) {
  EnsureMahoExtensionStateBridgeFactoryBuilt();

  TestingProfile profile;
  EXPECT_NE(MahoExtensionStateBridge::FromProfile(&profile), nullptr);
}

TEST_F(MahoExtensionStateBridgeTest, FromProfileNullForNullProfile) {
  EXPECT_EQ(MahoExtensionStateBridge::FromProfile(nullptr), nullptr);
}

TEST_F(MahoExtensionStateBridgeTest, FromProfileCreatesServiceForRegularProfile) {
  TestingProfile profile;
  EXPECT_NE(MahoExtensionStateBridge::FromProfile(&profile), nullptr);
}

TEST_F(MahoExtensionStateBridgeTest, FromProfileReturnsSameServiceForProfile) {
  TestingProfile profile;
  auto* first = MahoExtensionStateBridge::FromProfile(&profile);
  auto* second = MahoExtensionStateBridge::FromProfile(&profile);
  EXPECT_NE(first, nullptr);
  EXPECT_EQ(first, second);
}

TEST_F(MahoExtensionStateBridgeTest, FromProfileCreatesDistinctServicesForProfiles) {
  TestingProfile profile_a;
  TestingProfile profile_b;
  auto* first = MahoExtensionStateBridge::FromProfile(&profile_a);
  auto* second = MahoExtensionStateBridge::FromProfile(&profile_b);
  EXPECT_NE(first, nullptr);
  EXPECT_NE(second, nullptr);
  EXPECT_NE(first, second);
}

TEST_F(MahoExtensionStateBridgeTest, FromProfileNullForIncognito) {
  TestingProfile profile;
  Profile* otr = profile.GetOffTheRecordProfile(
      Profile::OTRProfileID::PrimaryID(), true);
  ASSERT_NE(otr, nullptr);
  EXPECT_EQ(MahoExtensionStateBridge::FromProfile(otr), nullptr);
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
  bridge.reset();
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

  bridge.reset();
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
       CanUninstallReturnsFalseForUnknownExtension) {
  TestingProfile profile;
  auto bridge = std::make_unique<MahoExtensionStateBridge>(&profile);
  EXPECT_FALSE(bridge->CanUninstall("does_not_exist"));
}

TEST_F(MahoExtensionStateBridgeTest,
       CanUninstallReturnsTrueForKnownUnpoliciedExtension) {
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

  EXPECT_TRUE(bridge->CanUninstall("uninstall_ext_id"));

  bridge.reset();
}

TEST_F(MahoExtensionStateBridgeTest,
       HandleIncomingSyncDeleteSkipsPolicyForcedExtension) {
  MahoCore* core = maho_core_new();
  ASSERT_NE(core, nullptr);
  ScopedCoreOverride scoped_core(core);

  TestingProfile profile;
  extensions::TestExtensionSystem* extension_system =
      static_cast<extensions::TestExtensionSystem*>(
          extensions::ExtensionSystem::Get(&profile));
  extension_system->CreateExtensionService(
      base::CommandLine::ForCurrentProcess(),
      base::FilePath(), false);

  auto bridge = std::make_unique<MahoExtensionStateBridge>(&profile);
  auto* registry = extensions::ExtensionRegistry::Get(&profile);

  auto ext = extensions::ExtensionBuilder("ForcedExt")
                 .SetID("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
                 .Build();
  extensions::ExtensionRegistrar::Get(&profile)->AddExtension(ext);

  TestManagementPolicyProvider policy_provider;
  policy_provider.SetMustRemainInstalled(true);
  extension_system->management_policy()->RegisterProvider(&policy_provider);

  bridge->HandleIncomingSyncForTesting("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", /*enabled=*/false, /*deleted=*/true);

  EXPECT_TRUE(registry->enabled_extensions().Contains("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));

  extension_system->management_policy()->UnregisterProvider(&policy_provider);
  bridge.reset();
}

TEST_F(MahoExtensionStateBridgeTest,
       HandleIncomingSyncDeleteUninstallsNormalExtension) {
  MahoCore* core = maho_core_new();
  ASSERT_NE(core, nullptr);
  ScopedCoreOverride scoped_core(core);

  TestingProfile profile;
  extensions::TestExtensionSystem* extension_system =
      static_cast<extensions::TestExtensionSystem*>(
          extensions::ExtensionSystem::Get(&profile));
  extension_system->CreateExtensionService(
      base::CommandLine::ForCurrentProcess(),
      base::FilePath(), false);

  auto bridge = std::make_unique<MahoExtensionStateBridge>(&profile);
  auto* registry = extensions::ExtensionRegistry::Get(&profile);

  auto ext = extensions::ExtensionBuilder("NormalExt")
                 .SetID("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")
                 .Build();
  extensions::ExtensionRegistrar::Get(&profile)->AddExtension(ext);

  bridge->HandleIncomingSyncForTesting("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", /*enabled=*/false, /*deleted=*/true);

  EXPECT_FALSE(registry->GetExtensionById("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", extensions::ExtensionRegistry::EVERYTHING));

  bridge.reset();
}

TEST_F(MahoExtensionStateBridgeTest,
       RequestEnableReturnsFalseForPolicyForcedExtension) {
  MahoCore* core = maho_core_new();
  ASSERT_NE(core, nullptr);
  ScopedCoreOverride scoped_core(core);

  TestingProfile profile;
  extensions::TestExtensionSystem* extension_system =
      static_cast<extensions::TestExtensionSystem*>(
          extensions::ExtensionSystem::Get(&profile));
  extension_system->CreateExtensionService(
      base::CommandLine::ForCurrentProcess(),
      base::FilePath(), false);

  auto bridge = std::make_unique<MahoExtensionStateBridge>(&profile);

  auto ext = extensions::ExtensionBuilder("ForcedDisabledExt")
                 .SetID("cccccccccccccccccccccccccccccccc")
                 .Build();
  auto* registrar = extensions::ExtensionRegistrar::Get(&profile);
  registrar->AddExtension(ext);
  registrar->DisableExtension("cccccccccccccccccccccccccccccccc", {extensions::disable_reason::DISABLE_USER_ACTION});

  TestManagementPolicyProvider policy_provider;
  policy_provider.SetUserMayModify(false);
  extension_system->management_policy()->RegisterProvider(&policy_provider);

  EXPECT_FALSE(bridge->RequestEnable("cccccccccccccccccccccccccccccccc"));

  extension_system->management_policy()->UnregisterProvider(&policy_provider);
  bridge.reset();
}

TEST_F(MahoExtensionStateBridgeTest,
       RequestDisableReturnsFalseForPolicyForcedExtension) {
  MahoCore* core = maho_core_new();
  ASSERT_NE(core, nullptr);
  ScopedCoreOverride scoped_core(core);

  TestingProfile profile;
  extensions::TestExtensionSystem* extension_system =
      static_cast<extensions::TestExtensionSystem*>(
          extensions::ExtensionSystem::Get(&profile));
  extension_system->CreateExtensionService(
      base::CommandLine::ForCurrentProcess(),
      base::FilePath(), false);

  auto bridge = std::make_unique<MahoExtensionStateBridge>(&profile);

  auto ext = extensions::ExtensionBuilder("ForcedEnabledExt")
                 .SetID("dddddddddddddddddddddddddddddddd")
                 .Build();
  extensions::ExtensionRegistrar::Get(&profile)->AddExtension(ext);

  TestManagementPolicyProvider policy_provider;
  policy_provider.SetMustRemainEnabled(true);
  extension_system->management_policy()->RegisterProvider(&policy_provider);

  EXPECT_FALSE(bridge->RequestDisable("dddddddddddddddddddddddddddddddd"));

  extension_system->management_policy()->UnregisterProvider(&policy_provider);
  bridge.reset();
}

}  // namespace
}  // namespace maho
