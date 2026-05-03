// Copyright 2026 Maho Browser. All rights reserved.
// chromium_src overlay reference for browser_view.cc
//
// Documents the upstream patches applied to browser_view.cc:
//
// 1. #include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
//
// 2. In BrowserView::BrowserView(), after contents_height_side_panel_:
//      if (GetIsNormalType()) {
//        maho_sidebar_container_ = AddChildView(
//            std::make_unique<maho::MahoSidebarContainerView>(browser_.get()));
//      }
//
// 3. In BrowserView::AddedToWidget(), layout_views wiring:
//      layout_views.maho_sidebar_container = maho_sidebar_container_;
//
// 4. Member field in BrowserView class (public, in member section):
//      raw_ptr<views::View> maho_sidebar_container_ = nullptr;
//
// GN dep added to chrome/browser/ui/BUILD.gn:
//      "//maho/browser/ui/views/command:maho_command_overlay",
