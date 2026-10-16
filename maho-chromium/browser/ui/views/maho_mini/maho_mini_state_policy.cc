// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/maho_mini/maho_mini_state_policy.h"

namespace maho {

MahoMiniState TransitionMahoMiniState(MahoMiniState state,
                                      MahoMiniStateEvent event) {
  switch (event) {
    case MahoMiniStateEvent::kToggle:
      return state == MahoMiniState::kCompact ? MahoMiniState::kExpanded
                                              : MahoMiniState::kCompact;
    case MahoMiniStateEvent::kNavigation:
      return MahoMiniState::kExpanded;
    case MahoMiniStateEvent::kPromote:
      return MahoMiniState::kExpanded;
  }
  return state;
}

}  // namespace maho
