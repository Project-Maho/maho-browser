// Copyright 2026 Maho Browser. All rights reserved.

// Non-macOS stub. Traffic lights are a macOS-only concept; on other platforms
// this always returns std::nullopt and MahoSidebarTopBarView leaves the
// BoxLayout top inset at its constructor default (kTopBarInsetTopDp = 0).
// This TU exists so the sidebar links on every platform without a per-
// platform GN branch in every consumer.

#include "maho/browser/ui/views/sidebar/maho_traffic_light_geometry.h"

namespace maho {

std::optional<int> GetTrafficLightCenterYInView(const views::View* /*view*/) {
  return std::nullopt;
}

}  // namespace maho
