// Copyright 2026 Maho Browser. All rights reserved.
// chromium_src overlay for location_bar_view.cc
//
// The Maho utility panel integration lives in the maho/browser/ui/views/
// location_bar/ directory. This overlay only wires that feature into the
// upstream LocationBarView implementation.
//
// Upstream patches applied to LocationBarView:
//
// 1. #include "maho/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.h"
//    #include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.h"
//    #include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h"
//
// 2. In LocationBarView::Init(), after existing page-action icons:
//      utility_icon_view_ = AddChildView(
//          std::make_unique<maho::MahoLocationBarUtilityIconView>(
//              base::BindRepeating(&LocationBarView::ShowUtilityPanel,
//                                  base::Unretained(this))));
//
// 3. New member: raw_ptr<maho::MahoLocationBarUtilityIconView> utility_icon_view_;
//
// 4. In LocationBarView::OnOmniboxHovered:
//      if (utility_icon_view_) { utility_icon_view_->SetHoverVisible(true/false); }
//
// 5. New method LocationBarView::ShowUtilityPanel():
//      auto model = maho::BuildMahoLocationBarUtilityPanelModel(this);
//      return maho::MahoLocationBarUtilityPanelView::Show(utility_icon_view_,
//                                                         std::move(model));
