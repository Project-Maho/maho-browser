// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DND_EVENTS_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DND_EVENTS_H_

namespace maho::sidebar {

// Shared constants for sidebar drag-and-drop event strings.
// Use these instead of raw string literals in production code and tests.

inline constexpr char kReorderRootItem[]    = "reorder_root_item";
inline constexpr char kReorderTabInFolder[] = "reorder_tab_in_folder";
inline constexpr char kReorderFavorite[]    = "reorder_favorite";
inline constexpr char kReorderFolder[]      = "reorder_folder";
inline constexpr char kMoveTabToRoot[]      = "move_tab_to_root";
inline constexpr char kMoveTabToFolder[]    = "move_tab_to_folder";
inline constexpr char kMoveFolderIntoFolder[] = "move_folder_into_folder";
inline constexpr char kMoveFolderToRoot[]   = "move_folder_to_root";
inline constexpr char kSetFolderPinned[]    = "set_folder_pinned";

}  // namespace maho::sidebar

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DND_EVENTS_H_
