// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_PANEL_PROVIDER_H_
#define MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_PANEL_PROVIDER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/gfx/vector_icon_types.h"

class Browser;
class LocationBar;
class Profile;
class GURL;

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
  base::RepeatingCallback<void(views::View*)> anchor_callback;
  bool show_disclosure_indicator = false;
  bool has_toggle = false;
  bool toggle_is_on = false;
  std::u16string toggle_on_subtitle;
  std::u16string toggle_off_subtitle;
  base::RepeatingCallback<void(bool, bool&, std::u16string&)> toggle_callback;
  bool close_after_activate = false;

  std::string extension_id;
  base::RepeatingCallback<void(views::View* anchor)> context_menu_callback;
  bool context_menu_enabled = true;
  bool is_pinned = false;
  bool pin_enabled = false;
  base::RepeatingCallback<void(bool)> pin_callback;
};

struct MahoLocationBarUtilityPanelFooter {
  MahoLocationBarUtilityPanelFooter();
  MahoLocationBarUtilityPanelFooter(const MahoLocationBarUtilityPanelFooter&);
  MahoLocationBarUtilityPanelFooter& operator=(
      const MahoLocationBarUtilityPanelFooter&);
  ~MahoLocationBarUtilityPanelFooter();

  std::u16string title;
  std::u16string subtitle;
  raw_ptr<const gfx::VectorIcon> icon = nullptr;
  MahoLocationBarUtilityPanelDataState data_state =
      MahoLocationBarUtilityPanelDataState::kLive;
  int security_level = 0;
};

enum class MahoUtilityPanelZoneId {
  kToolbar,     // Arc-style toolbar row
  kExtensions,  // Arc-style extensions chip row
  kSettings,    // Arc-style settings rows
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

MahoLocationBarUtilityPanelAction BuildShareAction(Browser* browser);

MahoLocationBarUtilityPanelAction BuildCameraAction(Browser* browser);

MahoLocationBarUtilityPanelAction BuildViewfinderAction(Browser* browser);

MahoLocationBarUtilityPanelAction BuildBlockAdsAndTrackersRow(Browser* browser);

MahoLocationBarUtilityPanelAction BuildBlockAdsAndTrackersRowForOrigin(
    Browser* browser,
    base::WeakPtr<content::WebContents> target_web_contents,
    const std::string& origin);

void SetAutoPipEnabledForOrigin(const GURL& url,
                                Profile* profile,
                                bool enabled,
                                bool& out_toggle_is_on,
                                std::u16string& out_subtitle);
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

MahoLocationBarUtilityPanelFooter BuildFooter(LocationBar* location_bar);

MahoLocationBarUtilityPanelFooter BuildFooter(Browser* browser);

}  // namespace internal

MahoLocationBarUtilityPanelModel BuildMahoLocationBarUtilityPanelModel(
    LocationBar* location_bar);
MahoLocationBarUtilityPanelModel
BuildMahoLocationBarUtilityPanelModelForBrowser(Browser* browser);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_PANEL_PROVIDER_H_
