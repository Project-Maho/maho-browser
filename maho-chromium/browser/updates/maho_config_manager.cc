// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/updates/maho_config_manager.h"

#include <cstddef>
#include <stdint.h>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/updates/maho_update_pref_names.h"
#include "maho/browser/updates/maho_update_server_url.h"
#include "maho/browser/updates/maho_update_signature.h"
#include "maho/browser/updates/manifest_pubkey.h"
#include "maho/browser/updates/rollout_bucket.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "url/gurl.h"

namespace maho {
namespace updates {

namespace {
constexpr int kCompiledBrainCapabilityVersion = 1;
constexpr size_t kMaxManifestBodyBytes = 64 * 1024;
}

// static
MahoConfigManager* MahoConfigManager::GetInstance() {
  return base::Singleton<MahoConfigManager>::get();
}

MahoConfigManager::MahoConfigManager() {}
MahoConfigManager::~MahoConfigManager() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void MahoConfigManager::Initialize(::Profile* profile) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (initialized_) return;
  initialized_ = true;
  profile_ = profile;

  LoadLastKnownGoodConfig();
  HandleRollbackIfNeeded();

  // Schedule periodic checks every 4 hours.
  update_timer_.Start(
      FROM_HERE, base::Hours(4),
      base::BindRepeating(&MahoConfigManager::CheckForConfigUpdate,
                          base::Unretained(this)));

  // Perform an initial check on startup.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&MahoConfigManager::CheckForConfigUpdate,
                     base::Unretained(this)),
      base::Seconds(5));
}

void MahoConfigManager::LoadLastKnownGoodConfig() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  PrefService* local_state = g_browser_process ? g_browser_process->local_state() : nullptr;
  if (!local_state || !profile_) return;

  const std::string lkg_version =
      local_state->GetString(prefs::kMahoUpdateLastKnownGoodVersion);
  if (lkg_version.empty()) return;

  int parsed_version = 0;
  if (!base::StringToInt(lkg_version, &parsed_version)) {
    LOG(WARNING) << "MahoConfigManager: Ignoring non-numeric LKG version: "
                 << lkg_version;
    return;
  }

  base::FilePath lkg_path = profile_->GetPath()
                                .AppendASCII("maho_config")
                                .AppendASCII("config_v" + lkg_version + ".json");
  if (!base::PathExists(lkg_path)) return;

  LOG(INFO) << "MahoConfigManager: Loading last-known-good configuration: "
            << lkg_version;
  if (ApplyConfigToCore(lkg_path)) {
    SignalReload(lkg_path, parsed_version);
  } else {
    LOG(ERROR) << "MahoConfigManager: Failed to apply last-known-good "
                  "configuration: "
               << lkg_path.value();
  }
}

void MahoConfigManager::HandleRollbackIfNeeded() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  PrefService* local_state = g_browser_process ? g_browser_process->local_state() : nullptr;
  if (!local_state || !profile_) return;

  if (local_state->GetBoolean(prefs::kMahoUpdateRollbackRequested)) {
    bool clear_flag = true;
    std::string lkg_version = local_state->GetString(prefs::kMahoUpdateLastKnownGoodVersion);
    int parsed_version = 0;
    if (!lkg_version.empty() && base::StringToInt(lkg_version, &parsed_version)) {
      base::FilePath config_dir = profile_->GetPath().AppendASCII("maho_config");
      base::FilePath lkg_path = config_dir.AppendASCII("config_v" + lkg_version + ".json");
      if (base::PathExists(lkg_path)) {
        LOG(WARNING) << "MahoConfigManager: Rollback requested. Reverting to LKG configuration: " << lkg_version;
        if (ApplyConfigToCore(lkg_path)) {
          SignalReload(lkg_path, parsed_version);
        } else {
          LOG(ERROR) << "MahoConfigManager: Failed to apply rollback "
                        "configuration; will retry on next start: "
                     << lkg_path.value();
          clear_flag = false;
        }
      }
    } else if (!lkg_version.empty()) {
      LOG(WARNING) << "MahoConfigManager: Rollback requested but LKG version is non-numeric: "
                   << lkg_version;
    }
    if (clear_flag) {
      local_state->SetBoolean(prefs::kMahoUpdateRollbackRequested, false);
    }
  }
}

