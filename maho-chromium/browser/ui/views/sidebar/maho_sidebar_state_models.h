// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_STATE_MODELS_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_STATE_MODELS_H_

#include <cstddef>
#include <string>
#include <vector>

#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "url/gurl.h"

namespace maho {

enum class MahoSidebarAddressDisplayMode {
  kSearchPlaceholder,
  kAddressCard,
};

struct MahoSidebarTopBarModel {
  bool navigation_enabled = false;
  bool can_go_back = false;
  bool can_go_forward = false;
  bool can_reload = false;
  bool is_loading = false;
};

struct MahoSidebarSearchModel {
  MahoSidebarSearchModel();
  MahoSidebarSearchModel(const MahoSidebarSearchModel&);
  MahoSidebarSearchModel& operator=(const MahoSidebarSearchModel&);
  ~MahoSidebarSearchModel();

  std::u16string idle_text;
  std::string current_url;
  bool has_active_tab = false;
  bool force_placeholder_for_empty_favorites = false;
  MahoSidebarAddressDisplayMode display_mode =
      MahoSidebarAddressDisplayMode::kSearchPlaceholder;
};

struct MahoSidebarFavoriteItemModel {
  MahoSidebarFavoriteItemModel();
  MahoSidebarFavoriteItemModel(const MahoSidebarFavoriteItemModel&);
  MahoSidebarFavoriteItemModel& operator=(const MahoSidebarFavoriteItemModel&);
  ~MahoSidebarFavoriteItemModel();

  std::u16string title;
  std::u16string subtitle;
  GURL url;
  // Stable tab ID from maho-core, empty if no matching tab.
  std::string tab_id;
  bool is_favorite = true;
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
  bool is_favorite = false;
  std::u16string custom_title;
  std::u16string title;
  std::u16string host;

  // --- Tab indicator fields (valid when kind == kTab) ---
  bool is_loading = false;
  bool is_audible = false;
  bool is_muted = false;
  std::string parent_folder_id;
  int folder_child_index = -1;

  // --- Tab split indicator (valid when kind == kTab) ---
  bool is_in_split = false;

  // --- Folder fields (valid when kind == kFolder) ---
  std::string folder_id;  // Stable FolderId from maho-core.
  std::u16string folder_name;
  bool is_expanded = true;
  bool folder_is_pinned = false;
  std::string parent_of_folder_id;

  // --- Children (folders and split groups) ---
  std::vector<SidebarTreeNode> children;

  // --- Split group fields (valid when kind == kSplitGroup) ---
  std::string split_id;
  std::string split_orientation;
  double split_ratio = 0.5;

  // Depth in tree for indentation (0 = top-level).
  int depth = 0;
};

// Legacy flat row kept for compatibility during transition, wraps tree node.
struct MahoSidebarTabRowModel {
  MahoSidebarTabRowModel();
  MahoSidebarTabRowModel(const MahoSidebarTabRowModel&);
  MahoSidebarTabRowModel(MahoSidebarTabRowModel&&);
  MahoSidebarTabRowModel& operator=(const MahoSidebarTabRowModel&);
  MahoSidebarTabRowModel& operator=(MahoSidebarTabRowModel&&);
  ~MahoSidebarTabRowModel();

  int index;
  bool is_active;
  bool is_pinned;
  MahoSidebarTabSection section;
  std::u16string title;
  std::u16string host;
  std::string tab_id;  // Stable ID from core.
  bool is_loading = false;
  bool is_audible = false;
  bool is_muted = false;
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

  // Flat lists kept for backward compat (populated alongside trees).
  std::vector<MahoSidebarTabRowModel> pinned_tabs;
  std::vector<MahoSidebarTabRowModel> tabs;

  // Active space ID (needed for ShellEvent dispatch).
  std::string active_space_id;
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
  std::vector<std::string> space_icons;    // emoji strings per space
  std::vector<std::string> space_names;    // display names per space
  std::vector<std::string> space_colors;   // hex colors per space
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
  MahoSidebarSearchModel search;
  MahoSidebarFavoritesModel favorites;
  MahoSidebarTabListModel tab_list;
  MahoSidebarFooterModel footer;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_STATE_MODELS_H_
