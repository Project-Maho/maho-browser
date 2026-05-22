// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DOWNLOADS_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DOWNLOADS_VIEW_H_

#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"

namespace views {
class Button;
class Label;
class ScrollView;
}  // namespace views

namespace maho {

enum class DownloadActionKind {
  kPause,
  kResume,
  kCancel,
  kRemove,
  kOpen,
  kReveal,
};

class MahoSidebarDownloadsView : public views::View {
  METADATA_HEADER(MahoSidebarDownloadsView, views::View)

 public:
  MahoSidebarDownloadsView();
  MahoSidebarDownloadsView(const MahoSidebarDownloadsView&) = delete;
  MahoSidebarDownloadsView& operator=(const MahoSidebarDownloadsView&) = delete;
  ~MahoSidebarDownloadsView() override;

  void ReloadDownloads();
  void SetDownloadFileActionCallbackForTesting(
      base::RepeatingCallback<void(const std::string&, DownloadActionKind)>
          callback);

  views::ScrollView* scroll_view_for_testing() { return scroll_view_; }
  views::View* list_container_for_testing() { return list_container_; }
  views::View* empty_state_view_for_testing() {
    return empty_state_view_.get();
  }

  views::View* download_row_for_download_id_for_testing(
      const std::string& download_id);
  views::Button* download_action_button_for_testing(
      const std::string& download_id,
      size_t action_index);

 private:
  void BuildUi();
  void HandleDownloadAction(const std::string& download_id,
                            DownloadActionKind action);

  base::RepeatingCallback<void(const std::string&, DownloadActionKind)>
      download_file_action_callback_for_testing_;

  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<views::View> list_container_ = nullptr;
  raw_ptr<views::View> empty_state_view_ = nullptr;
  raw_ptr<views::Label> header_summary_label_ = nullptr;

  base::WeakPtrFactory<MahoSidebarDownloadsView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DOWNLOADS_VIEW_H_
