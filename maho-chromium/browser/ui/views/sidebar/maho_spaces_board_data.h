// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SPACES_BOARD_DATA_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SPACES_BOARD_DATA_H_

#include <string>
#include <vector>

#include "base/values.h"

namespace maho {

enum class MahoSpaceDisplayThemeKind {
  kLegacyColor,
  kSolid,
  kGradient,
  kZen,
};

struct MahoSpaceDisplayThemeModel {
  MahoSpaceDisplayThemeModel();
  MahoSpaceDisplayThemeModel(const MahoSpaceDisplayThemeModel&);
  MahoSpaceDisplayThemeModel(MahoSpaceDisplayThemeModel&&);
  MahoSpaceDisplayThemeModel& operator=(const MahoSpaceDisplayThemeModel&);
  MahoSpaceDisplayThemeModel& operator=(MahoSpaceDisplayThemeModel&&);
  ~MahoSpaceDisplayThemeModel();

  bool has_theme = false;
  MahoSpaceDisplayThemeKind kind = MahoSpaceDisplayThemeKind::kLegacyColor;
  std::string theme_json;
  std::string primary_color;
  std::string secondary_color;
  std::vector<std::string> palette_colors;
  float opacity = 1.0f;
  float texture = 0.0f;
  float grain = 0.0f;
};

MahoSpaceDisplayThemeModel BuildMahoSpaceDisplayThemeModel(
    const base::DictValue& space_dict);
std::string GetMahoSpaceDisplayPrimaryColor(
    const MahoSpaceDisplayThemeModel& theme,
    const std::string& fallback_color);
std::string GetMahoSpaceDisplaySecondaryColor(
    const MahoSpaceDisplayThemeModel& theme,
    const std::string& fallback_color);

// A single tab item in a space board column. Parsed from TabViewModel JSON
// returned by maho_core_get_space_tabs().
struct SpaceBoardTabItem {
  SpaceBoardTabItem();
  SpaceBoardTabItem(const SpaceBoardTabItem&);
  SpaceBoardTabItem(SpaceBoardTabItem&&);
  SpaceBoardTabItem& operator=(const SpaceBoardTabItem&);
  SpaceBoardTabItem& operator=(SpaceBoardTabItem&&);
  ~SpaceBoardTabItem();

  // Stable TabId from maho-core.
  std::string id;
  // Display title; custom_title preferred over title when non-empty.
  std::u16string title;
  std::string url;
  bool is_pinned = false;
  bool is_loading = false;
  bool is_favorite = false;  // L3-EXEMPT: derived from role in spaces_board_data.cc
  bool is_muted = false;
  bool is_playing_audio = false;
};

// A board column representing one Space. Parsed from SpaceViewModel JSON
// returned by maho_core_get_space_view_models(), with tabs populated by a
// subsequent call to maho_core_get_space_tabs().
struct SpaceBoardColumn {
  SpaceBoardColumn();
  SpaceBoardColumn(const SpaceBoardColumn&);
  SpaceBoardColumn(SpaceBoardColumn&&);
  SpaceBoardColumn& operator=(const SpaceBoardColumn&);
  SpaceBoardColumn& operator=(SpaceBoardColumn&&);
  ~SpaceBoardColumn();

  // Stable SpaceId from maho-core.
  std::string space_id;
  std::u16string name;
  // Raw color string as serialized by Rust (e.g., hex "#rrggbb" or named).
  std::string color;
  MahoSpaceDisplayThemeModel theme;
  // Emoji/unicode icon for the space, may be empty.
  std::string icon;
  bool is_active = false;
  // tab_count from SpaceViewModel (may differ from tabs.size() if tabs
  // failed to load).
  int tab_count = 0;
  std::vector<SpaceBoardTabItem> tabs;
};

// The complete board model: one column per space, ordered as returned by
// maho_core_get_space_view_models().
struct SpaceBoardModel {
  SpaceBoardModel();
  SpaceBoardModel(const SpaceBoardModel&);
  SpaceBoardModel(SpaceBoardModel&&);
  SpaceBoardModel& operator=(const SpaceBoardModel&);
  SpaceBoardModel& operator=(SpaceBoardModel&&);
  ~SpaceBoardModel();

  std::vector<SpaceBoardColumn> columns;
  // Active space ID, parsed from maho_core_get_active_space_id().
  std::string active_space_id;
};

// Builds a complete SpaceBoardModel by calling maho_core_get_space_view_models(),
// maho_core_get_active_space_id(), and maho_core_get_space_tabs() for each
// space. Returns an empty model if maho-core is unavailable.
SpaceBoardModel BuildSpacesBoardModel();

// --- Compact display helpers (UI-agnostic) ---

// Returns a compact title for a tab: custom_title if set, otherwise the page
// title, otherwise the host extracted from the URL.
std::u16string SpaceBoardTabCompactTitle(const SpaceBoardTabItem& tab);

// Returns a subtitle string (the URL host, or full URL if no host).
std::u16string SpaceBoardTabSubtitle(const SpaceBoardTabItem& tab);

// Returns a one- or two-character badge initial for a space column derived
// from its name.
std::u16string SpaceBoardColumnBadge(const SpaceBoardColumn& column);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SPACES_BOARD_DATA_H_
