// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_toolbar_button_provider.h"

#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_search_view.h"
#include "ui/base/unowned_user_data/unowned_user_data_host.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"

namespace maho {

MahoToolbarButtonProvider::MahoToolbarButtonProvider(
    BrowserWindowInterface* browser,
    BrowserView* browser_view,
    MahoSidebarSearchView* search_view)
    : browser_(browser),
      browser_view_(browser_view),
      search_view_(search_view),
      upstream_toolbar_(browser_view ? browser_view->toolbar() : nullptr) {
  if (browser_) {
    scoped_unowned_user_data_.emplace(browser_->GetUnownedUserDataHost(),
                                      *this);
  }
}

MahoToolbarButtonProvider::~MahoToolbarButtonProvider() = default;

views::View* MahoToolbarButtonProvider::ResolveTranslateAnchor() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!search_view_) {
    return nullptr;
  }
  views::Label* label = search_view_->translate_label_view();
  if (label && label->GetVisible() && label->IsDrawn()) {
    return label;
  }
  return ResolvePillAnchor();
}

views::View* MahoToolbarButtonProvider::ResolvePillAnchor() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!search_view_) {
    return nullptr;
  }
  views::View* idle = search_view_->idle_button_view();
  if (idle && idle->IsDrawn()) {
    return idle;
  }
  return nullptr;
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
  if (action_id == kActionShowTranslate) {
    if (views::View* anchor = ResolveTranslateAnchor()) {
      return views::BubbleAnchor(anchor);
    }
  }
  if (views::View* pill = ResolvePillAnchor()) {
    return views::BubbleAnchor(pill);
  }
  return LastResortAnchor();
}

views::BubbleAnchor MahoToolbarButtonProvider::GetDefaultExtensionDialogAnchor() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (views::View* pill = ResolvePillAnchor()) {
    return views::BubbleAnchor(pill);
  }
  if (upstream_toolbar_) {
    return upstream_toolbar_->GetDefaultExtensionDialogAnchor();
  }
  return LastResortAnchor();
}

ExtensionsToolbarDesktop* MahoToolbarButtonProvider::GetExtensionsToolbarDesktop() {
  return upstream_toolbar_ ? upstream_toolbar_->GetExtensionsToolbarDesktop()
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

PageActionIconView* MahoToolbarButtonProvider::GetPageActionIconView(
    PageActionIconType type) {
  return upstream_toolbar_ ? upstream_toolbar_->GetPageActionIconView(type)
                           : nullptr;
}

IconLabelBubbleView* MahoToolbarButtonProvider::GetPageActionView(
    actions::ActionId action_id) {
  return upstream_toolbar_ ? upstream_toolbar_->GetPageActionView(action_id)
                           : nullptr;
}

AppMenuControl* MahoToolbarButtonProvider::GetAppMenuControl() {
  return upstream_toolbar_ ? upstream_toolbar_->GetAppMenuControl() : nullptr;
}

gfx::Rect MahoToolbarButtonProvider::GetFindBarBoundingBox(int contents_bottom) {
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

views::AccessiblePaneView* MahoToolbarButtonProvider::GetAsAccessiblePaneView() {
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

ToolbarButton* MahoToolbarButtonProvider::GetBackButton() {
  return upstream_toolbar_ ? upstream_toolbar_->GetBackButton() : nullptr;
}

ReloadControl* MahoToolbarButtonProvider::GetReloadButton() {
  return upstream_toolbar_ ? upstream_toolbar_->GetReloadButton() : nullptr;
}

IntentChipButton* MahoToolbarButtonProvider::GetIntentChipButton() {
  return upstream_toolbar_ ? upstream_toolbar_->GetIntentChipButton() : nullptr;
}

ToolbarButton* MahoToolbarButtonProvider::GetDownloadButton() {
  return upstream_toolbar_ ? upstream_toolbar_->GetDownloadButton() : nullptr;
}

WebUIToolbarWebView*
MahoToolbarButtonProvider::GetWebUIToolbarViewForTesting() {
  return upstream_toolbar_
             ? upstream_toolbar_->GetWebUIToolbarViewForTesting()
             : nullptr;
}

}  // namespace maho
