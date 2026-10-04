// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_toolbar_button_provider.h"

#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "maho/browser/ui/views/frame/maho_contents_header_view.h"
#include "maho/browser/ui/views/sidebar/maho_control_activity_indicator_view.h"
#include "ui/views/view.h"

namespace maho {

namespace {

MahoHeaderAnchorKind AnchorKindForAction(
    std::optional<actions::ActionId> action_id) {
  if (action_id == kActionShowTranslate) {
    return MahoHeaderAnchorKind::kTranslate;
  }
  return MahoHeaderAnchorKind::kUtility;
}

}  // namespace

MahoToolbarButtonProvider::MahoToolbarButtonProvider(
    [[maybe_unused]] BrowserWindowInterface* browser,
    BrowserView* browser_view)
    : browser_view_(browser_view),
      upstream_toolbar_(browser_view ? browser_view->toolbar() : nullptr)
#if MAHO_TOOLBAR_BUTTON_PROVIDER_OWNS_USER_DATA
      ,
      scoped_unowned_user_data_(browser->GetUnownedUserDataHost(), *this)
#endif
{
}

MahoToolbarButtonProvider::~MahoToolbarButtonProvider() = default;

void MahoToolbarButtonProvider::SetSidebarControlActivityIndicator(
    MahoControlActivityIndicatorView* indicator) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sidebar_control_activity_ = indicator;
}

void MahoToolbarButtonProvider::SetToolbarControlActivityIndicator(
    MahoControlActivityIndicatorView* indicator) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  toolbar_control_activity_ = indicator;
}

// static
MahoControlActivityIndicatorView*
MahoToolbarButtonProvider::ResolveControlActivityHost(
    MahoControlActivityIndicatorView* sidebar_indicator,
    bool sidebar_indicator_drawn,
    MahoControlActivityIndicatorView* toolbar_indicator) {
  // The sidebar chip wins whenever it is actually on screen; a collapsed or
  // hidden sidebar falls back to the toolbar chip so controller attribution
  // and Stop never disappear with the sidebar.
  if (sidebar_indicator && sidebar_indicator_drawn) {
    return sidebar_indicator;
  }
  return toolbar_indicator;
}

MahoControlActivityIndicatorView*
MahoToolbarButtonProvider::GetControlActivityIndicator() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return ResolveControlActivityHost(
      sidebar_control_activity_,
      sidebar_control_activity_ && sidebar_control_activity_->IsDrawn(),
      toolbar_control_activity_);
}

// static
views::View* MahoToolbarButtonProvider::ResolveHeaderAnchor(
    MahoHeaderAnchorKind kind,
    views::View* translate,
    bool translate_drawn,
    views::View* utility,
    bool utility_drawn,
    views::View* security,
    bool security_drawn) {
  if (kind == MahoHeaderAnchorKind::kTranslate && translate &&
      translate_drawn) {
    return translate;
  }
  if (kind == MahoHeaderAnchorKind::kSecurity && security && security_drawn) {
    return security;
  }
  // Every kind falls back to the utility icon, the header's one control that
  // is present on every page.
  if (utility && utility_drawn) {
    return utility;
  }
  return nullptr;
}

MahoContentsHeaderView* MahoToolbarButtonProvider::GetActiveHeader() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!browser_view_) {
    return nullptr;
  }
  ContentsContainerView* container =
      browser_view_->GetActiveContentsContainerView();
  return container ? container->maho_contents_header() : nullptr;
}

views::View* MahoToolbarButtonProvider::ResolveActiveHeaderAnchor(
    MahoHeaderAnchorKind kind) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoContentsHeaderView* header = GetActiveHeader();
  if (!header) {
    return nullptr;
  }
  views::View* translate = header->translate_anchor();
  views::View* utility = header->utility_anchor();
  views::View* security = header->security_anchor();
  return ResolveHeaderAnchor(kind, translate, translate && translate->IsDrawn(),
                             utility, utility && utility->IsDrawn(), security,
                             security && security->IsDrawn());
}

views::BubbleAnchor MahoToolbarButtonProvider::LastResortAnchor() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (browser_view_) {
    if (auto* tc = browser_view_->top_container();
        tc && tc->IsDrawn() && !tc->bounds().IsEmpty()) {
      return views::BubbleAnchor(tc);
    }
    return views::BubbleAnchor(browser_view_);
  }
  return views::BubbleAnchor();
}

