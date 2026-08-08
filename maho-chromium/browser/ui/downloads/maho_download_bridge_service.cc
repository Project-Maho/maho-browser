// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/downloads/maho_download_bridge_service.h"

#include <optional>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_manager.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace maho {

namespace {

std::string ParseMahoId(char* raw_id) {
  if (!raw_id) return "";
  std::string json(raw_id);
  maho_string_free(raw_id);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (parsed && parsed->is_string()) {
    return parsed->GetString();
  }
  return "";
}

}  // namespace

MahoDownloadBridgeService::MahoDownloadBridgeService(Profile* profile)
    : profile_(profile) {
  manager_ = profile_->GetDownloadManager();
  if (manager_) {
    notifier_ = std::make_unique<download::AllDownloadItemNotifier>(manager_, this);
  }
}

MahoDownloadBridgeService::~MahoDownloadBridgeService() = default;

void MahoDownloadBridgeService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void MahoDownloadBridgeService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void MahoDownloadBridgeService::PauseDownload(const std::string& maho_id) {
  MahoCore* core = maho::GetCore();
  if (core) {
    maho_core_pause_download(core, maho_id.c_str());
  }
  download::DownloadItem* item = GetDownloadItemByMahoId(maho_id);
  if (item) {
    item->Pause();
  }
  ScheduleNotifyChanged(/*immediate=*/true);
}

void MahoDownloadBridgeService::ResumeDownload(const std::string& maho_id) {
  MahoCore* core = maho::GetCore();
  if (core) {
    maho_core_resume_download(core, maho_id.c_str());
  }
  download::DownloadItem* item = GetDownloadItemByMahoId(maho_id);
  if (item) {
    item->Resume(/*user_resume=*/true);
  }
  ScheduleNotifyChanged(/*immediate=*/true);
}

void MahoDownloadBridgeService::CancelDownload(const std::string& maho_id) {
  MahoCore* core = maho::GetCore();
  if (core) {
    maho_core_cancel_download(core, maho_id.c_str());
  }
  download::DownloadItem* item = GetDownloadItemByMahoId(maho_id);
  if (item) {
    item->Cancel(/*user_cancel=*/true);
  }
  ScheduleNotifyChanged(/*immediate=*/true);
}

void MahoDownloadBridgeService::RemoveDownload(const std::string& maho_id) {
  MahoCore* core = maho::GetCore();
  if (core) {
    maho_core_remove_download(core, maho_id.c_str());
  }
  download::DownloadItem* item = GetDownloadItemByMahoId(maho_id);
  if (item) {
    item->Remove();
  }
  auto it = maho_id_to_guid_.find(maho_id);
  if (it != maho_id_to_guid_.end()) {
    std::string guid = it->second;
    last_state_.erase(guid);
    guid_to_maho_id_.erase(guid);
    metadata_finalized_.erase(guid);
    maho_id_to_guid_.erase(it);
  }
  ScheduleNotifyChanged(/*immediate=*/true);
}

void MahoDownloadBridgeService::Shutdown() {
  notifier_.reset();
}

