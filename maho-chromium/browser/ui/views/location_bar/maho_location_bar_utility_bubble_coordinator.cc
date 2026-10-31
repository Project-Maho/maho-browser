// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_bubble_coordinator.h"

#include <memory>
#include <utility>

#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.h"
#include "ui/views/widget/widget.h"

namespace maho {

MahoLocationBarUtilityBubbleCoordinator::
    MahoLocationBarUtilityBubbleCoordinator() = default;

MahoLocationBarUtilityBubbleCoordinator::
    ~MahoLocationBarUtilityBubbleCoordinator() {
  DetachFromBrowser();
  if (bubble_) {
    widget_observation_.Reset();
    views::Widget* widget = bubble_->GetWidget();
    bubble_ = nullptr;
    if (widget && !widget->IsClosed()) {
      widget->CloseNow();
    }
  }
}

void MahoLocationBarUtilityBubbleCoordinator::AttachToBrowser(
    Browser* browser) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!browser) {
    return;
  }
  TabStripModel* tab_strip = browser->GetTabStripModel();
  if (!tab_strip) {
    return;
  }
  if (observed_tab_strip_ == tab_strip) {
    return;
  }
  DetachFromBrowser();
  observed_tab_strip_ = tab_strip;
  tab_strip->AddObserver(this);
}

void MahoLocationBarUtilityBubbleCoordinator::DetachFromBrowser() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (observed_tab_strip_) {
    observed_tab_strip_->RemoveObserver(this);
    observed_tab_strip_ = nullptr;
  }
}

void MahoLocationBarUtilityBubbleCoordinator::ShowBubble(
    views::View* anchor_view,
    MahoLocationBarUtilityPanelModel model) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (IsShowing()) {
    return;
  }

  if (!anchor_view) {
    return;
  }

  views::Widget* widget =
      MahoLocationBarUtilityPanelView::Show(anchor_view, std::move(model));
  if (!widget) {
    return;
  }

  widget_observation_.Observe(widget);
  CHECK(widget->widget_delegate());
  bubble_ = static_cast<MahoLocationBarUtilityPanelView*>(
      widget->widget_delegate());
}

void MahoLocationBarUtilityBubbleCoordinator::Hide() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsShowing()) {
    return;
  }
  views::Widget* widget = bubble_->GetWidget();
  if (widget && !widget->IsClosed()) {
    widget->Close();
  }
}

bool MahoLocationBarUtilityBubbleCoordinator::IsShowing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return bubble_ != nullptr;
}

MahoLocationBarUtilityPanelView*
MahoLocationBarUtilityBubbleCoordinator::GetBubble() const {
  return bubble_;
}

base::CallbackListSubscription
MahoLocationBarUtilityBubbleCoordinator::RegisterOnCloseCallback(
    base::RepeatingClosure callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return on_close_callbacks_.Add(std::move(callback));
}

void MahoLocationBarUtilityBubbleCoordinator::OnWidgetDestroying(
    views::Widget* widget) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!bubble_) {
    return;
  }
  widget_observation_.Reset();
  bubble_ = nullptr;
  on_close_callbacks_.Notify();
}

void MahoLocationBarUtilityBubbleCoordinator::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsShowing()) {
    return;
  }
  if (selection.active_tab_changed()) {
    Hide();
  }
}

void MahoLocationBarUtilityBubbleCoordinator::OnTabStripModelDestroyed(
    TabStripModel* tab_strip_model) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (IsShowing()) {
    Hide();
  }
  if (tab_strip_model) {
    tab_strip_model->RemoveObserver(this);
  }
  observed_tab_strip_ = nullptr;
}

}  // namespace maho
