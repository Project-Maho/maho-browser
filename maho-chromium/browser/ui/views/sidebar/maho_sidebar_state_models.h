// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_STATE_MODELS_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_STATE_MODELS_H_

#include <cstddef>
#include <string>
#include <vector>

#include "base/values.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_spaces_board_data.h"
#include "url/gurl.h"

namespace maho {

// ---------------------------------------------------------------------------
// MahoDisplayPolicy — URL-aware display normalization used consistently by
// sidebar tab rows, favorites, subtitles/tooltips, and search pill text.
//
// Two concerns are deliberately separated:
//   Data layer  — ghost/url-less tabs must never enter core state from the
//                 Chromium path (enforced in maho_sidebar_state_adapter.cc and
//                 maho_core.rs).
//   Display layer — legitimate live chrome://newtab tabs are representable but
//                   must never surface raw internal placeholder text in the UI.
// ---------------------------------------------------------------------------
namespace MahoDisplayPolicy {

inline bool IsNtpOrBlankUrl(const GURL& url) {
  if (!url.is_valid() || url.is_empty()) {
    return true;
  }
  if (url.SchemeIs("about")) {
    return true;
  }
  if (url.SchemeIs("chrome") && url.host() == "newtab") {
    return true;
  }
  return false;
}

// Returns true for URLs that represent internal browser state rather than
// navigable web content: NTP, about: pages, chrome:// surfaces, devtools://,
// chrome-extension://, chrome-untrusted://, and chrome-native:// pages.
//
// Used by MahoAiSecurityUtils to keep internal browser pages out of AI page
// context. The command palette's current-tab prefill does NOT use this
// helper: BrowserView::ShowMahoCommandOverlayForCurrentTab() (patched via
// apply_chromium_src_overrides.py) only drops javascript:, data: and
// about:blank, so chrome:// pages are still prefilled there.
inline bool IsInternalBrowserPage(const GURL& url) {
  if (!url.is_valid() || url.is_empty()) {
    return true;
  }
  if (url.SchemeIs("about")) {
    return true;
  }
  if (url.SchemeIs("chrome")) {
    return true;
  }
  if (url.SchemeIs("devtools")) {
    return true;
  }
  if (url.SchemeIs("chrome-extension")) {
    return true;
  }
  if (url.SchemeIs("chrome-untrusted")) {
    return true;
  }
  if (url.SchemeIs("chrome-native")) {
    return true;
  }
  return false;
}

// Returns true for browser-internal "system surface" URLs that historically
// did not appear in the sidebar tab list (NTP, downloads, history, about:*).
// Used by the tab-strip fallback path; the core-driven path no longer
// filters here because tab URLs are now plumbed through tab_url_updated.
inline bool IsSystemSurfaceUrl(const GURL& url) {
  if (!url.is_valid() || url.is_empty()) return true;
  if (url.SchemeIs("about")) return true;
  if (url.SchemeIs("chrome")) {
    return url.host() == "newtab" ||
           url.host() == "downloads" ||
           url.host() == "history";
  }
  return false;
}

inline bool IsGhostTab(const std::string& url, const std::u16string& title) {
  return url.empty() && title.empty();
}

inline bool IsSentinelText(const std::u16string& text) {
  return text == u"newtab" || text == u"New Tab" || text == u"about:blank" ||
         text == u"chrome://newtab/";
}

// Returns a clean display title for a tab row or favorite tile.
//
// Priority order:
//   1. |custom_title| if non-empty AND (URL is not NTP/blank OR custom_title
//      is not a raw sentinel).  A real user custom title on a real page is
//      always preserved even if it happens to read "newtab".
//   2. |title| if non-empty and not a raw NTP/blank sentinel string.
//   3. "New Tab" when the URL is an NTP/blank origin — live NTP tabs must still
//      render a clean user-visible label.
//   4. Empty string — caller falls back to host or its own placeholder.
inline std::u16string ResolveTabDisplayText(
    const GURL& url,
    const std::u16string& title,
    const std::u16string& custom_title = std::u16string()) {
  const bool is_ntp = IsNtpOrBlankUrl(url);
  if (!custom_title.empty()) {
    if (!is_ntp || !IsSentinelText(custom_title)) {
      return custom_title;
    }
  }
  if (!title.empty() && !IsSentinelText(title)) {
    return title;
  }
  if (is_ntp) {
    return u"New Tab";
  }
  return std::u16string();
}

}  // namespace MahoDisplayPolicy

struct MahoSidebarTopBarModel {};

struct MahoSidebarFavoriteItemModel {
  MahoSidebarFavoriteItemModel();
  MahoSidebarFavoriteItemModel(const MahoSidebarFavoriteItemModel&);
  MahoSidebarFavoriteItemModel& operator=(const MahoSidebarFavoriteItemModel&);
  ~MahoSidebarFavoriteItemModel();

