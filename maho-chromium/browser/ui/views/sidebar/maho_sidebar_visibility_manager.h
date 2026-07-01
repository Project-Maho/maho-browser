// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_VISIBILITY_MANAGER_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_VISIBILITY_MANAGER_H_

#include <algorithm>

#include "ui/gfx/geometry/rect.h"

namespace maho {

// Determines which rows should be laid out + painted based on scroll position.
// Used by SidebarVirtualLayoutDelegate to cull off-screen rows during layout.
class SidebarVisibilityManager {
 public:
  struct Config {
    int row_height_dp = 33;       // kRowHeightDp
    int buffer_rows_above = 30;   // Pre-render above viewport
    int buffer_rows_below = 30;   // Pre-render below viewport
    int drag_overscan_rows = 50;  // Extra buffer during DnD
  };

  explicit SidebarVisibilityManager(Config config);
  ~SidebarVisibilityManager();

  SidebarVisibilityManager(const SidebarVisibilityManager&) = delete;
  SidebarVisibilityManager& operator=(const SidebarVisibilityManager&) = delete;

  struct VisibleRange {
    int first_visible_index = 0;  // Inclusive
    int last_visible_index = 0;   // Inclusive
    int total_rows = 0;
  };

  // Main calculation: given the viewport rect relative to the content origin,
  // returns which row indices should be visible.
  VisibleRange ComputeVisibleRange(const gfx::Rect& visible_rect,
                                   int total_row_count) const;

  void SetDragActive(bool active) { drag_active_ = active; }
  bool is_drag_active() const { return drag_active_; }

  // Force a specific row visible regardless of scroll position (for focus).
  void SetForcedVisibleIndex(int index) { forced_visible_index_ = index; }
  void ClearForcedVisibleIndex() { forced_visible_index_ = -1; }

  const Config& config() const { return config_; }

 private:
  Config config_;
  bool drag_active_ = false;
  int forced_visible_index_ = -1;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_VISIBILITY_MANAGER_H_
