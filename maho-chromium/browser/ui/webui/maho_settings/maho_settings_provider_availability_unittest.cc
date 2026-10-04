// Copyright 2026 Maho Browser. All rights reserved.

#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/passwords/maho_password_provider_utils.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings_password_helpers.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho_settings_password_helpers {
namespace {

using maho::passwords::EffectivePasswordProvider;

constexpr char kOwnerProfileKey[] = "owner-profile";
constexpr char kBitwardenExtensionId[] = "nngceckbapebfimnlniiiahkandclblb";
constexpr char kOnePasswordExtensionId[] =
    "aeblfdkhhhdcdjpifhhbdiojplfjncoa";

std::string PasswordProviderExtensionJson(bool bitwarden_enabled,
                                          bool one_password_enabled) {
  std::vector<std::string> entries;
  if (bitwarden_enabled) {
    entries.push_back(
        std::string(R"({"id":")") + kBitwardenExtensionId +
        R"(","name":"Bitwarden","version":"1.0","enabled":true})");
  }
  if (one_password_enabled) {
    entries.push_back(
        std::string(R"({"id":")") + kOnePasswordExtensionId +
        R"(","name":"1Password","version":"1.0","enabled":true})");
  }

  std::string json = "[";
  for (size_t index = 0; index < entries.size(); ++index) {
    if (index > 0) {
      json += ",";
    }
    json += entries[index];
  }
  json += "]";
  return json;
}

void SetPasswordProviderExtensionsForProfile(std::string_view profile_key,
                                             bool bitwarden_enabled,
                                             bool one_password_enabled) {
  const std::string json =
      PasswordProviderExtensionJson(bitwarden_enabled, one_password_enabled);
  const std::string key(profile_key);
  maho_core_set_installed_extensions_for_profile(maho::GetCore(), key.c_str(),
                                                 json.c_str());
}

class ScopedCoreOverride {
 public:
  explicit ScopedCoreOverride(MahoCore* core) : saved_(maho::GetCore()) {
    maho::SetCore(core);
  }
  ~ScopedCoreOverride() { maho::SetCore(saved_); }

  ScopedCoreOverride(const ScopedCoreOverride&) = delete;
  ScopedCoreOverride& operator=(const ScopedCoreOverride&) = delete;

 private:
  MahoCore* saved_;
};

class MahoSettingsProviderAvailabilityTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(maho_storage_set_sqlcipher_key(
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    const base::FilePath database_path =
        temp_dir_.GetPath().AppendASCII("provider-availability.sqlite");
    core_ = maho_core_new_with_storage(database_path.AsUTF8Unsafe().c_str());
    ASSERT_NE(core_, nullptr);
    core_override_ = std::make_unique<ScopedCoreOverride>(core_);
  }

  void TearDown() override {
    core_override_.reset();
    maho_core_free(core_);
    core_ = nullptr;
  }

 private:
  base::ScopedTempDir temp_dir_;
  MahoCore* core_ = nullptr;
  std::unique_ptr<ScopedCoreOverride> core_override_;
};

TEST_F(MahoSettingsProviderAvailabilityTest,
       EffectiveProviderUsesProfileScopedExtensionState) {
  struct TestCase {
    std::string_view selected_provider;
    bool bitwarden_installed;
    bool one_password_installed;
    EffectivePasswordProvider expected;
  };
  constexpr TestCase kCases[] = {
      {"maho_native", false, false,
       EffectivePasswordProvider::kMahoNative},
      {"bitwarden", true, false, EffectivePasswordProvider::kBitwarden},
      {"onepassword", false, true, EffectivePasswordProvider::kOnePassword},
      {"bitwarden", false, true, EffectivePasswordProvider::kDisabled},
      {"onepassword", true, false, EffectivePasswordProvider::kDisabled},
  };

  // Populate the legacy unprofiled/default bucket with both providers. A
  // missing provider in the owner bucket must still be unavailable.
  const std::string unprofiled_extensions =
      PasswordProviderExtensionJson(true, true);
  maho_core_set_installed_extensions(maho::GetCore(),
                                     unprofiled_extensions.c_str());

  for (const TestCase& test_case : kCases) {
    SCOPED_TRACE(test_case.selected_provider);
    SetPasswordProviderExtensionsForProfile(
        kOwnerProfileKey, test_case.bitwarden_installed,
        test_case.one_password_installed);
    ASSERT_TRUE(UpdateAutofillSettingsInCore(
        true, std::string(test_case.selected_provider)));

    EXPECT_EQ(test_case.expected,
              maho::passwords::GetEffectivePasswordProviderForProfileKey(
                  kOwnerProfileKey));
  }
}

