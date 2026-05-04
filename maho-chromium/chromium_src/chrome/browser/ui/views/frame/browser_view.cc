// Copyright 2026 Maho Browser. All rights reserved.
// chromium_src overlay reference for browser_view.cc
//
// Documents the upstream patches applied to browser_view.cc:
//
// 1. #include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
//    #include "maho/browser/ui/views/command/maho_command_overlay_controller.h"
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
// 4. In BrowserView::SetFocusToLocationBar(), BEFORE the original body:
//      if (is_user_initiated && maho_sidebar_container_) {
//        auto* container = static_cast<maho::MahoSidebarContainerView*>(
//            maho_sidebar_container_);
//        container->ToggleCommandOverlay();
//        return;
//      }
//    (Covers ALL paths: Mac NSMenu, Views accelerators, direct calls.)
//
// 5. Member field in BrowserView class (public, in member section):
//      raw_ptr<views::View> maho_sidebar_container_ = nullptr;
//
// 6. In BrowserView::NonClientHitTest(), BEFORE the vertical_tab_strip and
//    projects_panel checks (around line 4763 upstream), insert:
//      if (maho_sidebar_container_ && maho_sidebar_container_->GetVisible()) {
//        gfx::Point test_point(point);
//        if (ConvertedHitTest(parent(), maho_sidebar_container_, &test_point)) {
//          if (static_cast<maho::MahoSidebarContainerView*>(
//                  maho_sidebar_container_)
//                  ->IsPositionInWindowCaption(test_point)) {
//            return HTCAPTION;
//          }
//          return HTCLIENT;
//        }
//      }
//
// GN dep added to chrome/browser/ui/BUILD.gn:
//      "//maho/browser/ui/views/command:maho_command_overlay",