void MahoConfigManager::CheckForConfigUpdate() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  PrefService* local_state = g_browser_process ? g_browser_process->local_state() : nullptr;
  if (!local_state) return;

  if (!local_state->GetBoolean(prefs::kMahoConfigEnabled)) {
    LOG(INFO) << "MahoConfigManager: Config updates disabled by kill-switch.";
    return;
  }

  if (manifest_loader_) return; // check in progress

  std::string install_id = local_state->GetString(prefs::kMahoUpdateInstallId);
  if (install_id.empty()) {
    install_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
    local_state->SetString(prefs::kMahoUpdateInstallId, install_id);
  }
  int bucket = RolloutBucket::Compute(install_id);

  std::string channel = "stable";
  if (profile_ && profile_->GetPrefs()) {
    channel = profile_->GetPrefs()->GetString(prefs::kMahoUpdateChannel);
  }

  // Build request URL
  std::string server_url = ResolveUpdateServerBaseUrl(profile_);
  std::string url_str = server_url + "/updates/config?channel=" + channel + "&b=" + std::to_string(bucket);

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(url_str);
  request->load_flags = net::LOAD_DISABLE_CACHE;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("maho_config_update", R"(
        semantics {
          sender: "Maho Config Update Manager"
          description: "Fetches signed runtime configuration updates."
          trigger: "Periodic check every 4 hours or startup."
          data: "Rollout bucket value and update channel."
          destination: OTHER
        }
        policy {
          cookies_allowed: NO
          setting: "Gated by the maho.config.enabled pref."
        }
      )");

  manifest_loader_ = network::SimpleURLLoader::Create(std::move(request), traffic_annotation);
  auto* factory = g_browser_process->shared_url_loader_factory().get();
  manifest_loader_->DownloadToString(
      factory,
      base::BindOnce(&MahoConfigManager::OnManifestFetched,
                     base::Unretained(this)),
      kMaxManifestBodyBytes);
}

void MahoConfigManager::OnManifestFetched(std::optional<std::string> body) {
  std::unique_ptr<network::SimpleURLLoader> loader = std::move(manifest_loader_);
  if (!body.has_value() || loader->NetError() != net::OK) {
    LOG(WARNING) << "MahoConfigManager: Failed to fetch config manifest: net_error=" << loader->NetError();
    return;
  }

  auto envelope = base::JSONReader::Read(body.value(), base::JSON_PARSE_RFC);
  if (!envelope || !envelope->is_dict()) {
    LOG(WARNING) << "MahoConfigManager: Invalid JSON envelope.";
    return;
  }
  const auto& env_dict = envelope->GetDict();
  const std::string* sig_b64 = env_dict.FindString("signature");
  const std::string* payload_str = env_dict.FindString("payload");
  std::optional<int> version = env_dict.FindInt("version");

  if (!sig_b64 || !payload_str || !version) {
    LOG(WARNING) << "MahoConfigManager: Missing signature, payload, or version in envelope.";
    return;
  }

  // Verify signature
  std::string sig_bytes;
  if (!base::Base64Decode(*sig_b64, &sig_bytes)) {
    LOG(WARNING) << "MahoConfigManager: Failed to decode signature.";
    return;
  }

  std::vector<uint8_t> sig_vec(sig_bytes.begin(), sig_bytes.end());
  if (!VerifyManifestSignature(*payload_str, sig_vec, kMahoUpdateManifestPublicKey)) {
    LOG(ERROR) << "MahoConfigManager: Manifest signature verification failed!";
    return;
  }

  // Parse payload
  auto payload = base::JSONReader::Read(*payload_str, base::JSON_PARSE_RFC);
  if (!payload || !payload->is_dict()) {
    LOG(WARNING) << "MahoConfigManager: Invalid JSON payload.";
    return;
  }
  const auto& payload_dict = payload->GetDict();
  const std::string* url = payload_dict.FindString("url");
  const std::string* sha256 = payload_dict.FindString("sha256");

  if (!url || !sha256) {
    LOG(WARNING) << "MahoConfigManager: Missing url or sha256 in payload.";
    return;
  }

  if (*version <= active_version_) {
    LOG(INFO) << "MahoConfigManager: Configuration is up to date (current: " << active_version_ << ", latest: " << *version << ")";
    return;
  }

  downloading_url_ = *url;
  downloading_sha256_ = *sha256;
  downloading_version_ = *version;

  // Download blob
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(downloading_url_);
  request->load_flags = net::LOAD_DISABLE_CACHE;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("maho_config_blob_download", R"(
        semantics {
          sender: "Maho Config Blob Downloader"
          description: "Downloads the config blob payload."
          trigger: "New configuration version identified in manifest."
          data: "None."
          destination: OTHER
        }
        policy {
          cookies_allowed: NO
          setting: "Gated by the maho.config.enabled pref."
        }
      )");

  base::FilePath temp_dir;
  if (!base::PathService::Get(base::DIR_TEMP, &temp_dir)) {
    LOG(ERROR) << "MahoConfigManager: Failed to get temp directory.";
    return;
  }
  base::FilePath temp_file = temp_dir.AppendASCII("maho_config_temp_" + base::Uuid::GenerateRandomV4().AsLowercaseString() + ".json");

  blob_loader_ = network::SimpleURLLoader::Create(std::move(request), traffic_annotation);
  auto* factory = g_browser_process->shared_url_loader_factory().get();
  blob_loader_->DownloadToFile(
      factory,
      base::BindOnce(&MahoConfigManager::OnBlobDownloaded,
                     base::Unretained(this)),
      temp_file);
}

