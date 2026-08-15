// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_TOOLBAR_BUTTON_PROVIDER_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_TOOLBAR_BUTTON_PROVIDER_H_

#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/sequence_checker.h"
#include "chrome/browser/ui/page_action/page_action_icon_type.h"
#include "chrome/browser/ui/views/frame/toolbar_button_provider.h"
#include "ui/actions/action_id.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"

class BrowserView;
class BrowserWindowInterface;
class ToolbarView;

namespace maho {

class MahoControlActivityIndicatorView;
class MahoSidebarSearchView;

// Maho-owned implementation of `ToolbarButtonProvider` registered on
// `BrowserWindowInterface` whenever `BrowserView::IsMahoArcLayoutActive()` is
// true.  The upstream `ToolbarView` skips its own self-registration in this
// mode (see `apply_chromium_src_overrides.py` — `toolbar_view.cc` overlay) so
// `ToolbarButtonProvider::From(browser)` resolves to this Maho provider.
//
// Anchor methods (`GetBubbleAnchor`, `GetDefaultExtensionDialogAnchor`) route
// to the sidebar pill (`MahoSidebarSearchView::secondary_label_view()` or
// `idle_button_view()`).  Every non-anchor virtual is delegated to the
// still-constructed-but-zero-bounded upstream `ToolbarView` so callers that
// reach for child views (autofill, IPH, focus traversal, accessibility) get
// the same handles they would in non-Arc mode.
class MahoToolbarButtonProvider : public ToolbarButtonProvider {
 public:
  MahoToolbarButtonProvider(BrowserWindowInterface* browser,
                            BrowserView* browser_view,
                            MahoSidebarSearchView* search_view);
  MahoToolbarButtonProvider(const MahoToolbarButtonProvider&) = delete;
  MahoToolbarButtonProvider& operator=(const MahoToolbarButtonProvider&) =
      delete;
  ~MahoToolbarButtonProvider() override;

  ExtensionsToolbarDesktop* GetExtensionsToolbarDesktop() override;
  PinnedToolbarActions* GetPinnedToolbarActions() override;
  gfx::Size GetToolbarButtonSize() const override;
  views::BubbleAnchor GetDefaultExtensionDialogAnchor() override;
  PageActionIconView* GetPageActionIconView(PageActionIconType type) override;
  IconLabelBubbleView* GetPageActionView(actions::ActionId action_id) override;
  AppMenuControl* GetAppMenuControl() override;
  gfx::Rect GetFindBarBoundingBox(int contents_bottom) override;
  void FocusToolbar() override;
  views::AccessiblePaneView* GetAsAccessiblePaneView() override;
  views::BubbleAnchor GetBubbleAnchor(
      std::optional<actions::ActionId> action_id) override;
  void ZoomChangedForActiveTab(bool can_show_bubble) override;
  AvatarToolbarButtonInterface* GetAvatarToolbarButtonInterface() override;
  ToolbarButton* GetBackButton() override;
  ReloadControl* GetReloadButton() override;
  IntentChipButton* GetIntentChipButton() override;
  ToolbarButton* GetDownloadButton() override;
  WebUIToolbarWebView* GetWebUIToolbarViewForTesting() override;

  // Control-activity chip host resolution. The sidebar top bar owns the
  // primary chip; when the sidebar is collapsed that chip stops being drawn,
  // so a toolbar-hosted chip takes over as the fallback surface. The provider
  // stores unowned pointers only and never holds controller/session state.
  void SetSidebarControlActivityIndicator(
      MahoControlActivityIndicatorView* indicator);
  void SetToolbarControlActivityIndicator(
      MahoControlActivityIndicatorView* indicator);

  // Returns the chip that should currently render controller attribution, or
  // nullptr when neither host can display it.
  MahoControlActivityIndicatorView* GetControlActivityIndicator() const;

  // Pure host-selection rule, split out so the collapsed-sidebar fallback can
  // be asserted without constructing a BrowserView or a toolbar.
  static MahoControlActivityIndicatorView* ResolveControlActivityHost(
      MahoControlActivityIndicatorView* sidebar_indicator,
      bool sidebar_indicator_drawn,
      MahoControlActivityIndicatorView* toolbar_indicator);

 private:
  views::View* ResolveTranslateAnchor() const;
  views::View* ResolvePillAnchor() const;
  views::BubbleAnchor LastResortAnchor() const;

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<BrowserView> browser_view_;
  raw_ptr<MahoSidebarSearchView> search_view_;
  raw_ptr<ToolbarView> upstream_toolbar_;
  raw_ptr<MahoControlActivityIndicatorView> sidebar_control_activity_ = nullptr;
  raw_ptr<MahoControlActivityIndicatorView> toolbar_control_activity_ = nullptr;
  std::optional<ui::ScopedUnownedUserData<ToolbarButtonProvider>>
      scoped_unowned_user_data_;
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_TOOLBAR_BUTTON_PROVIDER_H_
