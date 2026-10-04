// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_LIVE_FOLDERS_MAHO_LIVE_FOLDER_ITEM_CACHE_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_LIVE_FOLDERS_MAHO_LIVE_FOLDER_ITEM_CACHE_H_

#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/no_destructor.h"

namespace maho {

// Lightweight shared item for live folder children rendered in the sidebar tree.
struct LiveFolderCachedItem {
  LiveFolderCachedItem();
  ~LiveFolderCachedItem();
  LiveFolderCachedItem(const LiveFolderCachedItem&);
  LiveFolderCachedItem& operator=(const LiveFolderCachedItem&);
  LiveFolderCachedItem(LiveFolderCachedItem&&);
  LiveFolderCachedItem& operator=(LiveFolderCachedItem&&);

  std::string id;
  std::string title;
  std::string subtitle;
  std::string url;
};

// Process-global cache of live folder items. Written by MahoLiveFolderPageHandler
// after network fetches; read by MahoSidebarStateAdapter::BuildTabListModel to
// inject children into live folder tree nodes.
//
// Keyed by config_json (unique per folder since each folder has distinct config).
class LiveFolderItemCache {
 public:
  static LiveFolderItemCache& GetInstance();

  void SetItems(const std::string& config_json,
                std::vector<LiveFolderCachedItem> items);

  const std::vector<LiveFolderCachedItem>* GetItems(
      const std::string& config_json) const;

  void Clear(const std::string& config_json);

 private:
  friend class base::NoDestructor<LiveFolderItemCache>;
  LiveFolderItemCache();
  ~LiveFolderItemCache();

  base::flat_map<std::string, std::vector<LiveFolderCachedItem>>
      items_by_config_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_LIVE_FOLDERS_MAHO_LIVE_FOLDER_ITEM_CACHE_H_
