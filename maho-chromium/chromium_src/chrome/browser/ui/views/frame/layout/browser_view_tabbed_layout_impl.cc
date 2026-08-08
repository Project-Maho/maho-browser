// Copyright 2026 Maho Browser. All rights reserved.
// chromium_src overlay reference for browser_view_tabbed_layout_impl.cc
//
// Documents the upstream patches applied to this file:
//
// 1. #include "components/prefs/pref_service.h"
//    #include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
//
// 2. IsMahoArcLayoutActive(): returns true when
//    views().maho_sidebar_container is non-null and visible.
//
// 3. GetTabStripType(): returns kNone when IsMahoArcLayoutActive(),
//    suppressing both horizontal and vertical Chromium tab strips.
//
// 4. CalculateProposedLayout(): early-returns via CalculateMahoArcLayout()
//    when Arc mode is active.
//
// 5. CalculateMahoArcLayout(): dedicated two-pane layout:
//    - Left rail (maho_sidebar_container) at fixed pref width, full height
//    - Contents container fills remaining width, full height
//    - All top chrome (top_container, tab strips, toolbar, bookmarks,
//      loading bar, separator) laid out at zero bounds with visibility false
//    - Infobar placed in contents area
//    - Contents-height side panel preserved if visible
//
// 6. GetMinimumSize(): accounts for rail width in Arc mode.
//
// 7. CalculateMahoArcLayout(): sizes BrowserView's nested
//    maho_content_gradient_view to the local contents-container bounds. The
//    gradient remains the first child behind MultiContentsView and is visible
//    only while the BrowserView-observed TabStripModel has zero live tabs.
//
// OLD APPROACH (retired):
// Previously, CalculateProposedLayout() had a block at the end that shifted
// all existing child layouts rightward by sidebar_width. This preserved the
// full top chrome (tab strip, toolbar, etc.) which contradicts the Arc UX
// target. That block is now a no-op; the early return to
// CalculateMahoArcLayout() handles the entire layout.
