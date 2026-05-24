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
//      if (is_user_initiated && maho_sidebar_container_ &&
//          base::FeatureList::IsEnabled(maho::features::kMahoUnifiedClickPath)) {
//        ShowMahoCommandOverlayForCurrentTab();
//        return;
//      }
//    Opens the palette in kCurrentTab mode with URL prefill + select-all.
//    Delegates to ShowMahoCommandOverlayForCurrentTab() which fetches the
//    active tab URL internally rather than receiving it as a parameter.
//    The kMahoUnifiedClickPath check gates only this intercept; callers that
//    reach ShowMahoCommandOverlayForCurrentTab() directly (Cmd+L shortcut
//    interceptor, Cmd+T sidebar button) are unaffected by the flag.
//    Enabled by default; disabling it restores the upstream location-bar
//    mouse/keyboard focus path (kill-switch retained for QA sign-off).
//
// 5. Member fields in BrowserView class (public, in member section):
//      raw_ptr<views::View> maho_sidebar_container_ = nullptr;
//      raw_ptr<views::View> maho_create_space_blank_view_ = nullptr;
//      std::unique_ptr<maho::MahoCommandOverlayController>
//          maho_command_overlay_controller_;
//
// 5a. Three helper methods added to BrowserView:
//
//      void BrowserView::ShowMahoCommandOverlayForCurrentTab() {
//        if (!maho_command_overlay_controller_)
//          return;
//        std::string current_url;
//        if (auto* contents =
//                browser_->tab_strip_model()->GetActiveWebContents()) {
//          const GURL& visible = contents->GetVisibleURL();
//          const GURL& url =
//              visible.is_empty() ? contents->GetLastCommittedURL() : visible;
//          // Denylist: skip javascript: (script injection), data: (opaque blobs),
//          // and about:blank (empty-tab sentinel). All other schemes — including
//          // chrome://, devtools://, file://, http, https, ftp — pass through.
//          if (!url.is_empty() &&
//              !url.SchemeIs("javascript") &&
//              !url.SchemeIs("data") &&
//              url.spec() != "about:blank")
//            current_url = url.spec();
//        }
//        maho_command_overlay_controller_->Show(
//            GetWidget(),
//            maho::CommandOverlayMode::kCurrentTab,
//            current_url,
//            /*select_initial_text=*/true);
//      }
//
//      void BrowserView::ShowMahoCommandOverlayForNewTab() {
//        if (!maho_command_overlay_controller_)
//          return;
//        maho_command_overlay_controller_->Show(
//            GetWidget(),
//            maho::CommandOverlayMode::kNewTab);
//      }
//
//      void BrowserView::ShowMahoCommandOverlayForSearch(
//          const std::string& initial_query) {
//        if (!maho_command_overlay_controller_)
//          return;
//        maho_command_overlay_controller_->Show(
//            GetWidget(),
//            maho::CommandOverlayMode::kSearch,
//            initial_query);
//      }
//
//    These three methods are the canonical entry points from all call sites:
//    MahoShortcutInterceptor (Cmd+L, command_bar action),
//    MahoSidebarContainerView (Cmd+T via ShowCommandOverlayForNewTab,
//    search-entry via ShowCommandOverlayForCurrentTab/ForSearch), and
//    MahoCommandActionHandler (command_bar action). The container's
//    ShowCommandOverlay* methods are thin delegation stubs that forward to
//    these BrowserView methods so callers without a direct BrowserView
//    reference can still reach the controller.
//
//    The controller is initialized alongside the sidebar container:
//      if (GetIsNormalType()) {
//        maho_sidebar_container_ = AddChildView(...);
//        maho_command_overlay_controller_ =
//            std::make_unique<maho::MahoCommandOverlayController>(browser_.get());
//      }
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
// Sources injected into chrome/browser/ui BUILD.gn (sidebar pane files):
//      "//maho/browser/ui/views/sidebar/maho_sidebar_easels_view.cc",
//      "//maho/browser/ui/views/sidebar/maho_sidebar_easels_view.h",
//      "//maho/browser/ui/views/sidebar/maho_sidebar_notes_view.cc",
//      "//maho/browser/ui/views/sidebar/maho_sidebar_notes_view.h",
//
// 7. #include "maho/browser/ui/views/split_view/maho_split_view_controller.h"
//
// 8. Forward declaration in browser_view.h:
//      class MahoSplitViewController;
//
//    Member field in BrowserView class:
//      std::unique_ptr<MahoSplitViewController> split_view_controller_;
//
// 9. Lazy-init accessor (creates controller on first use):
//
//      MahoSplitViewController*
//      BrowserView::GetOrCreateMahoSplitViewController() {
//        if (!split_view_controller_) {
//          split_view_controller_ =
//              std::make_unique<MahoSplitViewController>(browser_.get());
//        }
//        return split_view_controller_.get();
//      }
//
//    This is the canonical entry point for split-view mutations from command
//    and action-handler paths owned by BrowserView. Context menu split actions
//    still use their legacy direct controller construction until BrowserView
//    exposes a suitable public delegation hook for them.
//
// 10. Const read-only accessor (inline in browser_view.h; returns nullptr if
//     not yet created):
//
//      MahoSplitViewController*
//      BrowserView::GetMahoSplitViewController() const {
//        return split_view_controller_.get();
//      }
//
//     Use this for inspection-only paths that must not force creation (e.g.
//     serialization, metrics). Returns nullptr when no split has been
//     requested in this window's lifetime.
//
// 10a. Test accessor for the command overlay controller (in browser_view.h
//      public section, adjacent to maho_spaces_overlay_visible_for_testing):
//      maho::MahoCommandOverlayController*
//      GetMahoCommandOverlayControllerForTesting() const {
//        return maho_command_overlay_controller_.get();
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
//
// ---- Call-site inventory for GetOrCreateMahoSplitViewController() ----
//
// The split-view command actions route through this accessor so split-view
// state is owned by the BrowserView instead of ephemeral per-command
// controller instances.
//
// Current caller:
//   - maho/browser/ui/views/command/maho_command_action_handler.cc
//       actions: add_split_view, remove_split_view, toggle_split_view
//                swap_split_view
//       via file-local GetOrCreateSplitViewController(Browser*) helper
