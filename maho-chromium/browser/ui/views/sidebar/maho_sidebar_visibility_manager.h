// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_VISIBILITY_MANAGER_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_VISIBILITY_MANAGER_H_

#include <algorithm>
#include <vector>

#include "base/functional/function_ref.h"
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

  // Hard cap on the leading (scroll-direction) overscan the velocity-aware path
  // may request. Bounds total realized rows to O(viewport + this) so a fast
  // fling never makes realization scale with the tab count. 50 rows (~1800dp)
  // comfortably covers a frame of fling travel on top of synchronous layout.
  static constexpr int kMaxVelocityOverscanRows = 50;

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

  // Variable-height counterpart to ComputeVisibleRange() for the section-local
  // projection. |cumulative_top_at| returns the top offset (dp) for row index
  // within the section; |total_height| is the section's full content height.
  // Overscan buffering matches the uniform path — (drag_active_ ?
  // drag_overscan_rows : buffer_rows_below) * row_height_dp on each edge — and
  // the forced-visible index is always included.
  VisibleRange ComputeVisibleRange(
      const gfx::Rect& section_local_visible_rect,
      int row_count,
      int total_height,
      base::FunctionRef<int(int)> cumulative_top_at) const;

  template <typename CumulativeTopAt>
  VisibleRange ComputeVisibleRange(
      const gfx::Rect& section_local_visible_rect,
      int row_count,
      int total_height,
      const CumulativeTopAt& cumulative_top_at) const {
    return ComputeVisibleRange(
        section_local_visible_rect, row_count, total_height,
        base::FunctionRef<int(int)>(cumulative_top_at));
  }

  // Vector-backed compatibility wrapper for existing callers/tests.
  VisibleRange ComputeVisibleRangeForProjection(
      const gfx::Rect& section_local_visible_rect,
      const std::vector<int>& cumulative_tops,
      int total_height) const;

  void SetDragActive(bool active) { drag_active_ = active; }
  bool is_drag_active() const { return drag_active_; }

  // Per-edge overscan (rows) for the projection path. Callers set the leading
  // (scroll-direction) edge larger during fast flings, capped at
  // kMaxVelocityOverscanRows; the trailing edge keeps the base buffer. Both
  // default to config().buffer_rows_below. Ignored while a drag is active
  // (drag_overscan_rows takes over on both edges).
  void SetProjectionOverscanRows(int above_rows, int below_rows) {
    overscan_above_rows_ = std::max(0, above_rows);
    overscan_below_rows_ = std::max(0, below_rows);
  }
  int overscan_above_rows_for_testing() const { return overscan_above_rows_; }
  int overscan_below_rows_for_testing() const { return overscan_below_rows_; }

  // Force a specific row visible regardless of scroll position (for focus).
  void SetForcedVisibleIndex(int index) { forced_visible_index_ = index; }
  void ClearForcedVisibleIndex() { forced_visible_index_ = -1; }

  const Config& config() const { return config_; }

 private:
  Config config_;
  bool drag_active_ = false;
  int forced_visible_index_ = -1;
  int overscan_above_rows_;
  int overscan_below_rows_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_VISIBILITY_MANAGER_H_
