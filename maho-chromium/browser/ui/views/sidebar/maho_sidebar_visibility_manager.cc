// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_visibility_manager.h"

#include <algorithm>

namespace maho {

SidebarVisibilityManager::SidebarVisibilityManager(Config config)
    : config_(config) {}

SidebarVisibilityManager::~SidebarVisibilityManager() = default;

SidebarVisibilityManager::VisibleRange
SidebarVisibilityManager::ComputeVisibleRange(
    const gfx::Rect& visible_rect,
    int total_row_count) const {
  if (total_row_count == 0) {
    return {0, 0, 0};
  }

  const int row_h = std::max(config_.row_height_dp, 1);
  const int buffer_above =
      drag_active_ ? config_.drag_overscan_rows : config_.buffer_rows_above;
  const int buffer_below =
      drag_active_ ? config_.drag_overscan_rows : config_.buffer_rows_below;

  // Which rows intersect the viewport?
  int viewport_first = visible_rect.y() / row_h;
  int viewport_last = (visible_rect.bottom() + row_h - 1) / row_h;

  // Expand by buffer
  int first = std::max(0, viewport_first - buffer_above);
  int last = std::min(total_row_count - 1, viewport_last + buffer_below);

  // Include forced-visible index (for keyboard focus)
  if (forced_visible_index_ >= 0 &&
      forced_visible_index_ < total_row_count) {
    first = std::min(first, forced_visible_index_);
    last = std::max(last, forced_visible_index_);
  }

  return {first, last, total_row_count};
}

}  // namespace maho
