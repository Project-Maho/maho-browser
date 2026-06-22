// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_IMPORTER_MAHO_BROWSER_DETECTOR_H_
#define MAHO_BROWSER_IMPORTER_MAHO_BROWSER_DETECTOR_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/files/file_path.h"

namespace maho {

enum class BrowserType {
  kChrome,
  kArc,
  kBrave,
  kEdge,
  kVivaldi,
  kOpera,
  kFirefox,
  kZen,
  kSafari,
};

struct DetectedBrowser {
  BrowserType type;
  std::string display_name;       // e.g. "Google Chrome"
  base::FilePath profile_path;    // Bookmarks file (Chromium) or places.sqlite (Firefox/Zen) or Safari plist
  uint32_t services_supported;    // FAVORITES = 1<<1, etc. (matching user_data_importer flags)
  bool requires_full_disk_access; // true ONLY for Safari
};

// MUST be called on a thread pool with base::MayBlock().
std::vector<DetectedBrowser> DetectInstalledBrowsers();

}  // namespace maho

#endif  // MAHO_BROWSER_IMPORTER_MAHO_BROWSER_DETECTOR_H_
