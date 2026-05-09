// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_PANEL_PROVIDER_H_
#define MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_PANEL_PROVIDER_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/values.h"

class Browser;
class LocationBar;

namespace maho {

enum class MahoLocationBarUtilityPanelDataState {
  kLive,
  kUnavailable,
  kUnsupported,
};

struct MahoLocationBarUtilityPanelAction {
  MahoLocationBarUtilityPanelAction();
  MahoLocationBarUtilityPanelAction(const MahoLocationBarUtilityPanelAction&);
  MahoLocationBarUtilityPanelAction& operator=(
      const MahoLocationBarUtilityPanelAction&);
  ~MahoLocationBarUtilityPanelAction();

  std::u16string title;
  std::u16string subtitle;
  std::u16string accessible_name;
  bool enabled = true;
  MahoLocationBarUtilityPanelDataState data_state =
      MahoLocationBarUtilityPanelDataState::kLive;
  base::RepeatingClosure callback;
};

struct MahoLocationBarUtilityPanelFooter {
  MahoLocationBarUtilityPanelFooter();
  MahoLocationBarUtilityPanelFooter(const MahoLocationBarUtilityPanelFooter&);
  MahoLocationBarUtilityPanelFooter& operator=(
      const MahoLocationBarUtilityPanelFooter&);
  ~MahoLocationBarUtilityPanelFooter();

  std::u16string title;
  std::u16string subtitle;
  std::u16string primary_action_label;
  bool primary_action_enabled = true;
  bool close_after_primary_action = false;
  MahoLocationBarUtilityPanelDataState data_state =
      MahoLocationBarUtilityPanelDataState::kLive;
  int security_level = 0;
  base::RepeatingClosure primary_action;
};

struct MahoLocationBarUtilityPanelModel {
  MahoLocationBarUtilityPanelModel();
  MahoLocationBarUtilityPanelModel(const MahoLocationBarUtilityPanelModel&);
  MahoLocationBarUtilityPanelModel& operator=(
      const MahoLocationBarUtilityPanelModel&);
  ~MahoLocationBarUtilityPanelModel();

  std::vector<MahoLocationBarUtilityPanelAction> quick_actions;
  std::vector<MahoLocationBarUtilityPanelAction> extensions;
  std::vector<MahoLocationBarUtilityPanelAction> settings;
  MahoLocationBarUtilityPanelFooter footer;
};

namespace internal {

MahoLocationBarUtilityPanelAction BuildDeveloperModeAction(
    const base::Value* parsed_settings);
std::vector<MahoLocationBarUtilityPanelAction> BuildInstalledExtensions(
    const std::string& json);
MahoLocationBarUtilityPanelAction BuildExtensionsSummaryAction(
    const std::vector<MahoLocationBarUtilityPanelAction>& extensions);

}  // namespace internal

MahoLocationBarUtilityPanelModel BuildMahoLocationBarUtilityPanelModel(
    LocationBar* location_bar);
MahoLocationBarUtilityPanelModel BuildMahoLocationBarUtilityPanelModelForBrowser(
    Browser* browser);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_PANEL_PROVIDER_H_