TEST_F(MahoSettingsProviderAvailabilityTest,
       UnavailableExternalProviderIsDisabledNotNative) {
  SetPasswordProviderExtensionsForProfile(kOwnerProfileKey, false, false);
  ASSERT_TRUE(UpdateAutofillSettingsInCore(true, "bitwarden"));

  EXPECT_EQ(EffectivePasswordProvider::kDisabled,
            maho::passwords::GetEffectivePasswordProviderForProfileKey(
                kOwnerProfileKey));
  EXPECT_EQ("disabled",
            maho::passwords::GetActivePasswordProviderModeForProfileKey(
                kOwnerProfileKey));
}

TEST_F(MahoSettingsProviderAvailabilityTest,
       PasswordsDisabledDisablesNativeProvider) {
  SetPasswordProviderExtensionsForProfile(kOwnerProfileKey, true, true);
  ASSERT_TRUE(UpdateAutofillSettingsInCore(false, "maho_native"));

  EXPECT_EQ(EffectivePasswordProvider::kDisabled,
            maho::passwords::GetEffectivePasswordProviderForProfileKey(
                kOwnerProfileKey));
}

TEST_F(MahoSettingsProviderAvailabilityTest,
       MissingCoreDisablesEffectiveProvider) {
  ScopedCoreOverride missing_core(nullptr);
  EXPECT_EQ(EffectivePasswordProvider::kDisabled,
            maho::passwords::GetEffectivePasswordProviderForProfileKey(
                kOwnerProfileKey));
}

TEST_F(MahoSettingsProviderAvailabilityTest,
       MalformedOrUnknownProviderStateDisablesEffectiveProvider) {
  constexpr std::string_view kValidSettings =
      R"({"autofill":{"passwordsEnabled":true,"passwordProvider":"maho_native"}})";
  constexpr std::string_view kValidRegistry =
      R"([{"providerId":"maho_native","extensionIds":[]}])";
  constexpr std::string_view kValidExtensions = "[]";

  for (const auto& test_case : {
           std::tuple<std::string_view, std::string_view, std::string_view>{
               "{", kValidRegistry, kValidExtensions},
           {R"({"autofill":{"passwordsEnabled":"true","passwordProvider":"maho_native"}})",
            kValidRegistry, kValidExtensions},
           {R"({"autofill":{"passwordsEnabled":true,"passwordProvider":"unknown_external"}})",
            kValidRegistry, kValidExtensions},
           {kValidSettings, "{", kValidExtensions},
           {kValidSettings,
            R"([{"providerId":"maho_native","extensionIds":"not-a-list"}])",
            kValidExtensions},
       }) {
    EXPECT_EQ(
        EffectivePasswordProvider::kDisabled,
        maho::passwords::ComputeEffectivePasswordProviderForTesting(
            std::get<0>(test_case), std::get<1>(test_case),
            std::get<2>(test_case)));
  }

  EXPECT_EQ(EffectivePasswordProvider::kMahoNative,
            maho::passwords::ComputeEffectivePasswordProviderForTesting(
                kValidSettings, kValidRegistry, "{"));
}

}  // namespace
}  // namespace maho_settings_password_helpers
