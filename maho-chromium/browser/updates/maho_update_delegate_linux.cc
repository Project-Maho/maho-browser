// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/maho_update_delegate_linux.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/notreached.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "base/version.h"
#include "chrome/browser/browser_process.h"
#include "components/version_info/version_info.h"
#include "maho/browser/updates/maho_product_version.h"
#include "maho/browser/updates/maho_update_manager.h"
#include "maho/browser/updates/maho_update_signature.h"
#include "maho/browser/updates/manifest_pubkey.h"
#include "net/base/net_errors.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "url/gurl.h"

namespace maho {

namespace {

class PackageUpdaterDelegate : public LinuxManifestUpdaterDelegate {
 public:
  PackageUpdaterDelegate() = default;
  ~PackageUpdaterDelegate() override = default;

  std::string GetUpdateGuidance() const override {
    // Maho publishes no apt/dnf repository, so `apt upgrade` cannot find a
    // newer build; point at the release download and a local-file install.
    return "A newer version of Maho is available. Download the latest .deb or "
           ".rpm from https://mahobrowser.com/download/ and install it with "
           "your package manager (sudo apt install ./maho_<version>_amd64.deb, "
           "or sudo dnf install ./maho-<version>-1.x86_64.rpm).";
  }
};

class SelfContainedUpdaterDelegate : public LinuxManifestUpdaterDelegate {
 public:
  SelfContainedUpdaterDelegate() = default;
  ~SelfContainedUpdaterDelegate() override = default;

  std::string GetUpdateGuidance() const override {
    return "A newer version of Maho is available. Download the latest Linux "
           "archive from https://mahobrowser.com/download/ and replace this "
           "installation.";
  }
};

}  // namespace

LinuxManifestUpdaterDelegate::LinuxManifestUpdaterDelegate() = default;
LinuxManifestUpdaterDelegate::~LinuxManifestUpdaterDelegate() = default;

void LinuxManifestUpdaterDelegate::Initialize() {
  LOG(INFO) << "Maho: Linux update delegate initialized.";
}

void LinuxManifestUpdaterDelegate::Check(bool manual_check) {
  if (loader_) {
    return;
  }
  MahoUpdateManager::GetInstance()->TransitionToState(UpdateState::kChecking);

  auto request = std::make_unique<network::ResourceRequest>();
  const std::string& base_url =
      MahoUpdateManager::GetInstance()->GetServerBaseUrl();
  request->url = GURL(base_url + "/updates/linux/check?channel=" + channel_);
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("maho_update_linux_manifest", R"(
          semantics {
            sender: "Maho Auto-Update Manager"
            description: "Fetches signed update manifest envelope for Linux."
            trigger: "Triggered periodically or manually."
            data: "Channel and platform configuration."
            destination: WEBSITE
          }
          policy {
            cookies_allowed: NO
            setting: "Auto-updates are gated by the maho.update.enabled pref."
          }
        )");

  loader_ =
      network::SimpleURLLoader::Create(std::move(request), traffic_annotation);
  auto* factory = g_browser_process->shared_url_loader_factory().get();
  loader_->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
      factory, base::BindOnce(&LinuxManifestUpdaterDelegate::OnManifestFetched,
                              base::Unretained(this)));
}

void LinuxManifestUpdaterDelegate::SetChannel(const std::string& channel_name) {
  channel_ = channel_name.empty() ? std::string("stable") : channel_name;
}

void LinuxManifestUpdaterDelegate::ApplyUpdateAndRestart() {
  // Every shipped Linux format is banner-only: a package install is upgraded by
  // apt/dnf, and a tarball install lives in a user-owned directory the browser
  // must not overwrite underneath itself.
}

void LinuxManifestUpdaterDelegate::OnManifestFetched(
    std::optional<std::string> body) {
  std::unique_ptr<network::SimpleURLLoader> loader = std::move(loader_);
  if (!body || loader->NetError() != net::OK) {
    MahoUpdateManager::GetInstance()->TransitionToError(
        UpdateError::kConnectionFailed);
    return;
  }

  auto envelope = base::JSONReader::Read(*body, base::JSON_PARSE_RFC);
  if (!envelope || !envelope->is_dict()) {
    MahoUpdateManager::GetInstance()->TransitionToError(
        UpdateError::kManifestUnsigned);
    return;
  }
  const auto& env_dict = envelope->GetDict();
  const std::string* sig_hex = env_dict.FindString("signature");
  const std::string* payload_str = env_dict.FindString("payload");
  if (!sig_hex || !payload_str) {
    MahoUpdateManager::GetInstance()->TransitionToError(
        UpdateError::kManifestUnsigned);
    return;
  }

  std::vector<uint8_t> sig;
  if (!base::HexStringToBytes(*sig_hex, &sig)) {
    MahoUpdateManager::GetInstance()->TransitionToError(
        UpdateError::kManifestUnsigned);
    return;
  }
  if (!updates::VerifyManifestSignature(
          *payload_str, sig, updates::kMahoUpdateManifestPublicKey)) {
    MahoUpdateManager::GetInstance()->TransitionToError(
        UpdateError::kManifestUnsigned);
    return;
  }

  ApplyVerifiedManifest(*payload_str);
}

void LinuxManifestUpdaterDelegate::ApplyVerifiedManifest(
    std::string_view payload_json) {
  auto payload = base::JSONReader::Read(payload_json, base::JSON_PARSE_RFC);
  if (!payload || !payload->is_dict()) {
    MahoUpdateManager::GetInstance()->TransitionToError(UpdateError::kUnknown);
    return;
  }
  const std::string* latest_str = payload->GetDict().FindString("version");
  if (!latest_str) {
    MahoUpdateManager::GetInstance()->TransitionToError(UpdateError::kUnknown);
    return;
  }

  base::Version latest(*latest_str);
  base::Version current = updates::GetMahoProductVersion();
  // An unparseable version must NOT collapse into kUpToDate: telling a stale
  // install it is current is the failure mode this delegate exists to prevent.
  if (!latest.IsValid() || !current.IsValid()) {
    MahoUpdateManager::GetInstance()->TransitionToError(UpdateError::kUnknown);
    return;
  }
  if (latest <= current) {
    MahoUpdateManager::GetInstance()->TransitionToState(UpdateState::kUpToDate);
    return;
  }

  LOG(INFO) << "Maho: update available (" << current.GetString() << " -> "
            << latest.GetString() << "). " << GetUpdateGuidance();
  MahoUpdateManager::GetInstance()->TransitionToState(
      UpdateState::kUpdateAvailable);
}

std::unique_ptr<LinuxManifestUpdaterDelegate> CreateLinuxUpdaterDelegateForKind(
    updates::LinuxInstallKind kind) {
  switch (kind) {
    case updates::LinuxInstallKind::kSystemPackage:
      return std::make_unique<PackageUpdaterDelegate>();
    case updates::LinuxInstallKind::kSelfContained:
      return std::make_unique<SelfContainedUpdaterDelegate>();
  }
  NOTREACHED();
}

std::unique_ptr<PlatformUpdaterDelegate> CreatePlatformUpdaterDelegate() {
  return CreateLinuxUpdaterDelegateForKind(updates::DetectLinuxInstallKind());
}

}  // namespace maho
