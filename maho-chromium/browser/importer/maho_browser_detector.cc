// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/importer/maho_browser_detector.h"

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/json/json_reader.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace maho {

std::vector<DetectedBrowser> DetectInstalledBrowsers() {
  std::vector<DetectedBrowser> results;

  char* json = maho_import_detect_browsers();
  if (!json) return results;
  std::string json_str(json);
  maho_import_string_free(json);

  auto parsed = base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) return results;

  for (const auto& v : parsed->GetList()) {
    const auto* d = v.GetIfDict();
    if (!d) continue;

    DetectedBrowser browser;
    if (auto* s = d->FindString("display_name")) browser.display_name = *s;
    if (auto* s = d->FindString("profile_path"))
      browser.profile_path = base::FilePath::FromUTF8Unsafe(*s);
    if (auto i = d->FindInt("services_supported"))
      browser.services_supported = static_cast<uint32_t>(*i);
    if (auto b = d->FindBool("requires_full_disk_access"))
      browser.requires_full_disk_access = *b;

    if (auto* type_str = d->FindString("browser_type")) {
      if (*type_str == "Chrome") browser.type = BrowserType::kChrome;
      else if (*type_str == "Arc") browser.type = BrowserType::kArc;
      else if (*type_str == "Brave") browser.type = BrowserType::kBrave;
      else if (*type_str == "Edge") browser.type = BrowserType::kEdge;
      else if (*type_str == "Vivaldi") browser.type = BrowserType::kVivaldi;
      else if (*type_str == "Opera") browser.type = BrowserType::kOpera;
      else if (*type_str == "Firefox") browser.type = BrowserType::kFirefox;
      else if (*type_str == "Zen") browser.type = BrowserType::kZen;
      else if (*type_str == "Safari") browser.type = BrowserType::kSafari;
      else continue;
    } else {
      continue;
    }

    // Rust returns directories for Chromium/Firefox; callers expect file paths.
    switch (browser.type) {
      case BrowserType::kChrome:
      case BrowserType::kArc:
      case BrowserType::kBrave:
      case BrowserType::kEdge:
      case BrowserType::kVivaldi:
      case BrowserType::kOpera:
        browser.profile_path = browser.profile_path.Append(FILE_PATH_LITERAL("Bookmarks"));
        break;
      case BrowserType::kFirefox:
      case BrowserType::kZen:
        browser.profile_path = browser.profile_path.Append(FILE_PATH_LITERAL("places.sqlite"));
        break;
      case BrowserType::kSafari:
        break;
    }

    results.push_back(std::move(browser));
  }

  return results;
}

}  // namespace maho
