// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_SECTION_POLICY_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_SECTION_POLICY_H_

#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"

namespace maho {

enum class SectionKind {
  kPinned,
  kNormal,
  kFavorites,
};

SectionKind SectionKindFromTabSection(MahoSidebarTabSection section);
bool IsSameSectionRootDrop(SectionKind target, const SidebarDragPayload& payload);
bool NeedsFolderEscape(const SidebarDragPayload& payload);

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_SECTION_POLICY_H_
