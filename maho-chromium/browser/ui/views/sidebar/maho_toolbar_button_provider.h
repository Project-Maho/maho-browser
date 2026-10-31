// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_TOOLBAR_BUTTON_PROVIDER_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_TOOLBAR_BUTTON_PROVIDER_H_

#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/sequence_checker.h"
#include "chrome/browser/ui/page_action/page_action_icon_type.h"
#include "chrome/browser/ui/views/frame/toolbar_button_provider.h"
#include "chrome/common/chrome_version.h"
#include "ui/actions/action_id.h"

// Upstream dropped the deprecated pure virtual
// ToolbarButtonProvider::GetAvatarToolbarButton() in 149.0.7798.0 (commit
// 6c9ccc1c). The ee4bd9e9 pin (7795) still declares it; 72f18f12 (7812) does
// not, so the override exists only where the interface requires it.
#define MAHO_TOOLBAR_BUTTON_PROVIDER_HAS_AVATAR_BUTTON \
  (CHROME_VERSION_BUILD < 7798)

// 72f18f12 (7812) resolves ToolbarButtonProvider::From() through browser
// unowned user data; ee4bd9e9 (7795) has no slot. No pin sits in between, so
// the Arc-layout provider registers itself only above the ee4bd9e9 build (the
// toolbar_view.cc split hunk uses the same threshold to release the slot).
#define MAHO_TOOLBAR_BUTTON_PROVIDER_OWNS_USER_DATA \
  (CHROME_VERSION_BUILD > 7795)

#if MAHO_TOOLBAR_BUTTON_PROVIDER_OWNS_USER_DATA
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#endif

class AvatarToolbarButton;
class BrowserView;
class BrowserWindowInterface;
class ToolbarView;

namespace maho {

class MahoContentsHeaderView;
class MahoControlActivityIndicatorView;

// Which active-pane header control a bubble anchors to.
enum class MahoHeaderAnchorKind {
  kTranslate,
  kUtility,
  kSecurity,
};

// Maho-owned implementation of `ToolbarButtonProvider` owned by `BrowserView`
// when `BrowserView::IsMahoArcLayoutActive()` is true and returned from
// `BrowserView::toolbar_button_provider()`. Where the browser has a
// `ToolbarButtonProvider` user-data slot (72f18f12) the provider also takes
// it, so `ToolbarButtonProvider::From()` callers anchor to the pane header;
// upstream `ToolbarView` skips its own registration under the same Arc
// predicate. Like upstream, the registration is fixed for the window's
// lifetime: flipping the sidebar-layout pref later only changes header
// visibility.
//
// Anchor methods (`GetBubbleAnchor`, `GetDefaultExtensionDialogAnchor`) route
// to the active pane's `MahoContentsHeaderView`: translate to the translate
// label, page info to the security icon, everything else to the utility icon.
// Translate and page info fall back to the utility icon when their control is
// not drawn. Every non-anchor virtual is delegated through the
// `ToolbarButtonProvider` interface of the still-constructed-but-zero-bounded
// upstream `ToolbarView` so callers that reach for child views (autofill, IPH,
// focus traversal, accessibility) get the same handles they would in non-Arc
// mode.
class MahoToolbarButtonProvider : public ToolbarButtonProvider {
 public:
  MahoToolbarButtonProvider(BrowserWindowInterface* browser,
                            BrowserView* browser_view);
  MahoToolbarButtonProvider(const MahoToolbarButtonProvider&) = delete;
  MahoToolbarButtonProvider& operator=(const MahoToolbarButtonProvider&) =
      delete;
  ~MahoToolbarButtonProvider() override;

  ExtensionsContainerViews* GetExtensionsContainerViews() override;
  PinnedToolbarActions* GetPinnedToolbarActions() override;
  gfx::Size GetToolbarButtonSize() const override;
  views::BubbleAnchor GetDefaultExtensionDialogAnchor() override;
  page_actions::PageActionViewInterface* GetPageActionViewInterface(
      actions::ActionId action_id) override;
  AppMenuControl* GetAppMenuControl() override;
  gfx::Rect GetFindBarBoundingBox(int contents_bottom) override;
  void FocusToolbar() override;
  views::AccessiblePaneView* GetAsAccessiblePaneView() override;
  views::BubbleAnchor GetBubbleAnchor(
      std::optional<actions::ActionId> action_id) override;
  views::BubbleAnchor GetPageActionBubbleAnchor(
      actions::ActionId action_id) override;
  void ZoomChangedForActiveTab(bool can_show_bubble) override;
  AvatarToolbarButtonInterface* GetAvatarToolbarButtonInterface() override;
#if MAHO_TOOLBAR_BUTTON_PROVIDER_HAS_AVATAR_BUTTON
  AvatarToolbarButton* GetAvatarToolbarButton() override;
#endif
  ToolbarButton* GetBackButton() override;
  ReloadControl* GetReloadButton() override;
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

  // Pure anchor-selection rule over the active pane header's controls, split
  // out so the resolution order can be asserted without a BrowserView.
  // `translate` is the header's translate anchor (the translate label, or the
  // utility icon when the label is not drawn). A kind whose control is not
  // drawn falls back to the utility icon; nullptr means no header control is
  // on screen (e.g. the header is hidden in fullscreen) and the caller must
  // use its last-resort anchor.
  static views::View* ResolveHeaderAnchor(MahoHeaderAnchorKind kind,
                                          views::View* translate,
                                          bool translate_drawn,
                                          views::View* utility,
                                          bool utility_drawn,
                                          views::View* security,
                                          bool security_drawn);

 private:
  MahoContentsHeaderView* GetActiveHeader() const;
  views::View* ResolveActiveHeaderAnchor(MahoHeaderAnchorKind kind) const;
  views::BubbleAnchor LastResortAnchor() const;

  raw_ptr<BrowserView> browser_view_;
  // The upstream ToolbarView, held through its public ToolbarButtonProvider
  // interface so delegation needs no friend access to ToolbarView.
  raw_ptr<ToolbarButtonProvider> upstream_toolbar_;
  raw_ptr<MahoControlActivityIndicatorView> sidebar_control_activity_ = nullptr;
  raw_ptr<MahoControlActivityIndicatorView> toolbar_control_activity_ = nullptr;
  SEQUENCE_CHECKER(sequence_checker_);
#if MAHO_TOOLBAR_BUTTON_PROVIDER_OWNS_USER_DATA
  // Last member so the registration ends before any other member dies.
  ui::ScopedUnownedUserData<ToolbarButtonProvider> scoped_unowned_user_data_;
#endif
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_TOOLBAR_BUTTON_PROVIDER_H_
