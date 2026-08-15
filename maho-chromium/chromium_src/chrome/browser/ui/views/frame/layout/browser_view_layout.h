// Copyright 2026 Maho Browser. All rights reserved.
// chromium_src overlay for browser_view_layout.h
//
// Adds maho_sidebar_container to BrowserViewLayoutViews so the layout
// layer can discover the sidebar host without runtime view-ID lookups.
//
// PATCH: Insert the following field into struct BrowserViewLayoutViews
// after the `top_container_separator` member:
//
//   raw_ptr<views::View> maho_sidebar_container = nullptr;
//
// This field is set in BrowserView::AddedToWidget() and read by
// BrowserViewLayout / BrowserViewTabbedLayoutImpl for left-side width
// reservation in browser_view_tabbed_layout_impl.cc overlay.

#ifndef MAHO_CHROMIUM_SRC_BROWSER_VIEW_LAYOUT_H_OVERLAY_
#define MAHO_CHROMIUM_SRC_BROWSER_VIEW_LAYOUT_H_OVERLAY_

// The chromium_src directory precedes the upstream source root on the include
// path, so a documentation-only file here would hide the real declaration.
// Forward to the next matching header; the tracked override script applies the
// Maho fields to that upstream file before compilation.
#include_next "chrome/browser/ui/views/frame/layout/browser_view_layout.h"

#endif  // MAHO_CHROMIUM_SRC_BROWSER_VIEW_LAYOUT_H_OVERLAY_
