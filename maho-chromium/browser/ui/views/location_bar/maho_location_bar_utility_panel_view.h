// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_PANEL_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_PANEL_VIEW_H_

#include <map>

#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"

namespace views {
class View;
class Widget;
}  // namespace views

namespace maho {

class MahoLocationBarUtilityPanelView : public views::BubbleDialogDelegate {
 public:
  explicit MahoLocationBarUtilityPanelView(views::View* anchor_view,
                                           MahoLocationBarUtilityPanelModel model);
  MahoLocationBarUtilityPanelView(const MahoLocationBarUtilityPanelView&) =
      delete;
  MahoLocationBarUtilityPanelView& operator=(
      const MahoLocationBarUtilityPanelView&) = delete;
  ~MahoLocationBarUtilityPanelView() override;

  static views::Widget* Show(views::View* anchor_view,
                             MahoLocationBarUtilityPanelModel model);

  const MahoLocationBarUtilityPanelModel& model() const { return model_; }
  views::View* GetActionViewForTesting(const std::u16string& title) const;

 private:
  void BuildContents();
  void AddZone(const MahoUtilityPanelZone& zone);
  void AddZoneHeader(const std::u16string& header_label);
  void AddToolbarRowZone(const MahoUtilityPanelZone& zone);
  void AddChipRowZone(const MahoUtilityPanelZone& zone);
  void AddSettingsListZone(const MahoUtilityPanelZone& zone);
  void AddFooterSummary();
  void RegisterActionViewForTesting(
      const MahoLocationBarUtilityPanelAction& action,
      views::View* view);

  MahoLocationBarUtilityPanelModel model_;
  std::map<std::u16string, raw_ptr<views::View>> action_views_for_testing_;
  raw_ptr<views::View> contents_view_ = nullptr;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_PANEL_VIEW_H_
