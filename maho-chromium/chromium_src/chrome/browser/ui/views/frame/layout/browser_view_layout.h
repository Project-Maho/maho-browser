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

// This file documents the upstream patch. The actual struct modification
// is applied directly to the upstream header.

#endif  // MAHO_CHROMIUM_SRC_BROWSER_VIEW_LAYOUT_H_OVERLAY_
