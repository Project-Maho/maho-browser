// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DOWNLOADS_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DOWNLOADS_VIEW_H_

#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "maho/browser/ui/downloads/maho_download_bridge_service.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class Browser;

#include <optional>
#include "maho/browser/ui/views/sidebar/maho_sidebar_downloads_data.h"

namespace views {
class Button;
class ImageView;
class Label;
class ScrollView;
class Textfield;
}  // namespace views

namespace maho {

using MahoDownloadItem = DownloadItem;

enum class DownloadActionKind {
  kPause,
  kResume,
  kCancel,
  kRemove,
  kOpen,
  kReveal,
};

class MahoSidebarDownloadsView : public views::View,
                                 public views::TextfieldController,
                                 public maho::MahoDownloadBridgeService::Observer {
  METADATA_HEADER(MahoSidebarDownloadsView, views::View)

 public:
  explicit MahoSidebarDownloadsView(Browser* browser);
  MahoSidebarDownloadsView(const MahoSidebarDownloadsView&) = delete;
  MahoSidebarDownloadsView& operator=(const MahoSidebarDownloadsView&) = delete;
  ~MahoSidebarDownloadsView() override;

  void ReloadDownloads();
  void SetDownloadsForTesting(std::vector<DownloadItem> downloads);
  void SetSearchQueryForTesting(const std::u16string& query);
  void SetDownloadFileActionCallbackForTesting(
      base::RepeatingCallback<void(const std::string&, DownloadActionKind)>
           callback);
  void SetSidebarPalette(const MahoSidebarPalette& palette);

  views::ScrollView* scroll_view_for_testing() { return scroll_view_; }
  views::View* list_container_for_testing() { return list_container_; }
  views::View* empty_state_view_for_testing() {
    return empty_state_view_.get();
  }
  views::View* search_shell_for_testing() { return search_shell_.get(); }
  views::ImageView* search_icon_for_testing() { return search_icon_.get(); }
  views::Textfield* search_field_for_testing() { return search_field_.get(); }

  views::View* download_row_for_download_id_for_testing(
      const std::string& download_id);
  views::Button* download_action_button_for_testing(
      const std::string& download_id,
      size_t action_index);

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;

  // maho::MahoDownloadBridgeService::Observer:
  void OnMahoDownloadsChanged() override;

 private:
  void OnThemeChanged() override;

  void BuildUi();
  void ApplyPalette();
  void ApplySearchFilter();
  void HandleDownloadAction(const std::string& download_id,
                            DownloadActionKind action);
  // Reply (on the UI thread) of the async file-existence probe for Open/Reveal:
  // opens/reveals only when the file still exists, avoiding a silent no-op for
  // downloads whose file was moved or deleted.
  void HandleResolvedDownloadFileAction(DownloadActionKind action,
                                        base::FilePath path,
                                        bool exists);

  raw_ptr<Browser> browser_ = nullptr;

  base::RepeatingCallback<void(const std::string&, DownloadActionKind)>
      download_file_action_callback_for_testing_;

  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<views::View> list_container_ = nullptr;
  raw_ptr<views::View> empty_state_view_ = nullptr;
  raw_ptr<views::View> search_shell_ = nullptr;
  raw_ptr<views::ImageView> search_icon_ = nullptr;
  raw_ptr<views::Textfield> search_field_ = nullptr;
  base::OneShotTimer search_debounce_timer_;

  std::optional<std::vector<DownloadItem>> testing_downloads_;
  MahoSidebarPalette palette_;

  base::WeakPtrFactory<MahoSidebarDownloadsView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DOWNLOADS_VIEW_H_