void MahoConfigManager::OnBlobDownloaded(base::FilePath temp_path) {
  std::unique_ptr<network::SimpleURLLoader> loader = std::move(blob_loader_);
  if (temp_path.empty() || loader->NetError() != net::OK) {
    LOG(WARNING) << "MahoConfigManager: Failed to download config blob: net_error=" << loader->NetError();
    return;
  }

  InstallDownloadedBlob(std::move(temp_path));
}

void MahoConfigManager::InstallDownloadedBlobForTesting(
    const base::FilePath& temp_path,
    const std::string& sha256,
    int version) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  downloading_sha256_ = sha256;
  downloading_version_ = version;
  InstallDownloadedBlob(temp_path);
}

void MahoConfigManager::InstallDownloadedBlob(base::FilePath temp_path) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Verify hash
  if (!VerifySha256(temp_path, downloading_sha256_)) {
    LOG(ERROR) << "MahoConfigManager: Hash verification failed for config blob!";
    base::DeleteFile(temp_path);
    return;
  }

  // Semantic skew verification (HC4): parse blob and check min brain version
  std::string file_content;
  if (base::ReadFileToString(temp_path, &file_content)) {
    auto parsed = base::JSONReader::Read(file_content, base::JSON_PARSE_RFC);
    if (parsed && parsed->is_dict()) {
      std::optional<int> min_brain_version = parsed->GetDict().FindInt("min_brain_version");
      if (min_brain_version && *min_brain_version > kCompiledBrainCapabilityVersion) {
        LOG(ERROR) << "MahoConfigManager: Config version " << downloading_version_
                   << " requires brain version " << *min_brain_version
                   << " (compiled brain version: " << kCompiledBrainCapabilityVersion
                   << "). Rejecting config payload (fails closed).";
        base::DeleteFile(temp_path);
        return;
      }
    }
  }

  // Ensure config directory exists
  base::FilePath config_dir = profile_->GetPath().AppendASCII("maho_config");
  if (!base::CreateDirectory(config_dir)) {
    LOG(ERROR) << "MahoConfigManager: Failed to create configuration directory: " << config_dir.value();
    base::DeleteFile(temp_path);
    return;
  }

  base::FilePath dest_path = config_dir.AppendASCII("config_v" + std::to_string(downloading_version_) + ".json");
  if (!base::Move(temp_path, dest_path)) {
    LOG(ERROR) << "MahoConfigManager: Failed to move config blob to destination: " << dest_path.value();
    base::DeleteFile(temp_path);
    return;
  }

  if (ApplyConfigToCore(dest_path)) {
    PrefService* local_state = g_browser_process ? g_browser_process->local_state() : nullptr;
    if (local_state) {
      local_state->SetString(prefs::kMahoUpdateLastKnownGoodVersion, std::to_string(downloading_version_));
    }
    SignalReload(dest_path, downloading_version_);
  } else {
    LOG(ERROR) << "MahoConfigManager: Config apply failed; not promoting v"
               << downloading_version_ << " to active/LKG.";
  }
}

bool MahoConfigManager::ApplyConfigToCore(const base::FilePath& path) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoCore* core = maho::GetCore();
  if (!core) {
    LOG(WARNING) << "MahoConfigManager: MahoCore unavailable; config apply deferred to active path only.";
    return false;
  }

  if (!apply_config_callback_for_testing_.is_null()) {
    return apply_config_callback_for_testing_.Run(core, path);
  }

  const bool ok = maho_core_apply_config(core, path.AsUTF8Unsafe().c_str());
  if (!ok) {
    LOG(ERROR) << "MahoConfigManager: maho_core_apply_config failed for "
               << path.value();
  }
  return ok;
}

void MahoConfigManager::SignalReload(const base::FilePath& path, int version) {
  active_config_path_ = path;
  active_version_ = version;
  LOG(INFO) << "MahoConfigManager: Configuration updated and reloaded: version=" << version << ", path=" << path.value();
}

base::FilePath MahoConfigManager::GetActiveConfigPath() const {
  return active_config_path_;
}

void MahoConfigManager::SetApplyConfigCallbackForTesting(
    ApplyConfigCallbackForTesting callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  apply_config_callback_for_testing_ = std::move(callback);
}

void MahoConfigManager::ResetForTesting() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  update_timer_.Stop();
  manifest_loader_.reset();
  blob_loader_.reset();
  initialized_ = false;
  profile_ = nullptr;
  active_config_path_.clear();
  active_version_ = 0;
  downloading_url_.clear();
  downloading_sha256_.clear();
  downloading_version_ = 0;
  apply_config_callback_for_testing_.Reset();
}

}  // namespace updates
}  // namespace maho
