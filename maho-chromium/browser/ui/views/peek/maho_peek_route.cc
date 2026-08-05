// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/peek/maho_peek_route.h"

namespace maho {
namespace {

bool IsDispositionEligible(PeekSeam seam,
                           content::WindowOpenDisposition disposition) {
  switch (seam) {
    case PeekSeam::kAddNewContents:
      return disposition == content::WindowOpenDisposition::NEW_POPUP;
    case PeekSeam::kOpenUrlFromTab:
      return disposition ==
                 content::WindowOpenDisposition::NEW_FOREGROUND_TAB ||
             disposition ==
                 content::WindowOpenDisposition::NEW_BACKGROUND_TAB ||
             disposition == content::WindowOpenDisposition::NEW_WINDOW;
    case PeekSeam::kSameTabThrottle:
      return disposition == content::WindowOpenDisposition::CURRENT_TAB;
  }
}

bool IsPeekLinkSource(PeekSourceRole source_role) {
  return source_role == PeekSourceRole::kPinned ||
         source_role == PeekSourceRole::kFavorite;
}

}  // namespace

PeekRoute DecidePeekRoute(const PeekRouteInput& input) {
  const bool force_tab_link =
      input.force_tab && input.seam != PeekSeam::kAddNewContents;
  if (!input.master_enabled || force_tab_link || input.is_maho_mini_gesture ||
      input.atc_has_cross_space_target ||
      !IsDispositionEligible(input.seam, input.disposition) ||
      input.source_role == PeekSourceRole::kUnresolved) {
    return PeekRoute::kFallThrough;
  }

  switch (input.seam) {
    case PeekSeam::kAddNewContents:
      if (!input.popup_routing_enabled) {
        return PeekRoute::kFallThrough;
      }
      break;
    case PeekSeam::kOpenUrlFromTab:
    case PeekSeam::kSameTabThrottle:
      if (!input.link_routing_enabled || !IsPeekLinkSource(input.source_role) ||
          !input.is_user_initiated) {
        return PeekRoute::kFallThrough;
      }
      break;
  }

  return input.peek_slot_state == PeekSlotState::kBusy
             ? PeekRoute::kOpenInForegroundTab
             : PeekRoute::kOpenInPeek;
}

}  // namespace maho
