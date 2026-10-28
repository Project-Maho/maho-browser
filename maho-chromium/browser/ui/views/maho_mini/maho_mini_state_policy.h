// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_MAHO_MINI_MAHO_MINI_STATE_POLICY_H_
#define MAHO_BROWSER_UI_VIEWS_MAHO_MINI_MAHO_MINI_STATE_POLICY_H_

namespace maho {

enum class MahoMiniState {
  kCompact,
  kExpanded,
};

enum class MahoMiniStateEvent {
  kToggle,
  kNavigation,
  kPromote,
};

MahoMiniState TransitionMahoMiniState(MahoMiniState state,
                                      MahoMiniStateEvent event);

}  // namespace maho

#endif
