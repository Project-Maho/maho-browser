// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_sync/maho_sync_page_handler.h"

#include "maho/browser/ui/webui/maho_webui_private_boundary.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/webui/maho_auth_utils.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace {

// WebUI shows "Sign in to sync" when status_label=="error" &&
// error_message=="account_login_required".
maho_sync::mojom::SyncStatusPtr BuildAccountLoginRequiredStatus() {
  auto status = maho_sync::mojom::SyncStatus::New();
  status->is_syncing = false;
  status->relay_url = "";
  status->room_id = "";
  status->status_label = "error";
  status->error_message = "account_login_required";
  status->last_sync_timestamp = 0;
  return status;
}

maho_sync::mojom::SyncStatusPtr ParseSyncStatusJson(const std::string& json) {
  auto status = maho_sync::mojom::SyncStatus::New();
  status->is_syncing = false;
  status->relay_url = "";
  status->room_id = "";
  status->status_label = "idle";
  status->error_message = "";
  status->last_sync_timestamp = 0;

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return status;
  }
  const auto& dict = parsed->GetDict();

  const std::string* kind = dict.FindString("kind");
  if (kind) {
    status->status_label = *kind;
    status->is_syncing =
        (*kind == "syncing" || *kind == "synced" || *kind == "connecting");
  }

  const std::string* relay = dict.FindString("relay_url");
  if (relay) {
    status->relay_url = *relay;
  }

  const std::string* room = dict.FindString("room_id");
  if (room) {
    status->room_id = *room;
  }

  const std::string* error = dict.FindString("message");
  if (error) {
    status->error_message = *error;
  }

  const std::string* last_sync = dict.FindString("last_sync_at");
  if (last_sync) {
    // Stored as ISO 8601 string; timestamp conversion left to the UI.
    status->last_sync_timestamp = 0;
  }

  return status;
}

std::vector<maho_sync::mojom::DeviceInfoPtr> ParseDevicesJson(
    const std::string& json) {
  std::vector<maho_sync::mojom::DeviceInfoPtr> devices;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return devices;
  }
  for (const auto& entry : parsed->GetList()) {
    const auto* dict = entry.GetIfDict();
    if (!dict) {
      continue;
    }
    auto device = maho_sync::mojom::DeviceInfo::New();
    const std::string* id = dict->FindString("id");
    const std::string* name = dict->FindString("name");
    const std::string* dtype = dict->FindString("deviceType");
    std::optional<bool> online = dict->FindBool("isOnline");
    device->id = id ? *id : "";
    device->name = name ? *name : "Unknown";
    device->device_type = dtype ? *dtype : "unknown";
    device->is_online = online.value_or(false);
    devices.push_back(std::move(device));
  }
  return devices;
}

}  // namespace

MahoSyncPageHandler::MahoSyncPageHandler(
    mojo::PendingReceiver<maho_sync::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_sync::mojom::Page> page,
    Profile* profile)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      profile_(profile) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // R-4 fail-closed: forged Mojo receiver bypasses config-level denial. Do not remove.
  if (!MahoIsWebUIEnabled(profile)) {
    receiver_.reset();
    page_.reset();
    return;
  }
  poll_timer_.Start(FROM_HERE, base::Seconds(5),
                    base::BindRepeating(&MahoSyncPageHandler::PollSyncStatus,
                                        weak_factory_.GetWeakPtr()));
}

MahoSyncPageHandler::~MahoSyncPageHandler() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

// static
maho_sync::mojom::SyncStatusPtr
MahoSyncPageHandler::BuildSyncStatusOnPool() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    auto status = maho_sync::mojom::SyncStatus::New();
    status->is_syncing = false;
    status->status_label = "idle";
    return status;
  }
  char* json_str = maho_core_get_sync_status(core);
  if (!json_str) {
    auto status = maho_sync::mojom::SyncStatus::New();
    status->is_syncing = false;
    status->status_label = "idle";
    return status;
  }
  std::string json(json_str);
  maho_string_free(json_str);
  return ParseSyncStatusJson(json);
}

