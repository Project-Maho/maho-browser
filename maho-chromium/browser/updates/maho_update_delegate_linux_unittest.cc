// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/maho_update_delegate_linux.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "base/check.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "base/version.h"
#include "components/version_info/version_info.h"
#include "content/public/test/browser_task_environment.h"
#include "maho/browser/updates/linux_install_kind.h"
#include "maho/browser/updates/maho_product_version.h"
#include "maho/browser/updates/maho_update_manager.h"
#include "maho/browser/updates/maho_update_server_url.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/boringssl/src/include/openssl/curve25519.h"

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

// Ed25519 key pair standing in for the release signing key.
struct TestSigningKey {
  TestSigningKey() { ED25519_keypair(public_key.data(), private_key.data()); }
  std::array<uint8_t, 32> public_key;
  std::array<uint8_t, 64> private_key;
};

// Builds the {"payload","signature"} envelope served by the relay and mirrored
// to the static release asset.
std::string SignedEnvelope(const std::string& payload,
                           const TestSigningKey& key) {
  std::array<uint8_t, 64> sig;
  CHECK_EQ(1, ED25519_sign(sig.data(),
                           reinterpret_cast<const uint8_t*>(payload.data()),
                           payload.size(), key.private_key.data()));
  base::Value::Dict envelope;
  envelope.Set("payload", payload);
  envelope.Set("signature", base::HexEncode(sig));
  return *base::WriteJson(envelope);
}

constexpr char kTestRelayBaseUrl[] = "https://relay.test";

// Quits |run_loop| once the update check reaches a terminal state.
class TerminalStateWaiter : public MahoUpdateObserver {
 public:
  explicit TerminalStateWaiter(base::RunLoop* run_loop) : run_loop_(run_loop) {
    MahoUpdateManager::GetInstance()->AddObserver(this);
  }
  ~TerminalStateWaiter() override {
    MahoUpdateManager::GetInstance()->RemoveObserver(this);
  }
  void OnUpdateStateChanged(UpdateState state) override {
    if (state != UpdateState::kChecking && state != UpdateState::kIdle) {
      run_loop_->Quit();
    }
  }
  void OnUpdateProgress(double percent) override {}

 private:
  raw_ptr<base::RunLoop> run_loop_;
};

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

class LinuxUpdaterDelegateFallbackTest : public LinuxUpdaterDelegateTest {
 protected:
  std::unique_ptr<LinuxManifestUpdaterDelegate> MakeNetworkDelegate() {
    auto delegate = MakeDelegate(updates::LinuxInstallKind::kSelfContained);
    delegate->SetURLLoaderFactoryForTesting(
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &url_loader_factory_));
    delegate->SetRelayBaseUrlForTesting(kTestRelayBaseUrl);
    delegate->SetManifestPublicKeyForTesting(release_key_.public_key);
    return delegate;
  }

  void RunCheck(LinuxManifestUpdaterDelegate* delegate) {
    base::RunLoop run_loop;
    TerminalStateWaiter waiter(&run_loop);
    delegate->Check(/*manual_check=*/true);
    run_loop.Run();
  }

  static std::string RelayUrl() {
    return base::StrCat(
        {kTestRelayBaseUrl, "/updates/linux/check?channel=stable"});
  }

  TestSigningKey release_key_;
  network::TestURLLoaderFactory url_loader_factory_;
};

TEST_F(LinuxUpdaterDelegateFallbackTest, Relay503UsesStaticFallbackFeed) {
  auto delegate = MakeNetworkDelegate();
  url_loader_factory_.AddResponse(RelayUrl(), "unavailable",
                                  net::HTTP_SERVICE_UNAVAILABLE);
  url_loader_factory_.AddResponse(
      updates::GetStaticLinuxManifestFallbackUrl(),
      SignedEnvelope(ManifestWithVersion(VersionNewerThanCurrent()),
                     release_key_));

  RunCheck(delegate.get());

  EXPECT_EQ(State(), UpdateState::kUpdateAvailable);
  EXPECT_EQ(url_loader_factory_.total_requests(), 2u);
}

TEST_F(LinuxUpdaterDelegateFallbackTest, StaticFeedWithBadSignatureIsRejected) {
  auto delegate = MakeNetworkDelegate();
  TestSigningKey attacker_key;
  url_loader_factory_.AddResponse(RelayUrl(), "unavailable",
                                  net::HTTP_SERVICE_UNAVAILABLE);
  url_loader_factory_.AddResponse(
      updates::GetStaticLinuxManifestFallbackUrl(),
      SignedEnvelope(ManifestWithVersion(VersionNewerThanCurrent()),
                     attacker_key));

  RunCheck(delegate.get());

  EXPECT_EQ(State(), UpdateState::kError);
  EXPECT_EQ(LastError(), UpdateError::kManifestUnsigned);
  EXPECT_EQ(url_loader_factory_.total_requests(), 2u);
}

}  // namespace
}  // namespace maho
