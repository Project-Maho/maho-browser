// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_PEEK_MAHO_PEEK_ROUTE_H_
#define MAHO_BROWSER_UI_VIEWS_PEEK_MAHO_PEEK_ROUTE_H_

#include "ui/base/window_open_disposition.h"

namespace content {
using WindowOpenDisposition = ::WindowOpenDisposition;
}  // namespace content

namespace maho {

enum class PeekSeam {
  kAddNewContents,
  kOpenUrlFromTab,
  kSameTabThrottle,
};

enum class PeekSourceRole {
  kNormal,
  kPinned,
  kFavorite,
  kUnresolved,
};

enum class PeekSlotState {
  kClosed,
  kBusy,
};

enum class PeekRoute {
  kOpenInPeek,
  kOpenInForegroundTab,
  kFallThrough,
};

struct PeekRouteInput {
  bool master_enabled;
  bool popup_routing_enabled;
  bool link_routing_enabled;
  PeekSeam seam;
  content::WindowOpenDisposition disposition;
  PeekSourceRole source_role;
  bool is_user_initiated;
  bool force_tab;
  bool force_peek;
  bool is_maho_mini_gesture;
  bool atc_has_cross_space_target;
  PeekSlotState peek_slot_state;
};

PeekRoute DecidePeekRoute(const PeekRouteInput& input);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_PEEK_MAHO_PEEK_ROUTE_H_