// static
std::vector<maho_sync::mojom::DeviceInfoPtr>
MahoSyncPageHandler::BuildDeviceListOnPool() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return {};
  }
  char* json_str = maho_core_get_connected_devices(core);
  if (!json_str) {
    return {};
  }
  std::string json(json_str);
  maho_string_free(json_str);
  return ParseDevicesJson(json);
}

// static
maho_sync::mojom::SyncStatusPtr MahoSyncPageHandler::ConfigureSyncEncryptionOnPool(
    const std::string& relay_url,
    const std::string& recovery_phrase) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    auto status = maho_sync::mojom::SyncStatus::New();
    status->is_syncing = false;
    status->status_label = "error";
    status->error_message = "Core not available";
    return status;
  }
  char* result = maho_core_configure_sync_encryption(
      core, relay_url.c_str(), recovery_phrase.c_str());
  if (result) {
    std::string json(result);
    maho_string_free(result);
    std::optional<base::Value> parsed =
        base::JSONReader::Read(json, base::JSON_PARSE_RFC);
    if (parsed && parsed->is_dict()) {
      const auto& dict = parsed->GetDict();
      std::optional<bool> success = dict.FindBool("success");
      if (success && !*success) {
        auto status = maho_sync::mojom::SyncStatus::New();
        status->is_syncing = false;
        status->status_label = "error";
        const std::string* error = dict.FindString("error");
        status->error_message = error ? *error : "Unknown sync configuration error";
        return status;
      }
    }
  }
  return BuildSyncStatusOnPool();
}

// static
maho_sync::mojom::SyncStatusPtr MahoSyncPageHandler::JoinSyncOnPool(
    const std::string& room_id,
    const std::string& recovery_phrase) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    auto status = maho_sync::mojom::SyncStatus::New();
    status->is_syncing = false;
    status->status_label = "error";
    status->error_message = "Core not available";
    status->room_id = room_id;
    return status;
  }
  char* result = maho_core_join_sync(core, maho::auth::GetSyncRelayUrl().c_str(), recovery_phrase.c_str());
  if (result) {
    std::string json(result);
    maho_string_free(result);
    std::optional<base::Value> parsed =
        base::JSONReader::Read(json, base::JSON_PARSE_RFC);
    if (parsed && parsed->is_dict()) {
      const auto& dict = parsed->GetDict();
      std::optional<bool> success = dict.FindBool("success");
      if (success && !*success) {
        auto status = maho_sync::mojom::SyncStatus::New();
        status->is_syncing = false;
        status->status_label = "error";
        const std::string* error = dict.FindString("error");
        status->error_message = error ? *error : "Unknown sync join error";
        status->room_id = room_id;
        return status;
      }
    }
  }
  auto status = BuildSyncStatusOnPool();
  if (status && status->room_id.empty()) {
    status->room_id = room_id;
  }
  return status;
}

// static
void MahoSyncPageHandler::StopSyncOnPool() {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }
  maho_core_stop_sync(core);
}

// static
MahoSyncPageHandler::GenerateSyncKeyResult
MahoSyncPageHandler::GenerateSyncKeyOnPool() {
  GenerateSyncKeyResult result;
  MahoCore* core = maho::GetCore();
  if (!core) {
    return result;
  }
  char* json_str = maho_core_generate_sync_key(core);
  if (!json_str) {
    return result;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return result;
  }
  const auto& dict = parsed->GetDict();
  const std::string* sk = dict.FindString("syncKey");
  const std::string* ri = dict.FindString("roomId");
  const std::string* rp = dict.FindString("recoveryPhrase");
  if (sk) result.sync_key = *sk;
  if (ri) result.room_id = *ri;
  if (rp) result.recovery_phrase = *rp;
  return result;
}

