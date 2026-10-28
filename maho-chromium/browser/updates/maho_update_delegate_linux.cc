// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/maho_update_delegate_linux.h"

#include <algorithm>
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
#include "maho/browser/updates/maho_update_server_url.h"
#include "maho/browser/updates/maho_update_signature.h"
#include "maho/browser/updates/manifest_pubkey.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace maho {

namespace {

// Relay outages (connection failures, 5xx) fall back to the static feed. A 4xx
// or a body that fails verification does not: those are answers, not outages.
bool IsRelayOutage(const network::SimpleURLLoader& loader) {
  const int net_error = loader.NetError();
  if (net_error == net::OK) {
    return false;
  }
  if (net_error != net::ERR_HTTP_RESPONSE_CODE_FAILURE) {
    return true;
  }
  const network::mojom::URLResponseHead* info = loader.ResponseInfo();
  return info && info->headers && info->headers->response_code() >= 500;
}

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

LinuxManifestUpdaterDelegate::LinuxManifestUpdaterDelegate()
    : manifest_public_key_(updates::kMahoUpdateManifestPublicKey) {}
LinuxManifestUpdaterDelegate::~LinuxManifestUpdaterDelegate() = default;

void LinuxManifestUpdaterDelegate::Initialize() {
  LOG(INFO) << "Maho: Linux update delegate initialized.";
}

void LinuxManifestUpdaterDelegate::SetURLLoaderFactoryForTesting(
    scoped_refptr<network::SharedURLLoaderFactory> factory) {
  url_loader_factory_for_testing_ = std::move(factory);
}

void LinuxManifestUpdaterDelegate::SetRelayBaseUrlForTesting(
    const std::string& base_url) {
  relay_base_url_for_testing_ = base_url;
}

void LinuxManifestUpdaterDelegate::SetManifestPublicKeyForTesting(
    base::span<const uint8_t, 32> key) {
  std::copy(key.begin(), key.end(), manifest_public_key_.begin());
}

void LinuxManifestUpdaterDelegate::Check(bool manual_check) {
  if (loader_) {
    return;
  }
  MahoUpdateManager::GetInstance()->TransitionToState(UpdateState::kChecking);

  const std::string base_url =
      relay_base_url_for_testing_
          ? *relay_base_url_for_testing_
          : MahoUpdateManager::GetInstance()->GetServerBaseUrl();
  StartFetch(GURL(base_url + "/updates/linux/check?channel=" + channel_),
             /*is_static_fallback=*/false);
}

void LinuxManifestUpdaterDelegate::StartFetch(const GURL& url,
                                              bool is_static_fallback) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("maho_update_linux_manifest", R"(
          semantics {
            sender: "Maho Auto-Update Manager"
            description: "Fetches signed update manifest envelope for Linux, "
                         "from the Maho relay or, when the relay is down, the "
                         "static copy on the GitHub Release."
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
  network::SharedURLLoaderFactory* factory =
      url_loader_factory_for_testing_
          ? url_loader_factory_for_testing_.get()
          : g_browser_process->shared_url_loader_factory().get();
  loader_->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
      factory, base::BindOnce(&LinuxManifestUpdaterDelegate::OnManifestFetched,
                              base::Unretained(this), is_static_fallback));
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
    bool is_static_fallback,
    std::optional<std::string> body) {
  std::unique_ptr<network::SimpleURLLoader> loader = std::move(loader_);
  if (!body || loader->NetError() != net::OK) {
    if (!is_static_fallback && IsRelayOutage(*loader)) {
      LOG(WARNING) << "Maho: update relay unreachable ("
                   << net::ErrorToShortString(loader->NetError())
                   << "); retrying the static fallback manifest.";
      StartFetch(GURL(updates::GetStaticLinuxManifestFallbackUrl()),
                 /*is_static_fallback=*/true);
      return;
    }
    MahoUpdateManager::GetInstance()->TransitionToError(
        UpdateError::kConnectionFailed);
    return;
  }

  VerifyAndApplyEnvelope(*body);
}

void LinuxManifestUpdaterDelegate::VerifyAndApplyEnvelope(
    const std::string& body) {
  auto envelope = base::JSONReader::Read(body, base::JSON_PARSE_RFC);
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
  if (!updates::VerifyManifestSignature(*payload_str, sig,
                                        manifest_public_key_)) {
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
