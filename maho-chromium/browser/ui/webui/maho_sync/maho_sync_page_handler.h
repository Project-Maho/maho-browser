// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SYNC_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SYNC_PAGE_HANDLER_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/timer/timer.h"
#include "maho/browser/ui/webui/maho_sync/maho_sync.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

class Profile;

class MahoSyncPageHandler : public maho_sync::mojom::PageHandler {
 public:
  MahoSyncPageHandler(
      mojo::PendingReceiver<maho_sync::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_sync::mojom::Page> page,
      Profile* profile);
  MahoSyncPageHandler(const MahoSyncPageHandler&) = delete;
  MahoSyncPageHandler& operator=(const MahoSyncPageHandler&) = delete;
  ~MahoSyncPageHandler() override;

  // maho_sync::mojom::PageHandler:
  void ConfigureSyncEncryption(const std::string& recovery_phrase,
                               ConfigureSyncEncryptionCallback callback) override;
  void JoinSync(const std::string& room_id,
                const std::string& passphrase,
                JoinSyncCallback callback) override;
  void GetSyncStatus(GetSyncStatusCallback callback) override;
  void StopSync() override;
  void GetConnectedDevices(GetConnectedDevicesCallback callback) override;
  void GenerateSyncKey(GenerateSyncKeyCallback callback) override;

 private:
  static maho_sync::mojom::SyncStatusPtr BuildSyncStatusOnPool();
  static std::vector<maho_sync::mojom::DeviceInfoPtr>
  BuildDeviceListOnPool();
  static maho_sync::mojom::SyncStatusPtr ConfigureSyncEncryptionOnPool(
      const std::string& relay_url,
      const std::string& recovery_phrase);
  static maho_sync::mojom::SyncStatusPtr JoinSyncOnPool(
      const std::string& room_id,
      const std::string& recovery_phrase);
  static void StopSyncOnPool();

  struct GenerateSyncKeyResult {
    std::string sync_key;
    std::string room_id;
    std::string recovery_phrase;
  };
  static GenerateSyncKeyResult GenerateSyncKeyOnPool();

  void OnJoinSyncComplete(JoinSyncCallback callback,
                          maho_sync::mojom::SyncStatusPtr status);
  void OnSyncStatusResult(GetSyncStatusCallback callback,
                          maho_sync::mojom::SyncStatusPtr status);
  void OnDeviceListResult(
      GetConnectedDevicesCallback callback,
      std::vector<maho_sync::mojom::DeviceInfoPtr> devices);
  void OnGenerateSyncKeyResult(GenerateSyncKeyCallback callback,
                               GenerateSyncKeyResult result);

  void PollSyncStatus();

  mojo::Receiver<maho_sync::mojom::PageHandler> receiver_;
  mojo::Remote<maho_sync::mojom::Page> page_;
  raw_ptr<Profile> profile_;

  base::RepeatingTimer poll_timer_;
  std::string last_status_label_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoSyncPageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SYNC_PAGE_HANDLER_H_
