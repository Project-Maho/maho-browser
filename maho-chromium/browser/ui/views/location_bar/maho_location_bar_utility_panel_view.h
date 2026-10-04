// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_PANEL_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_PANEL_VIEW_H_

#include <map>

#include "base/memory/weak_ptr.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h"
#include "maho/browser/extensions/maho_extension_state_bridge.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"

namespace extensions {
class ExtensionContextMenuModel;
}  // namespace extensions

namespace views {
class MenuRunner;
class View;
class Widget;
}  // namespace views

namespace maho {

class MahoLocationBarUtilityPanelView : public views::BubbleDialogDelegate,
                                        public MahoExtensionStateBridge::Observer {
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

  // MahoExtensionStateBridge::Observer:
  void OnInstalledExtensionsChanged() override;

  // Public context menu runner delegate:
  void RunExtensionContextMenu(
      std::unique_ptr<extensions::ExtensionContextMenuModel> menu_model,
      views::View* anchor);
  void OnMenuClosed(int generation);

 private:
  void BuildContents();
  void AddZone(const MahoUtilityPanelZone& zone);
  void BuildExtensionsZoneContents(const MahoUtilityPanelZone& zone);
  void AddZoneHeader(const std::u16string& header_label,
                     const std::optional<MahoLocationBarUtilityPanelAction>& trailing_action,
                     views::View* parent_view);
  void AddToolbarRowZone(const MahoUtilityPanelZone& zone, views::View* parent_view);
  void AddChipRowZone(const MahoUtilityPanelZone& zone, views::View* parent_view);
  void AddSettingsListZone(const MahoUtilityPanelZone& zone, views::View* parent_view);
  void AddFooterSummary();
  void RegisterActionViewForTesting(
      const MahoLocationBarUtilityPanelAction& action,
      views::View* view);
  void OnManageButtonClicked(MahoLocationBarUtilityPanelAction action);
  void OnSecuritySummaryClicked();

  MahoLocationBarUtilityPanelModel model_;
  std::map<std::u16string, raw_ptr<views::View>> action_views_for_testing_;
  raw_ptr<views::View> contents_view_ = nullptr;

  base::ScopedObservation<MahoExtensionStateBridge,
                          MahoExtensionStateBridge::Observer>
      bridge_observation_{this};
  raw_ptr<views::View> extensions_container_ = nullptr;
  std::unique_ptr<extensions::ExtensionContextMenuModel> current_menu_model_;
  std::unique_ptr<views::MenuRunner> context_menu_runner_;
  int menu_generation_ = 0;
  bool rebuilding_extensions_ = false;
  base::WeakPtrFactory<MahoLocationBarUtilityPanelView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_PANEL_VIEW_H_
