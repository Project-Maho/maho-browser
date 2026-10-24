// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_LIVE_FOLDERS_MAHO_LIVE_FOLDER_CACHE_WARMER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_LIVE_FOLDERS_MAHO_LIVE_FOLDER_CACHE_WARMER_H_

class Profile;

namespace maho {

// Reads live folder prefs and fires network fetches to populate
// LiveFolderItemCache before the user opens chrome://maho-live-folders, so
// inline expand/collapse in the sidebar shows cached children immediately.
//
// Called from MahoSidebarView construction. The first call for a profile also
// starts a profile-owned refresh timer that refetches stale folders for as
// long as the profile lives. Each fetch batch self-destructs when done.
class LiveFolderCacheWarmer {
 public:
  // Fetches every configured folder whose cached items are missing or stale,
  // and ensures the periodic refresh for |profile| is running.
  static void WarmCache(Profile* profile);

 private:
  LiveFolderCacheWarmer() = delete;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_LIVE_FOLDERS_MAHO_LIVE_FOLDER_CACHE_WARMER_H_
