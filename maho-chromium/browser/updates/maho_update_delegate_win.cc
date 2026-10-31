// Copyright 2026 Maho Browser. All rights reserved.

#ifdef UNSAFE_BUFFERS_BUILD
// Win32 API interop uses raw buffers / pointer arithmetic (memset, .data(),
// pointer offsets) that cannot be expressed with bounds-checked spans.
#pragma allow_unsafe_buffers
#endif

#include "maho/browser/updates/platform_updater_delegate.h"
#include "maho/browser/updates/maho_update_manager.h"

#include <crtdbg.h>
#include <stdint.h>
#include <windows.h>
#include <appmodel.h>
#include <ratio>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>

#include <optional>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/bind_post_task.h"
#include "base/threading/thread.h"
#include "base/values.h"
#include "base/version.h"
#include "chrome/browser/browser_process.h"
#include "components/prefs/pref_service.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "maho/browser/updates/maho_update_pref_names.h"
#include "maho/browser/updates/maho_update_signature.h"
#include "maho/browser/updates/rollout_bucket.h"
#include "net/base/net_errors.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "url/gurl.h"

#include "maho/browser/updates/manifest_pubkey.h"
#include "maho/browser/updates/maho_product_version.h"

using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::Foundation;

namespace maho {
namespace {

bool IsPackaged() {
  wchar_t package_name[1] = {0};
  UINT32 buffer_length = 0;
  LONG result = GetCurrentPackageFullName(&buffer_length, package_name);
  return result != APPMODEL_ERROR_NO_PACKAGE;
}

void InitWinRTApartmentOnce() {
  winrt::init_apartment(winrt::apartment_type::single_threaded);
}

void UninitWinRTApartment() {
  winrt::uninit_apartment();
}

}  // namespace

class PlatformUpdaterDelegateWin : public PlatformUpdaterDelegate {
 public:
  PlatformUpdaterDelegateWin() = default;
  ~PlatformUpdaterDelegateWin() override {
    weak_factory_.InvalidateWeakPtrs();
    if (winrt_thread_) {
      winrt_thread_->task_runner()->PostTask(
          FROM_HERE, base::BindOnce(&UninitWinRTApartment));
      winrt_thread_->Stop();
    }
  }

  void Initialize() override {
    if (IsPackaged()) {
      is_msix_ = true;
      winrt_thread_ = std::make_unique<base::Thread>("MahoUpdateWinRT");
      base::Thread::Options options;
      options.message_pump_type = base::MessagePumpType::DEFAULT;
      winrt_thread_->StartWithOptions(std::move(options));
      winrt_thread_->task_runner()->PostTask(
          FROM_HERE, base::BindOnce(&InitWinRTApartmentOnce));
    } else {
      is_msix_ = false;
    }
  }

  void Check(bool manual_check) override {
    if (is_msix_) {
      winrt_thread_->task_runner()->PostTask(
          FROM_HERE,
          base::BindOnce(&PlatformUpdaterDelegateWin::CheckMsixUpdate,
                         base::BindPostTask(
                             content::GetUIThreadTaskRunner({}),
                             base::BindOnce(
                                 &PlatformUpdaterDelegateWin::OnMsixUpdateChecked,
                                 weak_factory_.GetWeakPtr()))));
    } else {
      CheckManifestUpdate();
    }
  }

  void SetChannel(const std::string& channel_name) override {
    channel_ = channel_name;
  }

  void ApplyUpdateAndRestart() override {
    // Windows updates are delivered exclusively through the Microsoft Store.
  }

  std::string GetUpdateGuidance() const override {
    return is_msix_ ? std::string()
                    : "Maho for Windows updates through the Microsoft Store. "
                      "Get the latest version from the Store.";
  }

 private:
  static void CheckMsixUpdate(
      base::OnceCallback<void(UpdateState, UpdateError)> reply) {
    // Chromium builds with C++ exceptions disabled, so the throwing C++/WinRT
    // projections (op.get() / try-catch on winrt::hresult_error) cannot be
    // used. Drive the async operation via a completion handler + WaitableEvent
    // and branch on AsyncStatus; GetResults() is only called on
    // AsyncStatus::Completed, where it does not throw.
    Package package = Package::Current();
    auto op = package.CheckUpdateAvailabilityAsync();

    base::WaitableEvent done;
    PackageUpdateAvailability availability = PackageUpdateAvailability::Unknown;
    AsyncStatus final_status = AsyncStatus::Error;
    op.Completed(
        [&](IAsyncOperation<PackageUpdateAvailabilityResult> const& async,
            AsyncStatus status) {
          final_status = status;
          if (status == AsyncStatus::Completed) {
            availability = async.GetResults().Availability();
          }
          done.Signal();
        });
    done.Wait();

    UpdateState next_state = UpdateState::kUpToDate;
    UpdateError err = UpdateError::kNone;
    if (final_status != AsyncStatus::Completed) {
      next_state = UpdateState::kError;
      err = UpdateError::kConnectionFailed;
    } else {
      switch (availability) {
        case PackageUpdateAvailability::Available:
        case PackageUpdateAvailability::Required:
          next_state = UpdateState::kUpdateAvailable;
          break;
        case PackageUpdateAvailability::NoUpdates:
          next_state = UpdateState::kUpToDate;
          break;
        case PackageUpdateAvailability::Error:
        case PackageUpdateAvailability::Unknown:
        default:
          next_state = UpdateState::kError;
          err = UpdateError::kConnectionFailed;
          break;
      }
    }

    std::move(reply).Run(next_state, err);
  }

