// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_MEDIA_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_MEDIA_VIEW_H_

#include <cstddef>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"

namespace views {
class Button;
class ScrollView;
}  // namespace views

namespace maho {

enum class MediaActionKind {
  kOpen,
  kReveal,
};

class MahoSidebarMediaView : public views::View {
  METADATA_HEADER(MahoSidebarMediaView, views::View)

 public:
  MahoSidebarMediaView();
  MahoSidebarMediaView(const MahoSidebarMediaView&) = delete;
  MahoSidebarMediaView& operator=(const MahoSidebarMediaView&) = delete;
  ~MahoSidebarMediaView() override;

  void ReloadMedia();

  views::ScrollView* scroll_view_for_testing() { return scroll_view_; }
  views::View* list_container_for_testing() { return list_container_; }
  views::View* empty_state_view_for_testing() {
    return empty_state_view_.get();
  }
  views::View* media_row_for_item_id_for_testing(const std::string& item_id);
  views::Button* media_action_button_for_item_id_for_testing(
      const std::string& item_id,
      size_t action_index);

 private:
  void BuildUi();
  void HandleMediaAction(const std::string& item_id, MediaActionKind action);

  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<views::View> list_container_ = nullptr;
  raw_ptr<views::Label> empty_state_view_ = nullptr;
  base::WeakPtrFactory<MahoSidebarMediaView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_MEDIA_VIEW_H_
