// Copyright 2026 Maho Browser. All rights reserved.
// chromium_src overlay for location_bar_view.cc
//
// The Maho utility panel integration lives in the maho/browser/ui/views/
// location_bar/ directory. This overlay only wires that feature into the
// upstream LocationBarView implementation.
//
// Upstream patches applied to LocationBarView:
//
// 1. #include "maho/browser/ui/views/location_bar/maho_location_bar_utility_bubble_coordinator.h"
//    #include "maho/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.h"
//    #include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.h"
//    #include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h"
//
// 2. New member: raw_ptr<maho::MahoLocationBarUtilityIconView> utility_icon_view_;
//    New member: std::unique_ptr<maho::MahoLocationBarUtilityBubbleCoordinator>
//                    utility_bubble_coordinator_;
//
// 3. In LocationBarView::Init(), after existing page-action icons:
//      utility_bubble_coordinator_ =
//          std::make_unique<maho::MahoLocationBarUtilityBubbleCoordinator>();
//      utility_bubble_coordinator_->AttachToBrowser(browser_);  // applied via apply_chromium_src_overrides.py
//      utility_icon_view_ = AddChildView(
//          std::make_unique<maho::MahoLocationBarUtilityIconView>(
//              base::BindRepeating(&LocationBarView::ShowUtilityPanel,
//                                  base::Unretained(this))));
//
// 4. In LocationBarView::OnOmniboxHovered:
//      if (utility_icon_view_) { utility_icon_view_->SetHoverVisible(true/false); }
//
// 5. New method LocationBarView::ShowUtilityPanel():
//      if (!utility_bubble_coordinator_) { return nullptr; }
//      auto model = maho::BuildMahoLocationBarUtilityPanelModel(this);
//      utility_bubble_coordinator_->ShowBubble(utility_icon_view_,
//                                               std::move(model));
//      return utility_bubble_coordinator_->IsShowing()
//                 ? utility_bubble_coordinator_->GetBubble()->GetWidget()
//                 : nullptr;
//
//    Routing ShowBubble() through the coordinator (rather than calling
//    MahoLocationBarUtilityPanelView::Show() directly) ensures the coordinator
//    is the single owner of the bubble pointer so that its
//    TabStripModelObserver can auto-dismiss on tab switch.
//
// --- Phase 4: Unified click-path via OnFocus() interception ---
//
// Background: LocationBarView::OnMousePressed() for a left-click already
// calls browser_view->SetFocusToLocationBar(/*is_user_initiated=*/true), which
// (per the browser_view.cc overlay) immediately routes to
// ShowMahoCommandOverlayForCurrentTab() and returns — the upstream omnibox
// focus path is never reached. That path needs no further patching.
//
// The remaining gap is LocationBarView::OnFocus(), which is called whenever
// the view receives keyboard focus (tab-key traversal, Cmd+L keyboard shortcut
// before the shortcut interceptor fires, or any programmatic RequestFocus()
// call). Without the patch below, OnFocus() activates the upstream omnibox
// directly rather than opening the command overlay, producing an inconsistent
// user experience.
//
// Right-click / context-menu: upstream opens the context menu from
// OnMousePressed() when event.IsRightMouseButton(), before focus is
// transferred. OnFocus() is NOT called on a context-menu open, so the guard
// below has no effect on right-click behavior.
//
// 6. #include "chrome/browser/ui/views/frame/browser_view.h"
//
// 7. Override LocationBarView::OnFocus():
//
//      void LocationBarView::OnFocus() {
//        BrowserView* browser_view =
//            BrowserView::GetBrowserViewForBrowser(browser_);
//        if (browser_view) {
//          browser_view->SetFocusToLocationBar(/*is_user_initiated=*/true);
//          return;
//        }
//        LocationBarView_OnFocus(this);
//      }
//
//    The #define rename pattern used here follows the chromium_src convention:
//
//      #define OnFocus LocationBarView_OnFocus
//      #include "chrome/browser/ui/views/location_bar/location_bar_view.cc"
//      #undef OnFocus
//
//    Then the override above is defined after the #include, giving it access
//    to all upstream symbols while the renamed original remains callable as
//    LocationBarView_OnFocus(this).
//
//    GN dep added to the location_bar_views source_set in browser/BUILD.gn:
//      "//chrome/browser/ui/tabs:tab_strip",
