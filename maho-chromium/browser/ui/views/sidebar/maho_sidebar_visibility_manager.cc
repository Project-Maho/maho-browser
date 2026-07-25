// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_visibility_manager.h"

#include <algorithm>

namespace maho {

SidebarVisibilityManager::SidebarVisibilityManager(Config config)
    : config_(config),
      overscan_above_rows_(config.buffer_rows_below),
      overscan_below_rows_(config.buffer_rows_below) {}

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

SidebarVisibilityManager::VisibleRange
SidebarVisibilityManager::ComputeVisibleRange(
    const gfx::Rect& section_local_visible_rect,
    int row_count,
    int total_height,
    base::FunctionRef<int(int)> cumulative_top_at) const {
  if (row_count == 0) {
    return {0, -1, 0};
  }

  const int row_h = std::max(config_.row_height_dp, 1);
  const int above_rows =
      drag_active_ ? config_.drag_overscan_rows : overscan_above_rows_;
  const int below_rows =
      drag_active_ ? config_.drag_overscan_rows : overscan_below_rows_;
  const int top_buffer_px = above_rows * row_h;
  const int bottom_buffer_px = below_rows * row_h;

  const int top_threshold = section_local_visible_rect.y() - top_buffer_px;
  const int bottom_threshold =
      section_local_visible_rect.bottom() + bottom_buffer_px;

  // Row i spans [cumulative_top_at(i), row_bottom(i)); the last row runs to
  // the section's total content height.
  auto row_bottom = [&](int i) {
    return (i + 1 < row_count) ? cumulative_top_at(i + 1) : total_height;
  };

  // First row whose bottom edge extends past the (buffered) viewport top.
  int first = row_count;
  {
    int lo = 0;
    int hi = row_count;
    while (lo < hi) {
      const int mid = lo + (hi - lo) / 2;
      if (row_bottom(mid) > top_threshold) {
        hi = mid;
      } else {
        lo = mid + 1;
      }
    }
    first = lo;
  }

  // Last row whose top edge is above the (buffered) viewport bottom.
  int last = -1;
  {
    int lo = 0;
    int hi = row_count;
    while (lo < hi) {
      const int mid = lo + (hi - lo) / 2;
      if (cumulative_top_at(mid) < bottom_threshold) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    last = lo - 1;
  }

  // Clamp the geometric window into range before applying the forced index.
  if (first >= row_count) {
    first = row_count - 1;
  }
  if (last < 0) {
    last = 0;
  }
  if (first > last) {
    first = last;
  }

  if (forced_visible_index_ >= 0 && forced_visible_index_ < row_count) {
    first = std::min(first, forced_visible_index_);
    last = std::max(last, forced_visible_index_);
  }

  return {first, last, row_count};
}

SidebarVisibilityManager::VisibleRange
SidebarVisibilityManager::ComputeVisibleRangeForProjection(
    const gfx::Rect& section_local_visible_rect,
    const std::vector<int>& cumulative_tops,
    int total_height) const {
  const int row_count = static_cast<int>(cumulative_tops.size());
  auto cumulative_top_at = [&cumulative_tops](int index) {
    return cumulative_tops[index];
  };
  return ComputeVisibleRange(section_local_visible_rect, row_count, total_height,
                             cumulative_top_at);
}

}  // namespace maho