  std::u16string title;
  // Raw authored custom title (maho-core customTitle). Empty when the user
  // has never renamed the favorite. Distinct from |title|, which is a
  // resolved display projection (page title/host fallback) and must never be
  // written back as authored state.
  std::u16string custom_title;
  std::u16string subtitle;
  GURL url;
  // Stable tab ID from maho-core, empty if no matching tab.
  std::string tab_id;
  bool is_favorite = true;  // L3-EXEMPT: derived from role in state_adapter.cc
  bool is_active = false;
  std::vector<uint8_t> favicon_png_data;
  // User-chosen tile glyph (emoji) from the favorite edit dialog. Empty when
  // the tile should render its favicon instead.
  std::u16string custom_icon;
  // User-pinned destination URL for this favorite. Empty when the tile follows
  // the live tab URL.
  std::string pinned_url;
};

struct MahoSidebarFavoritesModel {
  // Contract: favorites render using a count-based layout, capped to the
  // native runtime slice maximum.
  std::size_t slot_count = sidebar_layout::kMahoSidebarFavoriteSlotCount;
  MahoSidebarFavoritesModel();
  MahoSidebarFavoritesModel(const MahoSidebarFavoritesModel&);
  MahoSidebarFavoritesModel(MahoSidebarFavoritesModel&&);
  MahoSidebarFavoritesModel& operator=(const MahoSidebarFavoritesModel&);
  MahoSidebarFavoritesModel& operator=(MahoSidebarFavoritesModel&&);
  ~MahoSidebarFavoritesModel();

  std::vector<MahoSidebarFavoriteItemModel> items;
};

enum class MahoSidebarTabSection {
  kPinned,
  kNormal,
};

// --- Tree-based tab/folder model ---

enum class SidebarNodeKind {
  kTab,
  kFolder,
  kSplitGroup,
};

// A node in the sidebar tree: either a tab or a folder.
// Folders can contain child nodes recursively.
struct SidebarTreeNode {
  SidebarTreeNode();
  SidebarTreeNode(const SidebarTreeNode&);
  SidebarTreeNode(SidebarTreeNode&&);
  SidebarTreeNode& operator=(const SidebarTreeNode&);
  SidebarTreeNode& operator=(SidebarTreeNode&&);
  ~SidebarTreeNode();

  SidebarNodeKind kind = SidebarNodeKind::kTab;

  // --- Tab fields (valid when kind == kTab) ---
  std::string tab_id;       // Stable TabId from maho-core (primary identity).
  int tab_strip_index = -1; // Current Chromium tab-strip index for browser
                            // actions (activate/close). May be -1 if tab
                            // exists only in core.
  bool is_active = false;
  bool is_pinned = false;
  bool is_favorite = false;  // L3-EXEMPT: derived from role in state_adapter.cc
  std::string url;          // Raw URL string; used by display policy helpers.
  std::u16string custom_title;
  std::u16string title;
  std::u16string host;

  // PNG bytes from maho-core Tab.favicon (populated during import + page
  // load). Empty if not yet fetched. tab_list_view uses this when the
  // live Chromium TabStripModel has no entry for this tab (common after
  // import — no live WebContents exists for imported tab URLs).
  std::vector<uint8_t> favicon_png_data;

  // --- Tab indicator fields (valid when kind == kTab) ---
  bool is_loading = false;
  bool is_audible = false;
  bool is_muted = false;
  bool is_suspended = false;
  std::string parent_folder_id;
  int folder_child_index = -1;

  // --- Tab split indicator (valid when kind == kTab) ---
  bool is_in_split = false;

  // --- Folder fields (valid when kind == kFolder) ---
  std::string folder_id;  // Stable FolderId from maho-core.
  std::u16string folder_name;
  bool is_expanded = false;
  bool folder_is_pinned = false;
  std::string parent_of_folder_id;
  bool is_live = false;
  std::string provider_type;
  std::string config_json;

  // --- Children (folders and split groups) ---
  std::vector<SidebarTreeNode> children;

  // --- Split group fields (valid when kind == kSplitGroup) ---
  std::string split_id;
  std::string split_orientation;
  double split_ratio = 0.5;

  // Depth in tree for indentation (0 = top-level).
  int depth = 0;
};

struct MahoSidebarActiveTabModel {
  MahoSidebarActiveTabModel();
  MahoSidebarActiveTabModel(const MahoSidebarActiveTabModel&);
  MahoSidebarActiveTabModel& operator=(const MahoSidebarActiveTabModel&);
  ~MahoSidebarActiveTabModel();

