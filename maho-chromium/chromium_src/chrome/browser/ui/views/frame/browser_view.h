// Copyright 2026 Maho Browser. All rights reserved.
// chromium_src overlay for browser_view.h
//
// Documents the upstream BrowserView header patches required for Maho's split
// view host integration and Arc toolbar button provider.
//
// PATCHES:
//
// 1. Add forward declarations near other Maho declarations:
//
//      class MahoSplitViewController;
//      namespace maho { class MahoToolbarButtonProvider; }
//
// 2. Add BrowserView public accessors adjacent to the existing Maho command
//    overlay helpers:
//
//      MahoSplitViewController* GetOrCreateMahoSplitViewController();
//      MahoSplitViewController* GetMahoSplitViewController() const;
//
//    `GetOrCreate...` is the mutation entry point for split-view actions.
//    `GetMaho...` is inspection-only and must not force lazy creation.
//
// 3. Add BrowserView member storage in the private/member section:
//
//      std::unique_ptr<MahoSplitViewController> split_view_controller_;
//
//      std::unique_ptr<maho::MahoToolbarButtonProvider>
//          maho_toolbar_button_provider_;
//
//    `maho_toolbar_button_provider_` is instantiated in AddedToWidget() when
//    IsMahoArcLayoutActive() and kMahoArcToolbarButtonProvider is enabled, so
//    that ToolbarButtonProvider::From(browser_) resolves before the CHECK.
//
// This mirrors the BrowserView-owned/lazy-created controller pattern already
// used for Maho command overlay hosting.

#ifndef MAHO_CHROMIUM_SRC_BROWSER_VIEW_H_OVERLAY_
#define MAHO_CHROMIUM_SRC_BROWSER_VIEW_H_OVERLAY_

// This file documents the upstream patch. The actual declarations are applied
// directly to the upstream header.

#endif  // MAHO_CHROMIUM_SRC_BROWSER_VIEW_H_OVERLAY_
