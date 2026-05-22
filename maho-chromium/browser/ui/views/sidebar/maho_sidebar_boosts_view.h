// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_BOOSTS_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_BOOSTS_VIEW_H_

#include <cstddef>
#include <string>

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class Button;
class Label;
class ScrollView;
class View;
}  // namespace views

namespace maho {

class MahoSidebarBoostsView : public views::View {
  METADATA_HEADER(MahoSidebarBoostsView, views::View)

 public:
  MahoSidebarBoostsView();
  MahoSidebarBoostsView(const MahoSidebarBoostsView&) = delete;
  MahoSidebarBoostsView& operator=(const MahoSidebarBoostsView&) = delete;
  ~MahoSidebarBoostsView() override;

  void ReloadBoosts();

  views::ScrollView* scroll_view_for_testing() { return scroll_view_; }
  views::View* list_container_for_testing() { return list_container_; }
  views::View* empty_state_view_for_testing() { return empty_state_view_; }
  views::View* boost_row_for_boost_id_for_testing(const std::string& boost_id);
  views::Button* button_for_action_for_testing(size_t index);

 private:
  void BuildUi();
  void ToggleBoost(const std::string& boost_id, bool new_enabled);

  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<views::View> list_container_ = nullptr;
  raw_ptr<views::View> body_container_ = nullptr;
  raw_ptr<views::View> empty_state_view_ = nullptr;
  raw_ptr<views::Label> header_summary_label_ = nullptr;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_BOOSTS_VIEW_H_