void MahoDownloadBridgeService::OnDownloadCreated(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  if (!item) return;
  if (profile_->IsOffTheRecord()) return;

  if (guid_to_maho_id_.contains(item->GetGuid())) return;

  MahoCore* core = maho::GetCore();
  if (!core) return;

  std::string maho_id;

  // Fix D2: Handle COMPLETE downloads (restore history / fast completions) instead of ignoring them.
  if (item->GetState() == download::DownloadItem::COMPLETE) {
    // 1. Try finding by chromium_guid
    char* raw_found_id = maho_core_find_download_by_chromium_guid(core, item->GetGuid().c_str());
    maho_id = ParseMahoId(raw_found_id);
    if (!maho_id.empty()) {
      guid_to_maho_id_[item->GetGuid()] = maho_id;
      maho_id_to_guid_[maho_id] = item->GetGuid();
      last_state_[item->GetGuid()] = item->GetState();
      // Backfill metadata
      std::string meta_json = BuildStartJson(item);
      maho_core_update_download_metadata(core, maho_id.c_str(), meta_json.c_str());
      ScheduleNotifyChanged(/*immediate=*/true);
      return;
    }

    // 2. Legacy fallback match (guid-less seed row) using url + (file_path or started_at)
    char* raw_list = maho_core_get_download_view_models(core);
    if (raw_list) {
      std::string list_json(raw_list);
      maho_string_free(raw_list);
      std::optional<base::Value> parsed_list =
          base::JSONReader::Read(list_json, base::JSON_PARSE_RFC);
      if (parsed_list && parsed_list->is_list()) {
        for (const auto& val : parsed_list->GetList()) {
          const auto* dict = val.GetIfDict();
          if (dict) {
            const std::string* dl_url = dict->FindString("url");
            // DownloadViewModel serializes camelCase (rename_all): file_path -> "filePath".
            const std::string* dl_file_path = dict->FindString("filePath");
            const std::string* dl_id = dict->FindString("id");
            if (dl_url && dl_id && *dl_url == item->GetURL().spec()) {
              bool path_match = false;
              if (dl_file_path && !dl_file_path->empty() && !item->GetTargetFilePath().empty()) {
                if (*dl_file_path == item->GetTargetFilePath().AsUTF8Unsafe()) {
                  path_match = true;
                }
              }
              if (path_match || (item->GetTargetFilePath().empty() && (!dl_file_path || dl_file_path->empty()))) {
                maho_id = *dl_id;
                guid_to_maho_id_[item->GetGuid()] = maho_id;
                maho_id_to_guid_[maho_id] = item->GetGuid();
                last_state_[item->GetGuid()] = item->GetState();
                // Backfill the GUID to SQLite
                std::string meta_json = BuildStartJson(item);
                maho_core_update_download_metadata(core, maho_id.c_str(), meta_json.c_str());
                ScheduleNotifyChanged(/*immediate=*/true);
                return;
              }
            }
          }
        }
      }
    }

    // 3. Not found anywhere (ingest-as-complete)
    std::string start_json = BuildStartJson(item);
    char* raw_id = maho_core_start_download(core, start_json.c_str());
    maho_id = ParseMahoId(raw_id);
    if (!maho_id.empty()) {
      guid_to_maho_id_[item->GetGuid()] = maho_id;
      maho_id_to_guid_[maho_id] = item->GetGuid();
      last_state_[item->GetGuid()] = item->GetState();
      
      char* raw_res = maho_core_complete_download(core, maho_id.c_str());
      if (raw_res) {
        maho_string_free(raw_res);
      }
      ScheduleNotifyChanged(/*immediate=*/true);
    }
    return;
  }

  // Actively downloading item
  std::string start_json = BuildStartJson(item);
  char* raw_id = maho_core_start_download(core, start_json.c_str());
  maho_id = ParseMahoId(raw_id);
  if (!maho_id.empty()) {
    guid_to_maho_id_[item->GetGuid()] = maho_id;
    maho_id_to_guid_[maho_id] = item->GetGuid();
    last_state_[item->GetGuid()] = item->GetState();
    ScheduleNotifyChanged(/*immediate=*/true);
  }
}

