// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/maho_update_delegate_linux.h"

#include <memory>
#include <string>
#include <vector>

#include "base/check.h"
#include "base/strings/strcat.h"
#include "base/version.h"
#include "components/version_info/version_info.h"
#include "content/public/test/browser_task_environment.h"
#include "maho/browser/updates/linux_install_kind.h"
#include "maho/browser/updates/maho_product_version.h"
#include "maho/browser/updates/maho_update_manager.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho {
namespace {

std::string VersionNewerThanCurrent() {
  base::Version current = updates::GetMahoProductVersion();
  CHECK(current.IsValid());
  std::vector<uint32_t> parts = current.components();
  parts[0] += 1;
  return base::Version(parts).GetString();
}

std::string ManifestWithVersion(const std::string& version) {
  return base::StrCat({R"({"version":")", version, R"("})"});
}

class LinuxUpdaterDelegateTest : public ::testing::Test {
 protected:
  void SetUp() override {
    MahoUpdateManager::GetInstance()->ResetForTesting();
  }

  void TearDown() override {
    MahoUpdateManager::GetInstance()->ResetForTesting();
  }

  static UpdateState State() {
    return MahoUpdateManager::GetInstance()->GetState();
  }

  static UpdateError LastError() {
    return MahoUpdateManager::GetInstance()->GetLastError();
  }

  static std::unique_ptr<LinuxManifestUpdaterDelegate> MakeDelegate(
      updates::LinuxInstallKind kind) {
    return CreateLinuxUpdaterDelegateForKind(kind);
  }

  content::BrowserTaskEnvironment task_environment_;
};

TEST_F(LinuxUpdaterDelegateTest, TarballNewerVersionIsNotReportedUpToDate) {
  auto delegate = MakeDelegate(updates::LinuxInstallKind::kSelfContained);
  ASSERT_EQ(State(), UpdateState::kIdle);

  delegate->ApplyVerifiedManifest(
      ManifestWithVersion(VersionNewerThanCurrent()));

  EXPECT_NE(State(), UpdateState::kUpToDate);
  EXPECT_EQ(State(), UpdateState::kUpdateAvailable);
}

TEST_F(LinuxUpdaterDelegateTest, TarballGuidancePointsAtDownloadPage) {
  auto delegate = MakeDelegate(updates::LinuxInstallKind::kSelfContained);

  const std::string guidance = delegate->GetUpdateGuidance();

  EXPECT_NE(guidance.find("https://mahobrowser.com/download/"),
            std::string::npos)
      << "tarball guidance must hand the user a download location: "
      << guidance;
}

TEST_F(LinuxUpdaterDelegateTest, PackageGuidanceDiffersAndNamesPackageManager) {
  auto package = MakeDelegate(updates::LinuxInstallKind::kSystemPackage);
  auto tarball = MakeDelegate(updates::LinuxInstallKind::kSelfContained);

  const std::string package_guidance = package->GetUpdateGuidance();

  EXPECT_NE(package_guidance.find("apt"), std::string::npos)
      << package_guidance;
  EXPECT_NE(package_guidance.find("dnf"), std::string::npos)
      << package_guidance;
  EXPECT_NE(package_guidance, tarball->GetUpdateGuidance());
}

TEST_F(LinuxUpdaterDelegateTest, PackageNewerVersionReportsUpdateAvailable) {
  auto delegate = MakeDelegate(updates::LinuxInstallKind::kSystemPackage);

  delegate->ApplyVerifiedManifest(
      ManifestWithVersion(VersionNewerThanCurrent()));

  EXPECT_EQ(State(), UpdateState::kUpdateAvailable);
}

TEST_F(LinuxUpdaterDelegateTest, OlderManifestVersionReportsUpToDate) {
  auto delegate = MakeDelegate(updates::LinuxInstallKind::kSelfContained);

  delegate->ApplyVerifiedManifest(ManifestWithVersion("0.0.0.1"));

  EXPECT_EQ(State(), UpdateState::kUpToDate);
}

TEST_F(LinuxUpdaterDelegateTest, EqualProductVersionReportsUpToDate) {
  auto delegate = MakeDelegate(updates::LinuxInstallKind::kSelfContained);

  delegate->ApplyVerifiedManifest(
      ManifestWithVersion(updates::kMahoProductVersion));

  EXPECT_EQ(State(), UpdateState::kUpToDate);
}

TEST_F(LinuxUpdaterDelegateTest, OlderProductVersionReportsUpToDate) {
  auto delegate = MakeDelegate(updates::LinuxInstallKind::kSelfContained);

  std::vector<uint32_t> parts = updates::GetMahoProductVersion().components();
  ASSERT_GT(parts.back(), 0u);
  parts.back() -= 1;
  delegate->ApplyVerifiedManifest(
      ManifestWithVersion(base::Version(parts).GetString()));

  EXPECT_EQ(State(), UpdateState::kUpToDate);
}

TEST_F(LinuxUpdaterDelegateTest, NewerProductVersionReportsUpdateAvailable) {
  auto delegate = MakeDelegate(updates::LinuxInstallKind::kSelfContained);

  std::vector<uint32_t> parts = updates::GetMahoProductVersion().components();
  parts.back() += 1;
  delegate->ApplyVerifiedManifest(
      ManifestWithVersion(base::Version(parts).GetString()));

  EXPECT_EQ(State(), UpdateState::kUpdateAvailable);
}

TEST_F(LinuxUpdaterDelegateTest, UnparseableManifestVersionIsAnErrorNotUpToDate) {
  auto delegate = MakeDelegate(updates::LinuxInstallKind::kSelfContained);

  delegate->ApplyVerifiedManifest(ManifestWithVersion("not-a-version"));

  EXPECT_NE(State(), UpdateState::kUpToDate);
  EXPECT_EQ(State(), UpdateState::kError);
  EXPECT_EQ(LastError(), UpdateError::kUnknown);
}

TEST_F(LinuxUpdaterDelegateTest, ManifestWithoutAVersionIsAnErrorNotUpToDate) {
  auto delegate = MakeDelegate(updates::LinuxInstallKind::kSelfContained);

  delegate->ApplyVerifiedManifest(R"({"channel":"stable"})");

  EXPECT_NE(State(), UpdateState::kUpToDate);
  EXPECT_EQ(State(), UpdateState::kError);
}

}  // namespace
}  // namespace maho