void MahoSyncPageHandler::ConfigureSyncEncryption(const std::string& recovery_phrase,
                                                   ConfigureSyncEncryptionCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::auth::HasValidRelaySession(profile_->GetPrefs())) {
    std::move(callback).Run(BuildAccountLoginRequiredStatus());
    return;
  }
  maho::PostCoreTask<maho_sync::mojom::SyncStatusPtr>(
      FROM_HERE,
      base::BindOnce(&MahoSyncPageHandler::ConfigureSyncEncryptionOnPool,
                     maho::auth::GetSyncRelayUrl(), recovery_phrase),
      base::BindOnce(&MahoSyncPageHandler::OnJoinSyncComplete,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoSyncPageHandler::JoinSync(const std::string& room_id,
                                    const std::string& passphrase,
                                    JoinSyncCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!maho::auth::HasValidRelaySession(profile_->GetPrefs())) {
    std::move(callback).Run(BuildAccountLoginRequiredStatus());
    return;
  }
  maho::PostCoreTask<maho_sync::mojom::SyncStatusPtr>(
      FROM_HERE,
      base::BindOnce(&MahoSyncPageHandler::JoinSyncOnPool, room_id,
                     passphrase),
      base::BindOnce(&MahoSyncPageHandler::OnJoinSyncComplete,
                      weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoSyncPageHandler::GetSyncStatus(GetSyncStatusCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask<maho_sync::mojom::SyncStatusPtr>(
      FROM_HERE,
      base::BindOnce(&MahoSyncPageHandler::BuildSyncStatusOnPool),
      base::BindOnce(&MahoSyncPageHandler::OnSyncStatusResult,
                      weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoSyncPageHandler::StopSync() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreClosure(
      FROM_HERE,
      base::BindOnce(&MahoSyncPageHandler::StopSyncOnPool));
}

void MahoSyncPageHandler::GetConnectedDevices(
    GetConnectedDevicesCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask<std::vector<maho_sync::mojom::DeviceInfoPtr>>(
      FROM_HERE,
      base::BindOnce(&MahoSyncPageHandler::BuildDeviceListOnPool),
      base::BindOnce(&MahoSyncPageHandler::OnDeviceListResult,
                      weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoSyncPageHandler::GenerateSyncKey(
    GenerateSyncKeyCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask<GenerateSyncKeyResult>(
      FROM_HERE,
      base::BindOnce(&MahoSyncPageHandler::GenerateSyncKeyOnPool),
      base::BindOnce(&MahoSyncPageHandler::OnGenerateSyncKeyResult,
                      weak_factory_.GetWeakPtr(), std::move(callback)));
}

void MahoSyncPageHandler::OnJoinSyncComplete(
    JoinSyncCallback callback,
    maho_sync::mojom::SyncStatusPtr status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(std::move(status));
}

void MahoSyncPageHandler::OnSyncStatusResult(
    GetSyncStatusCallback callback,
    maho_sync::mojom::SyncStatusPtr status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(std::move(status));
}

void MahoSyncPageHandler::OnDeviceListResult(
    GetConnectedDevicesCallback callback,
    std::vector<maho_sync::mojom::DeviceInfoPtr> devices) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(std::move(devices));
}

void MahoSyncPageHandler::OnGenerateSyncKeyResult(
    GenerateSyncKeyCallback callback,
    GenerateSyncKeyResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(result.sync_key, result.room_id,
                           result.recovery_phrase);
}

void MahoSyncPageHandler::PollSyncStatus() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  maho::PostCoreTask<maho_sync::mojom::SyncStatusPtr>(
      FROM_HERE,
      base::BindOnce(&MahoSyncPageHandler::BuildSyncStatusOnPool),
      base::BindOnce(
          [](base::WeakPtr<MahoSyncPageHandler> handler,
             maho_sync::mojom::SyncStatusPtr status) {
            if (!handler) {
              return;
            }
            if (status->status_label != handler->last_status_label_) {
              handler->last_status_label_ = status->status_label;
              handler->page_->OnSyncStatusChanged(status->Clone());
            }
          },
          weak_factory_.GetWeakPtr()));
}
