#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings_password_helpers.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho_settings_password_helpers {
namespace {

constexpr char kBitwardenExtensionId[] = "nngceckbapebfimnlniiiahkandclblb";
constexpr char kOnePasswordExtensionId[] = "aeblfdkhhhdcdjpifhhbdiojplfjncoa";

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

void SetPasswordProviderExtensions(bool bitwarden_enabled,
                                   bool one_password_enabled) {
  const std::string json =
      PasswordProviderExtensionJson(bitwarden_enabled, one_password_enabled);
  maho_core_set_installed_extensions(maho::GetCore(), json.c_str());
}

std::string_view ProviderToken(
    maho_settings::mojom::PasswordProviderKind provider) {
  switch (provider) {
    case maho_settings::mojom::PasswordProviderKind::kMahoNative:
      return "maho_native";
    case maho_settings::mojom::PasswordProviderKind::kBitwarden:
      return "bitwarden";
    case maho_settings::mojom::PasswordProviderKind::kOnePassword:
      return "onepassword";
  }
}

class ScopedCoreOverride {
 public:
  explicit ScopedCoreOverride(MahoCore* core) : saved_(maho::GetCore()) {
    maho::SetCore(core);
  }
  ~ScopedCoreOverride() { maho::SetCore(saved_); }

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
       CoreBackedProviderAvailabilityMatrixUsesEveryApprovedExtension) {
  using Provider = maho_settings::mojom::PasswordProviderKind;
  struct ProviderInstallCase {
    std::string_view name;
    bool bitwarden_installed;
    bool one_password_installed;
  };
  struct ProviderSelectionCase {
    std::string_view selected;
    Provider expected_selected;
    Provider expected_effective_when_installed;
  };

  constexpr ProviderInstallCase kInstallCases[] = {
      {"neither", false, false},
      {"onepassword_only", false, true},
      {"bitwarden_only", true, false},
      {"both", true, true},
  };
  constexpr ProviderSelectionCase kSelectionCases[] = {
      {"maho_native", Provider::kMahoNative, Provider::kMahoNative},
      {"bitwarden", Provider::kBitwarden, Provider::kBitwarden},
      {"onepassword", Provider::kOnePassword, Provider::kOnePassword},
  };

  for (const ProviderInstallCase& install_case : kInstallCases) {
    SCOPED_TRACE(install_case.name);
    SetPasswordProviderExtensions(install_case.bitwarden_installed,
                                  install_case.one_password_installed);

    for (const ProviderSelectionCase& selection_case : kSelectionCases) {
      SCOPED_TRACE(selection_case.selected);
      ASSERT_TRUE(UpdateAutofillSettingsInCore(
          true, std::string(selection_case.selected)));

      auto status = BuildVaultProviderStatus();

      const bool selected_is_installed =
          selection_case.expected_selected == Provider::kMahoNative ||
          (selection_case.expected_selected == Provider::kBitwarden &&
           install_case.bitwarden_installed) ||
          (selection_case.expected_selected == Provider::kOnePassword &&
           install_case.one_password_installed);
      EXPECT_EQ(selection_case.expected_selected, status->selected_provider);
      EXPECT_EQ(selected_is_installed
                    ? selection_case.expected_effective_when_installed
                    : Provider::kMahoNative,
                status->effective_provider);
      EXPECT_EQ(selected_is_installed, status->selected_provider_is_available);
      const maho_settings::mojom::PasswordProviderKind expected_effective =
          selected_is_installed
              ? selection_case.expected_effective_when_installed
              : Provider::kMahoNative;
      EXPECT_EQ(std::string(ProviderToken(expected_effective)),
                ReadPasswordProviderModeFromCore());
      ASSERT_EQ(3u, status->providers.size());
      EXPECT_TRUE(status->providers[0]->is_available);
      EXPECT_EQ(install_case.bitwarden_installed,
                status->providers[1]->is_available);
      EXPECT_EQ(install_case.one_password_installed,
                status->providers[2]->is_available);
    }
  }
}

}  // namespace
}  // namespace maho_settings_password_helpers
