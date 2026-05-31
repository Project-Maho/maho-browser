// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_PANEL_PROVIDER_H_
#define MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_PANEL_PROVIDER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "ui/gfx/vector_icon_types.h"

class Browser;
class LocationBar;
class LocationBarModel;
class Profile;

namespace views {
class View;
}  // namespace views

namespace content {
class WebContents;
}  // namespace content

namespace zoom {
class ZoomController;
}  // namespace zoom

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
  raw_ptr<const gfx::VectorIcon> icon = nullptr;
  bool enabled = true;
  MahoLocationBarUtilityPanelDataState data_state =
      MahoLocationBarUtilityPanelDataState::kLive;
  base::RepeatingClosure callback;
  bool has_toggle = false;
  bool toggle_is_on = false;
  std::u16string toggle_on_subtitle;
  std::u16string toggle_off_subtitle;
  base::RepeatingCallback<void(bool)> toggle_callback;
  bool close_after_activate = false;

  // Extension Icon Grid Phase 3 updates
  std::string extension_id;
  base::RepeatingCallback<void(views::View* anchor)> context_menu_callback;
  bool context_menu_enabled = true;
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
  raw_ptr<const gfx::VectorIcon> icon = nullptr;
  bool primary_action_enabled = true;
  bool close_after_primary_action = false;
  MahoLocationBarUtilityPanelDataState data_state =
      MahoLocationBarUtilityPanelDataState::kLive;
  int security_level = 0;
  base::RepeatingClosure primary_action;
};

enum class MahoUtilityPanelZoneId {
  kToolbar,             // Arc-style toolbar row
  kExtensions,          // Arc-style extensions chip row
  kSettings,            // Arc-style settings rows
};

enum class MahoUtilityPanelZoneStyle {
  kToolbarRow,
  kChipRow,
  kSettingsList,
};

struct MahoUtilityPanelZone {
  MahoUtilityPanelZone();
  MahoUtilityPanelZone(const MahoUtilityPanelZone&);
  MahoUtilityPanelZone& operator=(const MahoUtilityPanelZone&);
  ~MahoUtilityPanelZone();

  MahoUtilityPanelZoneId id = MahoUtilityPanelZoneId::kToolbar;
  MahoUtilityPanelZoneStyle style = MahoUtilityPanelZoneStyle::kToolbarRow;
  std::u16string header_label;
  std::vector<MahoLocationBarUtilityPanelAction> items;
  std::optional<MahoLocationBarUtilityPanelAction> header_trailing_action;
};

struct MahoLocationBarUtilityPanelModel {
  MahoLocationBarUtilityPanelModel();
  MahoLocationBarUtilityPanelModel(const MahoLocationBarUtilityPanelModel&);
  MahoLocationBarUtilityPanelModel& operator=(
      const MahoLocationBarUtilityPanelModel&);
  ~MahoLocationBarUtilityPanelModel();

  std::vector<MahoUtilityPanelZone> zones;
  MahoLocationBarUtilityPanelFooter footer;
  raw_ptr<Browser> owning_browser = nullptr;
};

namespace internal {

bool IsAtMaxZoom(zoom::ZoomController* zoom_controller);
bool IsAtMinZoom(zoom::ZoomController* zoom_controller);

MahoLocationBarUtilityPanelAction BuildDeveloperModeAction(
    const base::Value* parsed_settings);

MahoLocationBarUtilityPanelAction BuildShareAction(Browser* browser);

MahoLocationBarUtilityPanelAction BuildCameraAction(Browser* browser);

MahoLocationBarUtilityPanelAction BuildViewfinderAction(Browser* browser);

MahoLocationBarUtilityPanelAction BuildShieldsAction(Browser* browser);

MahoLocationBarUtilityPanelAction BuildBlockAdsAndTrackersRow(Browser* browser);

MahoLocationBarUtilityPanelAction BuildBlockAdsAndTrackersRowForState(
    bool blocking_enabled);

void SetBlockAdsAndTrackersEnabledForOrigin(const std::string& origin,
                                            bool blocking_enabled);

std::vector<MahoLocationBarUtilityPanelAction> BuildExtensionIconActions(
    Browser* browser);

MahoLocationBarUtilityPanelAction BuildExtensionsManageAction(Browser* browser);

MahoUtilityPanelZone BuildToolbarZone(Browser* browser);

MahoUtilityPanelZone BuildExtensionsZone(Browser* browser);

MahoUtilityPanelZone BuildSettingsZone(Browser* browser);

MahoLocationBarUtilityPanelAction BuildAutoPipSettingsRow(
    content::WebContents* web_contents,
    Profile* profile);

std::string GetSecuritySFSymbolNameForLevel(int security_level);
const gfx::VectorIcon& GetSecurityIconForLevel(int security_level);

MahoLocationBarUtilityPanelFooter BuildFooterFromWebContents(
    content::WebContents* web_contents);

MahoLocationBarUtilityPanelFooter BuildFooterFromModel(
    LocationBarModel* model);

MahoLocationBarUtilityPanelFooter BuildFooter(LocationBar* location_bar);

MahoLocationBarUtilityPanelFooter BuildFooter(Browser* browser);

bool IsOriginExceptedInSiteExceptions(const std::string& exceptions_json,
                                      const std::string& origin);

MahoLocationBarUtilityPanelAction BuildZoomInActionForState(
    bool has_active_page,
    bool at_max_zoom);

MahoLocationBarUtilityPanelAction BuildZoomOutActionForState(
    bool has_active_page,
    bool at_min_zoom);

MahoLocationBarUtilityPanelAction BuildZoomResetActionForState(
    bool has_active_page,
    bool at_default_zoom,
    std::optional<int> zoom_percent);

}  // namespace internal

MahoLocationBarUtilityPanelModel BuildMahoLocationBarUtilityPanelModel(
    LocationBar* location_bar);
MahoLocationBarUtilityPanelModel BuildMahoLocationBarUtilityPanelModelForBrowser(
    Browser* browser);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_PANEL_PROVIDER_H_
