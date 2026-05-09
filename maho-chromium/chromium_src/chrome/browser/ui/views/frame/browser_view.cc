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
//        std::string current_url;
//        auto* active_contents =
//            browser_->tab_strip_model()->GetActiveWebContents();
//        if (active_contents) {
//          current_url = active_contents->GetLastCommittedURL().spec();
//        }
//        container->ShowCommandOverlayForCurrentTab(current_url);
//        return;
//      }
//    Opens the palette in kCurrentTab mode with URL prefill + select-all.
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
//      "//maho/browser/ui/views/split_view:maho_split_view",
//
// 7. #include "maho/browser/ui/views/split_view/maho_split_view_controller.h"
//
// 8. Forward declaration in browser_view.h:
//      class MahoSplitViewController;
//
//    Member field in BrowserView class:
//      std::unique_ptr<MahoSplitViewController> split_view_controller_;
//
// 9. Lazy-init factory (creates controller on first use):
//      MahoSplitViewController* BrowserView::GetOrCreateMahoSplitViewController() {
//        if (!split_view_controller_) {
//          split_view_controller_ = std::make_unique<MahoSplitViewController>(browser_);
//        }
//        return split_view_controller_.get();
//      }
//
// 10. Const accessor (returns nullptr if not yet created):
//      MahoSplitViewController* GetMahoSplitViewController() const {
//        return split_view_controller_.get();
//      }
//
// 11. #include "maho/browser/ui/views/command/maho_shortcut_interceptor.h"
//     (resolved via //maho/browser/ui/views/command:maho_command_overlay)
//
// 12. In BrowserView::PreHandleKeyboardEvent(), after the early-return for
//     non-key event types and BEFORE focus_manager setup, insert:
//        if (event.GetType() == blink::WebInputEvent::Type::kRawKeyDown &&
//            maho::MahoShortcutInterceptor::MaybeHandleKeyEvent(browser_.get(),
//                                                                event)) {
//          return content::KeyboardEventProcessingResult::HANDLED;
//        }
//    This resolves Maho-registered shortcuts (e.g. ⌘S → toggle_sidebar)
//    via Rust maho_core_resolve_shortcut() before Chromium's accelerator table.