  void OnMsixUpdateChecked(UpdateState state, UpdateError error) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (state == UpdateState::kError) {
      MahoUpdateManager::GetInstance()->TransitionToError(error);
    } else {
      MahoUpdateManager::GetInstance()->TransitionToState(state);
    }
  }

  void CheckManifestUpdate() {
    if (manifest_loader_) {
      return;
    }
    MahoUpdateManager::GetInstance()->TransitionToState(UpdateState::kChecking);

    auto request = std::make_unique<network::ResourceRequest>();
    const std::string& base_url =
        MahoUpdateManager::GetInstance()->GetServerBaseUrl();
    request->url = GURL(base_url + "/updates/windows/check?channel=" +
                        channel_);
    request->credentials_mode = network::mojom::CredentialsMode::kOmit;

    net::NetworkTrafficAnnotationTag traffic_annotation =
        net::DefineNetworkTrafficAnnotation("maho_update_manifest_fetch", R"(
          semantics {
            sender: "Maho Auto-Update Manager"
            description: "Fetches signed update manifest envelope for Windows."
            trigger: "Triggered periodically or manually from about page."
            data: "Channel and platform configuration."
            destination: WEBSITE
          }
          policy {
            cookies_allowed: NO
            setting: "Auto-updates are gated by the maho.update.enabled pref."
          }
        )");

    manifest_loader_ = network::SimpleURLLoader::Create(std::move(request),
                                                        traffic_annotation);
    auto* factory = g_browser_process->shared_url_loader_factory().get();
    manifest_loader_->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
        factory,
        base::BindOnce(&PlatformUpdaterDelegateWin::OnManifestFetched,
                       base::Unretained(this)));
  }

  // The on-the-wire envelope is intentionally minimal so the signed bytes
  // are unambiguous. Server emits:
  //   { "signature": "<128 hex chars>", "payload": "<inner JSON string>" }
  // The signature covers the unescaped bytes of the payload string, NOT a
  // re-serialization of the parsed dict. This sidesteps JSON canonicalization
  // (key ordering, whitespace, number formatting) — the bytes the server
  // signs are exactly the bytes the client receives via FindString.
  void OnManifestFetched(std::optional<std::string> body) {
    std::unique_ptr<network::SimpleURLLoader> loader = std::move(manifest_loader_);
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
    const base::DictValue& env_dict = envelope->GetDict();
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
                                          updates::kMahoUpdateManifestPublicKey)) {
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
    const base::DictValue& m = payload->GetDict();

    const std::string* version = m.FindString("version");
    std::optional<int> rollout_bucket = m.FindInt("rollout_bucket");

    if (!version || !rollout_bucket) {
      MahoUpdateManager::GetInstance()->TransitionToError(
          UpdateError::kUnknown);
      return;
    }

    // Downgrade protection: reject manifests that are not strictly newer than
    // the running build. The manifest is ed25519-signed, but an old (still
    // validly signed) manifest+installer could otherwise be replayed to force
    // a downgrade to a known-vulnerable version. Mirrors the Linux delegate.
    base::Version latest(*version);
    base::Version current = updates::GetMahoProductVersion();
    if (!latest.IsValid() || !current.IsValid() || latest <= current) {
      MahoUpdateManager::GetInstance()->TransitionToState(
          UpdateState::kUpToDate);
      return;
    }

    PrefService* local_state =
        g_browser_process ? g_browser_process->local_state() : nullptr;
    std::string install_id = local_state
                                 ? local_state->GetString(
                                       prefs::kMahoUpdateInstallId)
                                 : std::string();
    if (updates::RolloutBucket::Compute(install_id) >= *rollout_bucket) {
      MahoUpdateManager::GetInstance()->TransitionToState(
          UpdateState::kUpToDate);
      return;
    }

    MahoUpdateManager::GetInstance()->TransitionToState(
        UpdateState::kUpdateAvailable);
  }

  bool is_msix_ = false;
  std::unique_ptr<base::Thread> winrt_thread_;
  std::string channel_;

  std::unique_ptr<network::SimpleURLLoader> manifest_loader_;

  // Only the UI completion dereferences this weak receiver.
  base::WeakPtrFactory<PlatformUpdaterDelegateWin> weak_factory_{this};
};

std::unique_ptr<PlatformUpdaterDelegate> CreatePlatformUpdaterDelegate() {
  return std::make_unique<PlatformUpdaterDelegateWin>();
}

}  // namespace maho
