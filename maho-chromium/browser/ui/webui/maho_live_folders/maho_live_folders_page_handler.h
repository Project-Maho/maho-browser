// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_LIVE_FOLDERS_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_LIVE_FOLDERS_PAGE_HANDLER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "maho/browser/ui/webui/maho_live_folders/maho_live_folders.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

class Profile;

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

// Manages live folder configurations and fetches items from external APIs.
class MahoLiveFolderPageHandler
    : public maho_live_folders::mojom::PageHandler {
 public:
  MahoLiveFolderPageHandler(
      mojo::PendingReceiver<maho_live_folders::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_live_folders::mojom::Page> page,
      Profile* profile);
  MahoLiveFolderPageHandler(const MahoLiveFolderPageHandler&) = delete;
  MahoLiveFolderPageHandler& operator=(const MahoLiveFolderPageHandler&) =
      delete;
  ~MahoLiveFolderPageHandler() override;

  // maho_live_folders::mojom::PageHandler:
  void GetFolders(GetFoldersCallback callback) override;
  void GetFolderItems(const std::string& folder_id,
                      GetFolderItemsCallback callback) override;
  void AddFolder(const std::string& provider_type,
                 const std::string& config_json,
                 AddFolderCallback callback) override;
  void RemoveFolder(const std::string& folder_id) override;
  void RefreshFolder(const std::string& folder_id) override;

 private:
  struct FolderConfig {
    FolderConfig();
    FolderConfig(const FolderConfig&);
    FolderConfig& operator=(const FolderConfig&);
    ~FolderConfig();

    std::string id;
    std::string name;
    std::string provider_type;
    std::string config_json;
  };

  // Fetch items from the external API for a folder.
  void FetchGitHubPRs(const FolderConfig& folder);
  void FetchGoogleCalendarEvents(const FolderConfig& folder);

  // Callbacks for URL fetches.
  void OnGitHubResponse(const std::string& folder_id,
                        std::optional<std::string> body);
  void OnGoogleCalendarResponse(const std::string& folder_id,
                                std::optional<std::string> body);

  // Parse API responses into LiveFolderItem structs.
  static std::vector<maho_live_folders::mojom::LiveFolderItemPtr>
  ParseGitHubPRs(const std::string& json);
  static std::vector<maho_live_folders::mojom::LiveFolderItemPtr>
  ParseGoogleCalendarEvents(const std::string& json);

  // Persist folder configs to prefs.
  void SaveFolders();
  void LoadFolders();

  // Schedule periodic refresh for a folder.
  void ScheduleRefresh(const std::string& folder_id);

  mojo::Receiver<maho_live_folders::mojom::PageHandler> receiver_;
  mojo::Remote<maho_live_folders::mojom::Page> page_;
  raw_ptr<Profile> profile_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;

  // Active folder configs keyed by folder ID.
  base::flat_map<std::string, FolderConfig> folders_;

  // Cached items keyed by folder ID.
  base::flat_map<std::string,
                 std::vector<maho_live_folders::mojom::LiveFolderItemPtr>>
      cached_items_;

  // Active URL loaders keyed by folder ID.
  base::flat_map<std::string, std::unique_ptr<network::SimpleURLLoader>>
      active_loaders_;

  // Refresh timers keyed by folder ID.
  base::flat_map<std::string, std::unique_ptr<base::RepeatingTimer>>
      refresh_timers_;

  // Pending GetFolderItems callbacks awaiting fetch completion.
  base::flat_map<std::string, GetFolderItemsCallback> pending_callbacks_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoLiveFolderPageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_LIVE_FOLDERS_PAGE_HANDLER_H_