void MahoDownloadBridgeService::OnDownloadUpdated(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  if (!item) return;
  if (profile_->IsOffTheRecord()) return;

  auto it = guid_to_maho_id_.find(item->GetGuid());
  if (it == guid_to_maho_id_.end()) {
    // Fix D3: Remove COMPLETE drop in fallback -- delegate to OnDownloadCreated.
    if (item->GetState() != download::DownloadItem::CANCELLED) {
      OnDownloadCreated(manager, item);
    }
    return;
  }

  std::string maho_id = it->second;
  MahoCore* core = maho::GetCore();
  if (!core) return;

  maho_core_update_download_progress(core, maho_id.c_str(), item->GetReceivedBytes());

  // Fix D1: Update metadata immediately when target file path is determined
  bool finalized = metadata_finalized_[item->GetGuid()];
  if (!finalized && !item->GetTargetFilePath().empty()) {
    std::string meta_json = BuildStartJson(item);
    maho_core_update_download_metadata(core, maho_id.c_str(), meta_json.c_str());
    metadata_finalized_[item->GetGuid()] = true;
    ScheduleNotifyChanged(/*immediate=*/true);
  }

  download::DownloadItem::DownloadState state = item->GetState();
  auto last_it = last_state_.find(item->GetGuid());
  bool state_changed = (last_it == last_state_.end() || last_it->second != state);
  last_state_[item->GetGuid()] = state;

  bool is_state_transition = false;
  if (state_changed) {
    // Refresh metadata on transitions: the target file path / final name / size
    // are frequently unknown at OnDownloadCreated time, so re-send them here so a
    // completed download exposes Open/Reveal instead of only Remove.
    std::string meta_json = BuildStartJson(item);
    maho_core_update_download_metadata(core, maho_id.c_str(), meta_json.c_str());

    switch (state) {
      case download::DownloadItem::IN_PROGRESS:
        // Restores the Maho "downloading" state after a prior pause/interruption
        // so a resumed download does not stay stuck as paused/cancelled.
        maho_core_resume_download(core, maho_id.c_str());
        break;
      case download::DownloadItem::COMPLETE: {
        char* raw_res = maho_core_complete_download(core, maho_id.c_str());
        if (raw_res) {
          maho_string_free(raw_res);
        }
        break;
      }
      case download::DownloadItem::CANCELLED:
        maho_core_cancel_download(core, maho_id.c_str());
        break;
      case download::DownloadItem::INTERRUPTED:
        // Resumable interruptions surface as paused; unrecoverable ones as cancelled.
        if (item->CanResume()) {
          maho_core_pause_download(core, maho_id.c_str());
        } else {
          maho_core_cancel_download(core, maho_id.c_str());
        }
        break;
      default:
        break;
    }
    is_state_transition = true;
  }

  ScheduleNotifyChanged(/*immediate=*/is_state_transition);
}

void MahoDownloadBridgeService::OnDownloadRemoved(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  if (!item) return;

  auto it = guid_to_maho_id_.find(item->GetGuid());
  if (it != guid_to_maho_id_.end()) {
    std::string maho_id = it->second;
    guid_to_maho_id_.erase(it);
    maho_id_to_guid_.erase(maho_id);
    last_state_.erase(item->GetGuid());
    metadata_finalized_.erase(item->GetGuid());

    MahoCore* core = maho::GetCore();
    if (core) {
      maho_core_remove_download(core, maho_id.c_str());
    }
    ScheduleNotifyChanged(/*immediate=*/true);
  }
}

void MahoDownloadBridgeService::ScheduleNotifyChanged(bool immediate) {
  if (immediate) {
    throttle_timer_.Stop();
    FlushNotifyChanged();
  } else {
    if (!throttle_timer_.IsRunning()) {
      throttle_timer_.Start(
          FROM_HERE, base::Milliseconds(250),
          base::BindOnce(&MahoDownloadBridgeService::FlushNotifyChanged,
                         base::Unretained(this)));
    }
  }
}

void MahoDownloadBridgeService::FlushNotifyChanged() {
  for (auto& observer : observers_) {
    observer.OnMahoDownloadsChanged();
  }
}

std::string MahoDownloadBridgeService::BuildStartJson(download::DownloadItem* item) {
  base::DictValue dict;
  dict.Set("filename", item->GetFileNameToReportUser().AsUTF8Unsafe());
  dict.Set("url", item->GetURL().spec());
  dict.Set("total_bytes", static_cast<double>(item->GetTotalBytes()));
  dict.Set("chromium_guid", item->GetGuid());

  base::FilePath path = item->GetTargetFilePath();
  if (!path.empty()) {
    dict.Set("file_path", path.AsUTF8Unsafe());
  }

  std::string mime = item->GetMimeType();
  if (!mime.empty()) {
    dict.Set("mime_type", mime);
  }

  std::string json;
  base::JSONWriter::Write(dict, &json);
  return json;
}

download::DownloadItem* MahoDownloadBridgeService::GetDownloadItemByMahoId(
    const std::string& maho_id) {
  auto it = maho_id_to_guid_.find(maho_id);
  if (it == maho_id_to_guid_.end()) {
    return nullptr;
  }
  return manager_ ? manager_->GetDownloadByGuid(it->second) : nullptr;
}

}  // namespace maho
