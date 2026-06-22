// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/importer/safari_bookmark_parser.h"

#include <string>
#include <vector>

#include "base/apple/foundation_util.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/sys_string_conversions.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_MAC)
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace maho::importer {

SafariParseResult::SafariParseResult() = default;
SafariParseResult::~SafariParseResult() = default;
SafariParseResult::SafariParseResult(SafariParseResult&&) = default;
SafariParseResult& SafariParseResult::operator=(SafariParseResult&&) = default;

#if BUILDFLAG(IS_MAC)

namespace {

void WalkChildren(CFArrayRef children, std::vector<maho::ParsedBookmark>* out, int depth) {
  if (!children || depth >= 32) {
    return;
  }

  CFIndex count = CFArrayGetCount(children);
  for (CFIndex i = 0; i < count; i++) {
    CFDictionaryRef item = static_cast<CFDictionaryRef>(
        CFArrayGetValueAtIndex(children, i));
    if (!item || CFGetTypeID(item) != CFDictionaryGetTypeID()) {
      continue;
    }

    CFStringRef type_ref = static_cast<CFStringRef>(
        CFDictionaryGetValue(item, CFSTR("WebBookmarkType")));
    if (!type_ref) {
      continue;
    }

    std::string type = base::SysCFStringRefToUTF8(type_ref);

    CFStringRef title_ref = static_cast<CFStringRef>(
        CFDictionaryGetValue(item, CFSTR("Title")));
    std::u16string title;
    if (title_ref && CFGetTypeID(title_ref) == CFStringGetTypeID()) {
      title = base::SysCFStringRefToUTF16(title_ref);
    }

    if (type == "WebBookmarkTypeList") {
      maho::ParsedBookmark folder;
      folder.title = title;
      folder.is_folder = true;

      CFArrayRef sub = static_cast<CFArrayRef>(
          CFDictionaryGetValue(item, CFSTR("Children")));
      if (sub && CFGetTypeID(sub) == CFArrayGetTypeID()) {
        WalkChildren(sub, &folder.children, depth + 1);
      }
      out->push_back(std::move(folder));

    } else if (type == "WebBookmarkTypeLeaf") {
      CFDictionaryRef uri_dict = static_cast<CFDictionaryRef>(
          CFDictionaryGetValue(item, CFSTR("URIDictionary")));
      CFStringRef url_ref = static_cast<CFStringRef>(
          CFDictionaryGetValue(item, CFSTR("URLString")));

      if (uri_dict && CFGetTypeID(uri_dict) == CFDictionaryGetTypeID()) {
        CFStringRef leaf_title = static_cast<CFStringRef>(
            CFDictionaryGetValue(uri_dict, CFSTR("title")));
        if (leaf_title && CFGetTypeID(leaf_title) == CFStringGetTypeID()) {
          title = base::SysCFStringRefToUTF16(leaf_title);
        }
      }

      if (!url_ref || CFGetTypeID(url_ref) != CFStringGetTypeID()) {
        continue;
      }

      std::string url_str = base::SysCFStringRefToUTF8(url_ref);
      GURL url(url_str);
      if (!url.is_valid()) {
        continue;
      }

      maho::ParsedBookmark bm;
      bm.title = title;
      bm.url = url;
      bm.is_folder = false;
      out->push_back(std::move(bm));
    }
  }
}

}  // namespace

SafariParseResult ParseSafariBookmarks(const base::FilePath& plist_path) {
  SafariParseResult result;

  FILE* f = fopen(plist_path.value().c_str(), "rb");
  if (!f) {
    if (errno == EACCES || errno == EPERM) {
      result.status = SafariParseStatus::kPermissionDenied;
      result.fda_required = true;
    } else {
      result.status = SafariParseStatus::kFileNotFound;
    }
    return result;
  }
  fclose(f);

  base::apple::ScopedCFTypeRef<CFDataRef> data;
  {
    std::string contents;
    if (!base::ReadFileToString(plist_path, &contents)) {
      result.status = SafariParseStatus::kPermissionDenied;
      result.fda_required = true;
      return result;
    }
    data.reset(CFDataCreate(kCFAllocatorDefault,
                            reinterpret_cast<const UInt8*>(contents.data()),
                            contents.size()));
  }

  if (!data) {
    result.status = SafariParseStatus::kPlistParseFailed;
    return result;
  }

  base::apple::ScopedCFTypeRef<CFPropertyListRef> plist(
      CFPropertyListCreateWithData(kCFAllocatorDefault, data.get(),
                                   kCFPropertyListImmutable, nullptr, nullptr));
  if (!plist || CFGetTypeID(plist.get()) != CFDictionaryGetTypeID()) {
    result.status = SafariParseStatus::kPlistParseFailed;
    return result;
  }

  CFDictionaryRef root = static_cast<CFDictionaryRef>(plist.get());
  CFArrayRef children = static_cast<CFArrayRef>(
      CFDictionaryGetValue(root, CFSTR("Children")));
  if (!children || CFGetTypeID(children) != CFArrayGetTypeID()) {
    result.status = SafariParseStatus::kPlistParseFailed;
    return result;
  }

  WalkChildren(children, &result.bookmarks, 0);
  result.status = SafariParseStatus::kSuccess;
  return result;
}

#else

SafariParseResult ParseSafariBookmarks(const base::FilePath& plist_path) {
  SafariParseResult result;
  result.status = SafariParseStatus::kUnsupportedPlatform;
  return result;
}

#endif

}  // namespace maho::importer
