// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DOWNLOADS_DATA_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DOWNLOADS_DATA_H_

#include <algorithm>
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

// Download states that still count as an unfinished transfer. This is the one
// predicate behind both the per-row progress bar
// (maho_sidebar_downloads_view.cc) and the Library rail indicator, so the two
// surfaces can never disagree: a paused transfer is unfinished, while
// completed/cancelled/failed/interrupted rows are terminal.
inline bool IsActiveDownloadState(const std::string& state) {
  return state == "downloading" || state == "paused";
}

// Aggregate progress for the Library rail's Downloads icon.
struct DownloadsIndicatorState {
  // False when no download is active: the rail then paints no indicator at
  // all, which is what clears it once the last active download ends.
  bool visible = false;
  // True when at least one active download has an unknown total size, so no
  // honest determinate fraction exists for the aggregate.
  bool indeterminate = false;
  int active_count = 0;
  // Byte weighting over the active downloads that DO report a total size.
  uint64_t received_bytes = 0;
  uint64_t total_bytes = 0;
  // received_bytes / total_bytes clamped to [0, 1]; 0 when indeterminate.
  double fraction = 0.0;
};

// Byte-weights the active downloads that report a total size and reports an
// indeterminate aggregate as soon as one active download's size is unknown.
// Terminal downloads contribute nothing, so the indicator clears itself when
// the last active download finishes, fails, or is cancelled.
//
// Deliberately inline and dependency-free (std only): the aggregation contract
// is then unit-testable without a browser, a Views widget, or the base library.
inline DownloadsIndicatorState ComputeDownloadsIndicatorState(
    const std::vector<DownloadItem>& downloads) {
  DownloadsIndicatorState state;
  for (const DownloadItem& download : downloads) {
    if (!IsActiveDownloadState(download.state)) {
      continue;
    }
    ++state.active_count;
    if (download.total_bytes == 0) {
      state.indeterminate = true;
      continue;
    }
    state.received_bytes += download.received_bytes;
    state.total_bytes += download.total_bytes;
  }

  state.visible = state.active_count > 0;
  if (state.indeterminate) {
    return state;
  }
  if (state.total_bytes > 0) {
    state.fraction = std::clamp(
        static_cast<double>(state.received_bytes) /
            static_cast<double>(state.total_bytes),
        0.0, 1.0);
  }
  return state;
}

// Accessible status for the rail icon, e.g. "2 downloads in progress, 37%
// complete (3.7 MB of 10.0 MB)" or "Download in progress, size unknown".
// Empty when the indicator is not visible, which clears the AX description.
std::u16string DownloadsIndicatorAccessibleDescription(
    const DownloadsIndicatorState& state);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DOWNLOADS_DATA_H_
