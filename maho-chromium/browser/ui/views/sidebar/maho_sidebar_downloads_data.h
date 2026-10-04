// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DOWNLOADS_DATA_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DOWNLOADS_DATA_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace maho {

struct DownloadItem {
  DownloadItem();
  DownloadItem(const DownloadItem&);
  DownloadItem(DownloadItem&&);
  DownloadItem& operator=(const DownloadItem&);
  DownloadItem& operator=(DownloadItem&&);
  ~DownloadItem();

  std::string id;
  std::u16string filename;
  std::string url;
  std::string state;
  uint64_t total_bytes = 0;
  uint64_t received_bytes = 0;
  std::string started_at;
  std::optional<std::string> completed_at;
  std::optional<std::string> file_path;
  std::optional<std::string> mime_type;
  std::optional<std::string> error;
};

std::vector<DownloadItem> ParseDownloads();
bool HasRealFilePath(const DownloadItem& item);
bool IsMediaLikeDownload(const DownloadItem& item);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DOWNLOADS_DATA_H_
