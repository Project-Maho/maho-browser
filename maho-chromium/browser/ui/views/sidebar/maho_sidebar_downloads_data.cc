// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_sidebar_downloads_data.h"

#include <cmath>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/byte_size.h"
#include "base/json/json_reader.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/base/text/bytes_formatting.h"

namespace maho {

DownloadItem::DownloadItem() = default;
DownloadItem::DownloadItem(const DownloadItem&) = default;
DownloadItem::DownloadItem(DownloadItem&&) = default;
DownloadItem& DownloadItem::operator=(const DownloadItem&) = default;
DownloadItem& DownloadItem::operator=(DownloadItem&&) = default;
DownloadItem::~DownloadItem() = default;

namespace {

std::u16string FormatByteCount(uint64_t bytes) {
  return ui::FormatBytes(base::ByteSize(bytes));
}

bool EndsWithAny(std::string_view value,
                 std::initializer_list<std::string_view> suffixes) {
  for (std::string_view suffix : suffixes) {
    if (base::EndsWith(value, suffix, base::CompareCase::INSENSITIVE_ASCII)) {
      return true;
    }
  }
  return false;
}

bool IsMediaMimeType(std::string_view mime_type) {
  return base::StartsWith(mime_type, "audio/", base::CompareCase::SENSITIVE) ||
         base::StartsWith(mime_type, "video/", base::CompareCase::SENSITIVE) ||
         base::StartsWith(mime_type, "image/", base::CompareCase::SENSITIVE);
}

bool IsMediaFilePath(std::string_view path) {
  return EndsWithAny(path, {".mp3", ".m4a", ".aac", ".flac", ".wav",
                            ".mp4", ".m4v", ".mov", ".mkv", ".webm",
                            ".jpg", ".jpeg", ".png", ".gif", ".webp"});
}

}  // namespace

std::vector<DownloadItem> ParseDownloads() {
  std::vector<DownloadItem> downloads;
  // kMahoDownloadMetadata reads the process-global regular MahoCore. GetCore()
  // is null ONLY in an incognito-only standalone process (--incognito); in a
  // normal session MaybeInitializeForBrowser has already created the core, so
  // this null-guard does NOT by itself deny OTR reads. The actual OTR boundary
  // is the is_otr_ gate at the sidebar library entry (MahoSidebarView::
  // OpenLibraryCategory/OpenLibrary), which prevents any OTR window from
  // reaching this code path at all.
  MahoCore* core = maho::GetCore();
  if (!core) {
    return downloads;
  }

  char* json_str = maho_core_get_download_view_models(core);
  if (!json_str) {
    return downloads;
  }

  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return downloads;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }

    DownloadItem download;
    if (const std::string* id = dict->FindString("id")) {
      download.id = *id;
    }
    if (const std::string* filename = dict->FindString("filename")) {
      download.filename = base::UTF8ToUTF16(*filename);
    }
    if (const std::string* url = dict->FindString("url")) {
      download.url = *url;
    }
    if (const std::string* state = dict->FindString("state")) {
      download.state = *state;
    }
    if (auto total = dict->FindDouble("totalBytes")) {
      download.total_bytes = static_cast<uint64_t>(*total);
    }
    if (auto received = dict->FindDouble("receivedBytes")) {
      download.received_bytes = static_cast<uint64_t>(*received);
    }
    if (const std::string* started_at = dict->FindString("startedAt")) {
      download.started_at = *started_at;
    }
    if (const std::string* completed_at = dict->FindString("completedAt")) {
      download.completed_at = *completed_at;
    }
    if (const std::string* file_path = dict->FindString("filePath")) {
      download.file_path = *file_path;
    }
    if (const std::string* mime_type = dict->FindString("mimeType")) {
      download.mime_type = *mime_type;
    }
    if (const std::string* error = dict->FindString("error")) {
      download.error = *error;
    }

    if (!download.id.empty()) {
      downloads.push_back(std::move(download));
    }
  }

  return downloads;
}

bool HasRealFilePath(const DownloadItem& item) {
  // Must stay non-blocking: this runs on the UI thread during row render, so it
  // cannot call base::PathExists() (blocking I/O trips AssertBlockingAllowed and
  // aborts under DCHECK). A completed download with a path is treated as openable;
  // platform_util::OpenItem/ShowItemInFolder handle a since-deleted file safely.
  return item.state == "completed" && item.file_path.has_value() &&
         !item.file_path->empty();
}

std::u16string DownloadsIndicatorAccessibleDescription(
    const DownloadsIndicatorState& state) {
  if (!state.visible) {
    return std::u16string();
  }

  const std::u16string summary =
      state.active_count == 1
          ? u"Download in progress"
          : base::UTF8ToUTF16(std::to_string(state.active_count)) +
                u" downloads in progress";
  if (state.indeterminate) {
    return base::StrCat({summary, u", size unknown"});
  }

  const int percent = static_cast<int>(std::lround(state.fraction * 100.0));
  return base::StrCat(
      {summary, u", ", base::UTF8ToUTF16(std::to_string(percent)),
       u"% complete (", FormatByteCount(state.received_bytes), u" of ",
       FormatByteCount(state.total_bytes), u")"});
}

bool IsMediaLikeDownload(const DownloadItem& item) {
  if (item.mime_type.has_value() && !item.mime_type->empty() &&
      IsMediaMimeType(*item.mime_type)) {
    return true;
  }

  if (item.file_path.has_value() && !item.file_path->empty() &&
      IsMediaFilePath(*item.file_path)) {
    return true;
  }

  if (!item.filename.empty()) {
    return IsMediaFilePath(base::UTF16ToUTF8(item.filename));
  }

  return false;
}

}  // namespace maho
