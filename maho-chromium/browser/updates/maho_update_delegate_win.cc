// Copyright 2026 Maho Browser. All rights reserved.

#ifdef UNSAFE_BUFFERS_BUILD
// Win32 API interop uses raw buffers / pointer arithmetic (memset, .data(),
// pointer offsets) that cannot be expressed with bounds-checked spans.
#pragma allow_unsafe_buffers
#endif

#include "maho/browser/updates/platform_updater_delegate.h"
#include "maho/browser/updates/maho_update_manager.h"

#include <crtdbg.h>
#include <windows.h>
#include <appmodel.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/json/json_reader.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/strings/string_number_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/thread_pool.h"
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
#include "maho/browser/updates/maho_update_signature_win.h"
#include "maho/browser/updates/rollout_bucket.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"

#include "maho/browser/updates/manifest_pubkey.h"

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
      // SAFETY: winrt_thread_ is a dedicated cross-thread task runner owned by
      // this object. Use WeakPtr so the callback silently no-ops if |this| is
      // destroyed before CheckMsixUpdate runs (e.g. rapid shutdown).
      winrt_thread_->task_runner()->PostTask(
          FROM_HERE,
          base::BindOnce(&PlatformUpdaterDelegateWin::CheckMsixUpdate,
                         weak_factory_.GetWeakPtr()));
    } else {
      CheckCustomDownloaderUpdate(manual_check);
    }
  }

  void SetChannel(const std::string& channel_name) override {
    channel_ = channel_name;
  }

  void ApplyUpdateAndRestart() override {
    if (is_msix_) {
      return;
    }

    if (verified_installer_path_.empty()) {
      return;
    }

    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
        base::BindOnce(
            [](const base::FilePath& installer_path, const std::string& hash,
               const std::string& cn) {
              // Re-verify immediately before execution to close the TOCTOU
              // window between OnVerificationComplete and this deferred launch:
              // another process running as this user could otherwise swap the
              // fixed-path installer for a malicious binary after verification.
              if (!updates::VerifySha256(installer_path, hash) ||
                  !updates::VerifyAuthenticode(installer_path, cn)) {
                LOG(ERROR) << "Maho: installer failed re-verification before "
                              "launch; aborting update.";
                base::DeleteFile(installer_path);
                return;
              }
              base::LaunchOptions options;
              options.start_hidden = true;
              base::CommandLine cmd(installer_path);
              // TODO(maho): Maho ships NSIS today; "/S" is NSIS-specific.
              // If installer toolchain changes, update flags here AND
              // installer/README.md. NSIS: "/S"   Inno Setup:
              // "/VERYSILENT /SUPPRESSMSGBOXES"   MSI: invoked via
              // msiexec /i <pkg> /qn.
              cmd.AppendArg("/S");
              base::LaunchProcess(cmd, options);
            },
            verified_installer_path_, installer_sha256_,
            installer_signer_cn_));
  }

 private:
  void CheckMsixUpdate() {
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

    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](UpdateState state, UpdateError error) {
              if (state == UpdateState::kError) {
                MahoUpdateManager::GetInstance()->TransitionToError(error);
              } else {
                MahoUpdateManager::GetInstance()->TransitionToState(state);
              }
            },
            next_state, err));
  }

  void CheckCustomDownloaderUpdate(bool manual_check) {
    if (manifest_loader_) {
      return;
    }

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
    const std::string* installer_url = m.FindString("installer_url");
    const std::string* sha256 = m.FindString("installer_sha256");
    const std::string* signer_cn = m.FindString("installer_signer_cn");
    std::optional<int> rollout_bucket = m.FindInt("rollout_bucket");

    if (!version || !installer_url || !sha256 || !signer_cn ||
        !rollout_bucket) {
      MahoUpdateManager::GetInstance()->TransitionToError(
          UpdateError::kUnknown);
      return;
    }

    // Downgrade protection: reject manifests that are not strictly newer than
    // the running build. The manifest is ed25519-signed, but an old (still
    // validly signed) manifest+installer could otherwise be replayed to force
    // a downgrade to a known-vulnerable version. Mirrors the Linux delegate.
    base::Version latest(*version);
    base::Version current(version_info::GetVersionNumber());
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

    installer_url_ = *installer_url;
    installer_sha256_ = *sha256;
    installer_signer_cn_ = *signer_cn;

    StartDownload();
  }

  void StartDownload() {
    MahoUpdateManager::GetInstance()->TransitionToState(
        UpdateState::kDownloading);

    auto request = std::make_unique<network::ResourceRequest>();
    request->url = GURL(installer_url_);
    request->credentials_mode = network::mojom::CredentialsMode::kOmit;

    net::NetworkTrafficAnnotationTag traffic_annotation =
        net::DefineNetworkTrafficAnnotation("maho_update_installer_download", R"(
          semantics {
            sender: "Maho Auto-Update Manager"
            description: "Downloads browser installer for verification on Windows."
            trigger: "Triggered after a valid update manifest is verified."
            data: "No user data."
            destination: WEBSITE
          }
          policy {
            cookies_allowed: NO
            setting: "Auto-updates are gated by the maho.update.enabled pref."
          }
        )");

    download_loader_ = network::SimpleURLLoader::Create(std::move(request),
                                                        traffic_annotation);

    base::FilePath temp_dir;
    base::PathService::Get(base::DIR_TEMP, &temp_dir);
    base::FilePath dest_path =
        temp_dir.Append(FILE_PATH_LITERAL("maho_update.exe"));

    auto* factory = g_browser_process->shared_url_loader_factory().get();
    download_loader_->DownloadToFile(
        factory,
        base::BindOnce(&PlatformUpdaterDelegateWin::OnDownloadComplete,
                       base::Unretained(this)),
        dest_path);
  }

  void OnDownloadComplete(base::FilePath path) {
    std::unique_ptr<network::SimpleURLLoader> loader =
        std::move(download_loader_);
    if (path.empty() || loader->NetError() != net::OK) {
      MahoUpdateManager::GetInstance()->TransitionToError(
          UpdateError::kDownloadFailed);
      return;
    }

    MahoUpdateManager::GetInstance()->TransitionToState(
        UpdateState::kVerifying);

    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(
            [](const base::FilePath& file_path, const std::string& hash,
               const std::string& cn) {
              if (!updates::VerifySha256(file_path, hash)) {
                return UpdateError::kHashMismatch;
              }
              if (!updates::VerifyAuthenticode(file_path, cn)) {
                return UpdateError::kAuthenticodeFailed;
              }
              return UpdateError::kNone;
            },
            path, installer_sha256_, installer_signer_cn_),
        // SAFETY: The reply runs back on the originating sequence (same sequence
        // as the delegate), but use WeakPtr so the callback silently no-ops if
        // |this| is destroyed between posting and the reply being dispatched.
        base::BindOnce(&PlatformUpdaterDelegateWin::OnVerificationComplete,
                       weak_factory_.GetWeakPtr(), path));
  }

  void OnVerificationComplete(base::FilePath path, UpdateError error) {
    if (error != UpdateError::kNone) {
      base::ThreadPool::PostTask(
          FROM_HERE, {base::MayBlock()},
          base::BindOnce([](const base::FilePath& p) { base::DeleteFile(p); },
                         path));
      MahoUpdateManager::GetInstance()->TransitionToError(error);
      return;
    }

    verified_installer_path_ = path;
    MahoUpdateManager::GetInstance()->TransitionToState(
        UpdateState::kReadyToInstall);
  }

  bool is_msix_ = false;
  std::unique_ptr<base::Thread> winrt_thread_;
  std::string channel_;

  std::unique_ptr<network::SimpleURLLoader> manifest_loader_;
  std::unique_ptr<network::SimpleURLLoader> download_loader_;

  std::string installer_url_;
  std::string installer_sha256_;
  std::string installer_signer_cn_;
  base::FilePath verified_installer_path_;

  // Must be last: invalidates WeakPtrs on destruction so cross-thread tasks
  // posted with GetWeakPtr() silently no-op if |this| is destroyed first.
  base::WeakPtrFactory<PlatformUpdaterDelegateWin> weak_factory_{this};
};

std::unique_ptr<PlatformUpdaterDelegate> CreatePlatformUpdaterDelegate() {
  return std::make_unique<PlatformUpdaterDelegateWin>();
}

}  // namespace maho
