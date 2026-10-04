// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_section_policy.h"

namespace maho {

SectionKind SectionKindFromTabSection(MahoSidebarTabSection section) {
  return section == MahoSidebarTabSection::kPinned ? SectionKind::kPinned
                                                   : SectionKind::kNormal;
}

bool IsSameSectionRootDrop(SectionKind target,
                            const SidebarDragPayload& payload) {
  if (NeedsFolderEscape(payload)) {
    return false;
  }
  const SectionKind source =
      (payload.origin == SidebarDragOrigin::kPinnedSection)
          ? SectionKind::kPinned
          : SectionKind::kNormal;
  return source == target;
}

bool NeedsFolderEscape(const SidebarDragPayload& payload) {
  return !payload.source_parent_folder_id.empty();
}

}  // namespace maho
