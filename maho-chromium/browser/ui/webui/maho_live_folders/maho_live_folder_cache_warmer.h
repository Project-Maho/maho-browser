// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_LIVE_FOLDERS_MAHO_LIVE_FOLDER_CACHE_WARMER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_LIVE_FOLDERS_MAHO_LIVE_FOLDER_CACHE_WARMER_H_

class Profile;

namespace maho {

// Reads live folder prefs at browser startup and fires network fetches to
// populate LiveFolderItemCache before the user opens chrome://maho-live-folders.
// This ensures inline expand/collapse in the sidebar shows cached children
// immediately.
//
// Designed to be called once per profile from MahoSidebarView construction.
// Self-destructs after all fetches complete (or fail).
class LiveFolderCacheWarmer {
 public:
  // Kicks off cache warming. Caller does not own the returned pointer —
  // the warmer self-destructs when all fetches finish.
  static void WarmCache(Profile* profile);

 private:
  LiveFolderCacheWarmer() = delete;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_LIVE_FOLDERS_MAHO_LIVE_FOLDER_CACHE_WARMER_H_