  int index = -1;
  std::u16string title;
  std::u16string host;
  std::string tab_id;
};

struct MahoSidebarTabListModel {
  MahoSidebarTabListModel();
  MahoSidebarTabListModel(const MahoSidebarTabListModel&);
  MahoSidebarTabListModel(MahoSidebarTabListModel&&);
  MahoSidebarTabListModel& operator=(const MahoSidebarTabListModel&);
  MahoSidebarTabListModel& operator=(MahoSidebarTabListModel&&);
  ~MahoSidebarTabListModel();

  MahoSidebarActiveTabModel active_tab;

  // Tree-based sections: top-level nodes for pinned and normal.
  std::vector<SidebarTreeNode> pinned_tree;
  std::vector<SidebarTreeNode> normal_tree;

  // Active space ID (needed for ShellEvent dispatch).
  std::string active_space_id;

  // Active space's icon (raw emoji string, e.g. "💼"). Empty if no icon set.
  std::string active_space_icon;

  // Active space's display name. Empty if no active space resolved.
  std::u16string active_space_name;

  // True when the core could not produce this space's tree at all (null v2
  // payload: core swapped, import gate raised, ...). Distinct from an empty
  // tree, which is real data and must render; views keep what they show.
  bool tree_unavailable = false;
};

// --- Drag/Drop payload ---

enum class SidebarDragOrigin {
  kPinnedSection,
  kNormalSection,
  kFavorites,
};

struct SidebarDragPayload {
  SidebarDragPayload();
  SidebarDragPayload(const SidebarDragPayload&);
  SidebarDragPayload& operator=(const SidebarDragPayload&);
  ~SidebarDragPayload();

  SidebarNodeKind node_kind = SidebarNodeKind::kTab;
  std::string node_id;  // tab_id or folder_id
  SidebarDragOrigin origin = SidebarDragOrigin::kNormalSection;
  std::string space_id;
  int tab_strip_index = -1;
  std::string source_parent_folder_id;
  int source_folder_child_index = -1;
  std::vector<std::string> selected_tab_ids;
  // When the dragged tab belongs to a split group, this holds every member
  // tab_id of that split. Only the space-dot drop target reads it (to move the
  // whole split to another Space); all other drop targets ignore it.
  std::vector<std::string> split_member_tab_ids;
};

struct MahoSidebarFooterUpdatePillModel {
  MahoSidebarFooterUpdatePillModel();
  MahoSidebarFooterUpdatePillModel(const MahoSidebarFooterUpdatePillModel&);
  MahoSidebarFooterUpdatePillModel& operator=(const MahoSidebarFooterUpdatePillModel&);
  ~MahoSidebarFooterUpdatePillModel();

  bool visible = false;
  bool has_update = false;
  std::u16string label;
  std::u16string accessible_label;
};

struct MahoSidebarFooterModel {
  MahoSidebarFooterModel();
  MahoSidebarFooterModel(const MahoSidebarFooterModel&);
  MahoSidebarFooterModel(MahoSidebarFooterModel&&);
  MahoSidebarFooterModel& operator=(const MahoSidebarFooterModel&);
  MahoSidebarFooterModel& operator=(MahoSidebarFooterModel&&);
  ~MahoSidebarFooterModel();

  std::u16string status_text;
  int space_count;
  int active_space_index;
  std::string space_color;
  MahoSpaceDisplayThemeModel space_theme;
  std::vector<std::string> space_icons;    // emoji strings per space
  std::vector<std::string> space_names;    // display names per space
  std::vector<std::string> space_colors;   // hex colors per space
  std::vector<MahoSpaceDisplayThemeModel> space_themes;
  std::vector<std::string> space_ids;      // space IDs in display order
  bool can_open_space_controls;
  bool can_create_space;
  MahoSidebarFooterUpdatePillModel update_pill;
};

struct MahoSidebarViewStateModel {
  MahoSidebarViewStateModel();
  MahoSidebarViewStateModel(const MahoSidebarViewStateModel&);
  MahoSidebarViewStateModel(MahoSidebarViewStateModel&&);
  MahoSidebarViewStateModel& operator=(const MahoSidebarViewStateModel&);
  MahoSidebarViewStateModel& operator=(MahoSidebarViewStateModel&&);
  ~MahoSidebarViewStateModel();

  MahoSidebarTopBarModel top_bar;
  MahoSidebarFavoritesModel favorites;
  MahoSidebarTabListModel tab_list;
  MahoSidebarFooterModel footer;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_STATE_MODELS_H_