views::BubbleAnchor MahoToolbarButtonProvider::GetBubbleAnchor(
    std::optional<actions::ActionId> action_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (views::View* anchor =
          ResolveActiveHeaderAnchor(AnchorKindForAction(action_id))) {
    return views::BubbleAnchor(anchor);
  }
  return LastResortAnchor();
}

views::BubbleAnchor
MahoToolbarButtonProvider::GetDefaultExtensionDialogAnchor() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (views::View* anchor =
          ResolveActiveHeaderAnchor(MahoHeaderAnchorKind::kUtility)) {
    return views::BubbleAnchor(anchor);
  }
  return LastResortAnchor();
}

views::BubbleAnchor MahoToolbarButtonProvider::GetPageActionBubbleAnchor(
    actions::ActionId action_id) {
  return GetBubbleAnchor(action_id);
}

ExtensionsContainerViews*
MahoToolbarButtonProvider::GetExtensionsContainerViews() {
  return upstream_toolbar_ ? upstream_toolbar_->GetExtensionsContainerViews()
                           : nullptr;
}

PinnedToolbarActions* MahoToolbarButtonProvider::GetPinnedToolbarActions() {
  return upstream_toolbar_ ? upstream_toolbar_->GetPinnedToolbarActions()
                           : nullptr;
}

gfx::Size MahoToolbarButtonProvider::GetToolbarButtonSize() const {
  return upstream_toolbar_ ? upstream_toolbar_->GetToolbarButtonSize()
                           : gfx::Size();
}

page_actions::PageActionViewInterface*
MahoToolbarButtonProvider::GetPageActionViewInterface(
    actions::ActionId action_id) {
  return upstream_toolbar_ ? upstream_toolbar_->GetPageActionViewInterface(action_id)
                           : nullptr;
}

AppMenuControl* MahoToolbarButtonProvider::GetAppMenuControl() {
  return upstream_toolbar_ ? upstream_toolbar_->GetAppMenuControl() : nullptr;
}

gfx::Rect MahoToolbarButtonProvider::GetFindBarBoundingBox(
    int contents_bottom) {
  if (upstream_toolbar_) {
    return upstream_toolbar_->GetFindBarBoundingBox(contents_bottom);
  }
  return gfx::Rect();
}

void MahoToolbarButtonProvider::FocusToolbar() {
  if (upstream_toolbar_) {
    upstream_toolbar_->FocusToolbar();
  }
}

views::AccessiblePaneView*
MahoToolbarButtonProvider::GetAsAccessiblePaneView() {
  return upstream_toolbar_ ? upstream_toolbar_->GetAsAccessiblePaneView()
                           : nullptr;
}

void MahoToolbarButtonProvider::ZoomChangedForActiveTab(bool can_show_bubble) {
  if (upstream_toolbar_) {
    upstream_toolbar_->ZoomChangedForActiveTab(can_show_bubble);
  }
}

AvatarToolbarButtonInterface*
MahoToolbarButtonProvider::GetAvatarToolbarButtonInterface() {
  return upstream_toolbar_
             ? upstream_toolbar_->GetAvatarToolbarButtonInterface()
             : nullptr;
}

#if MAHO_TOOLBAR_BUTTON_PROVIDER_HAS_AVATAR_BUTTON
AvatarToolbarButton* MahoToolbarButtonProvider::GetAvatarToolbarButton() {
  return upstream_toolbar_ ? upstream_toolbar_->GetAvatarToolbarButton()
                           : nullptr;
}
#endif

ToolbarButton* MahoToolbarButtonProvider::GetBackButton() {
  return upstream_toolbar_ ? upstream_toolbar_->GetBackButton() : nullptr;
}

ReloadControl* MahoToolbarButtonProvider::GetReloadButton() {
  return upstream_toolbar_ ? upstream_toolbar_->GetReloadButton() : nullptr;
}

ToolbarButton* MahoToolbarButtonProvider::GetDownloadButton() {
  return upstream_toolbar_ ? upstream_toolbar_->GetDownloadButton() : nullptr;
}

WebUIToolbarWebView*
MahoToolbarButtonProvider::GetWebUIToolbarViewForTesting() {
  return upstream_toolbar_ ? upstream_toolbar_->GetWebUIToolbarViewForTesting()
                           : nullptr;
}

}  // namespace maho
