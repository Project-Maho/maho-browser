// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_LIVE_FOLDERS_MAHO_LIVE_FOLDER_ITEM_CACHE_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_LIVE_FOLDERS_MAHO_LIVE_FOLDER_ITEM_CACHE_H_

#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/no_destructor.h"
#include "base/time/time.h"

namespace maho {

// How long fetched items for |provider_type| stay fresh before the sidebar
// warmer and the live folders page refetch them.
base::TimeDelta LiveFolderRefreshInterval(const std::string& provider_type);

// Google Calendar events-list URL listing the next upcoming events of
// |calendar_id| as of |now| (timeMin is RFC 3339 UTC).
std::string LiveFolderGoogleCalendarEventsUrl(const std::string& calendar_id,
                                              base::Time now);

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

// Shared response parsers used by both MahoLiveFolderPageHandler and the
// LiveFolderCacheWarmer, so the cache holds identical items whichever one
// wrote last. Return std::nullopt when |json| is not a well-formed response
// (callers must then keep the previously cached items); an empty vector means
// the provider genuinely returned no items.
//
// GitHub PRs: id "pr_<number>", title "#<number> <title>", subtitle = author.
std::optional<std::vector<LiveFolderCachedItem>> ParseLiveFolderGitHubPRs(
    const std::string& json);
// Google Calendar events: id = event id, title = summary, subtitle = start.
std::optional<std::vector<LiveFolderCachedItem>>
ParseLiveFolderGoogleCalendarEvents(const std::string& json);
// Single "Open Google Calendar" item shown when no OAuth token is configured.
std::vector<LiveFolderCachedItem> LiveFolderGoogleCalendarPlaceholderItems();

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

  // When the items for |config_json| were last stored; null if absent.
  base::Time GetUpdateTime(const std::string& config_json) const;

  void Clear(const std::string& config_json);

 private:
  friend class base::NoDestructor<LiveFolderItemCache>;
  LiveFolderItemCache();
  ~LiveFolderItemCache();

  struct Entry {
    Entry();
    ~Entry();
    Entry(Entry&&);
    Entry& operator=(Entry&&);

    std::vector<LiveFolderCachedItem> items;
    base::Time updated;
  };

  base::flat_map<std::string, Entry> items_by_config_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_LIVE_FOLDERS_MAHO_LIVE_FOLDER_ITEM_CACHE_H_
