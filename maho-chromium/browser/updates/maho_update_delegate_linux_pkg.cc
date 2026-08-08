// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/platform_updater_delegate.h"
#include "maho/browser/updates/maho_update_manager.h"

#include <vector>

#include "base/functional/bind.h"
#include <optional>

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "base/version.h"
#include "chrome/browser/browser_process.h"
#include "components/prefs/pref_service.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/browser_thread.h"
#include "maho/browser/updates/maho_update_pref_names.h"
#include "maho/browser/updates/maho_update_signature.h"
#include "maho/browser/updates/rollout_bucket.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"

#include "maho/browser/updates/manifest_pubkey.h"

namespace maho {

class PlatformUpdaterDelegateLinuxPkg : public PlatformUpdaterDelegate {
 public:
  PlatformUpdaterDelegateLinuxPkg() = default;
  ~PlatformUpdaterDelegateLinuxPkg() override = default;

  void Initialize() override {
    LOG(INFO) << "Maho: Linux pkg update delegate initialized.";
  }

  void Check(bool manual_check) override {
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

    loader_ = network::SimpleURLLoader::Create(std::move(request),
                                               traffic_annotation);
    auto* factory = g_browser_process->shared_url_loader_factory().get();
    loader_->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
        factory,
        base::BindOnce(&PlatformUpdaterDelegateLinuxPkg::OnManifestFetched,
                       base::Unretained(this)));
  }

  void SetChannel(const std::string& channel_name) override {
    channel_ = channel_name.empty() ? std::string("stable") : channel_name;
  }

  void ApplyUpdateAndRestart() override {
    // Linux pkg path is banner-only; the system package manager applies the
    // upgrade. There is nothing for the browser to do here.
  }

 private:
  void OnManifestFetched(std::optional<std::string> body) {
    std::unique_ptr<network::SimpleURLLoader> l = std::move(loader_);
    if (!body || l->NetError() != net::OK) {
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

    auto payload = base::JSONReader::Read(*payload_str, base::JSON_PARSE_RFC);
    if (!payload || !payload->is_dict()) {
      MahoUpdateManager::GetInstance()->TransitionToError(
          UpdateError::kUnknown);
      return;
    }
    const std::string* latest_str =
        payload->GetDict().FindString("version");
    if (!latest_str) {
      MahoUpdateManager::GetInstance()->TransitionToError(
          UpdateError::kUnknown);
      return;
    }

    base::Version latest(*latest_str);
    base::Version current(version_info::GetVersionNumber());
    if (!latest.IsValid() || !current.IsValid() || latest <= current) {
      MahoUpdateManager::GetInstance()->TransitionToState(
          UpdateState::kUpToDate);
      return;
    }

    // Banner only. The manager exposes kUpdateAvailable; the UI surface
    // (Phase 7) is responsible for showing the "Run sudo apt upgrade" copy.
    MahoUpdateManager::GetInstance()->TransitionToState(
        UpdateState::kUpdateAvailable);
  }

  std::string channel_ = "stable";
  std::unique_ptr<network::SimpleURLLoader> loader_;
};

std::unique_ptr<PlatformUpdaterDelegate> CreatePlatformUpdaterDelegate() {
  return std::make_unique<PlatformUpdaterDelegateLinuxPkg>();
}

}  // namespace maho
