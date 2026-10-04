// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_MEDIA_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_MEDIA_VIEW_H_

#include <cstddef>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_downloads_data.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

namespace views {
class Button;
class ImageView;
class Label;
class ScrollView;
class Textfield;
}  // namespace views

namespace maho {

enum class MediaKind {
  kAudio,
  kVideo,
  kImage,
  kOther,
};

enum class MediaActionKind {
  kOpen,
  kReveal,
};

class MahoSidebarMediaView : public views::View,
                             public views::TextfieldController {
  METADATA_HEADER(MahoSidebarMediaView, views::View)

 public:
  MahoSidebarMediaView();
  MahoSidebarMediaView(const MahoSidebarMediaView&) = delete;
  MahoSidebarMediaView& operator=(const MahoSidebarMediaView&) = delete;
  ~MahoSidebarMediaView() override;

  void OnThemeChanged() override;
  void ReloadMedia();
  void SetSidebarPalette(const MahoSidebarPalette& palette);

  views::ScrollView* scroll_view_for_testing() { return scroll_view_; }
  views::View* list_container_for_testing() { return list_container_; }
  views::View* empty_state_view_for_testing() {
    return empty_state_view_.get();
  }
  views::View* media_row_for_item_id_for_testing(const std::string& item_id);
  views::Button* media_action_button_for_item_id_for_testing(
      const std::string& item_id,
      size_t action_index);

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;

 private:
  void BuildUi();
  void UpdateAppearance();
  void ApplySearchFilter();
  void SetActiveFilter(std::optional<MediaKind> filter);
  void HandleMediaAction(const std::string& item_id, MediaActionKind action);

  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<views::View> list_container_ = nullptr;
  raw_ptr<views::Label> header_label_ = nullptr;
  raw_ptr<views::Label> empty_state_view_ = nullptr;
  raw_ptr<views::View> search_shell_ = nullptr;
  raw_ptr<views::ImageView> search_icon_ = nullptr;
  raw_ptr<views::Textfield> search_field_ = nullptr;
  raw_ptr<views::View> filters_container_ = nullptr;
  base::OneShotTimer search_debounce_timer_;
  std::optional<MediaKind> active_filter_;
  std::vector<DownloadItem> cached_media_items_;
  MahoSidebarPalette palette_;

  base::WeakPtrFactory<MahoSidebarMediaView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_MEDIA_VIEW_H_
