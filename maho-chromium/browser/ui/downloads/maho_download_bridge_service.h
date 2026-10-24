// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_DOWNLOADS_MAHO_DOWNLOAD_BRIDGE_SERVICE_H_
#define MAHO_BROWSER_UI_DOWNLOADS_MAHO_DOWNLOAD_BRIDGE_SERVICE_H_

#include <optional>
#include <string>

#include "base/containers/flat_map.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/timer/timer.h"
#include "components/download/content/public/all_download_item_notifier.h"
#include "components/download/public/common/download_interrupt_reasons.h"
#include "components/download/public/common/download_item.h"
#include "components/keyed_service/core/keyed_service.h"

class Profile;

namespace content {
class DownloadManager;
}

namespace download {
class DownloadItem;
}

namespace maho {

class MahoDownloadBridgeService : public KeyedService,
                                  public download::AllDownloadItemNotifier::Observer {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnMahoDownloadsChanged() = 0;
  };

  explicit MahoDownloadBridgeService(Profile* profile);
  MahoDownloadBridgeService(const MahoDownloadBridgeService&) = delete;
  MahoDownloadBridgeService& operator=(const MahoDownloadBridgeService&) = delete;
  ~MahoDownloadBridgeService() override;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  void PauseDownload(const std::string& maho_id);
  void ResumeDownload(const std::string& maho_id);
  void CancelDownload(const std::string& maho_id);
  void RemoveDownload(const std::string& maho_id);

  // A download this session saw interrupted by an error (network, server,
  // disk...), as opposed to a user pause or cancel. MahoCore only records it
  // as paused (resumable) or cancelled (unrecoverable), so the downloads UI
  // overlays this to show a "failed" row with the reason.
  struct LiveFailure {
    std::u16string message;  // Localized interrupt reason.
    bool can_resume = false;
  };
  std::optional<LiveFailure> GetLiveFailure(const std::string& maho_id) const;

  // Localized short text for an interrupt reason, as Chromium's download UI
  // shows it.
  static std::u16string FailureMessageForReason(
      download::DownloadInterruptReason reason);

  // KeyedService:
  void Shutdown() override;

  // AllDownloadItemNotifier::Observer:
  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnDownloadUpdated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnDownloadRemoved(content::DownloadManager* manager,
                         download::DownloadItem* item) override;

 private:
  void ScheduleNotifyChanged(bool immediate);
  void FlushNotifyChanged();
  std::string BuildStartJson(download::DownloadItem* item);
  download::DownloadItem* GetDownloadItemByMahoId(const std::string& maho_id);

  raw_ptr<Profile> profile_;
  raw_ptr<content::DownloadManager> manager_;
  std::unique_ptr<download::AllDownloadItemNotifier> notifier_;

  base::flat_map<std::string, std::string> guid_to_maho_id_;
  base::flat_map<std::string, std::string> maho_id_to_guid_;
  base::flat_map<std::string, download::DownloadItem::DownloadState> last_state_;
  base::flat_map<std::string, bool> metadata_finalized_;
  // guid -> last error interrupt reason, for downloads currently INTERRUPTED
  // by something other than the user.
  base::flat_map<std::string, download::DownloadInterruptReason>
      failure_reason_;

  base::ObserverList<Observer> observers_;
  base::OneShotTimer throttle_timer_;

  base::WeakPtrFactory<MahoDownloadBridgeService> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_DOWNLOADS_MAHO_DOWNLOAD_BRIDGE_SERVICE_H_
