// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_now_playing_view.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/profiles/profile.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/global_media_controls/media_notification_service.h"
#include "chrome/browser/ui/global_media_controls/media_notification_service_factory.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "components/global_media_controls/public/media_item_manager.h"
#include "content/public/browser/media_session.h"
#include "maho/browser/ui/views/sidebar/maho_now_playing_card.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"

namespace maho {

MahoSidebarNowPlayingView::MahoSidebarNowPlayingView(Browser* browser)
    : browser_(browser) {
  SetVisible(false);

  SetLayoutManager(std::make_unique<views::FillLayout>());

  service_ = MediaNotificationServiceFactory::GetForProfile(browser_->profile());

  TabStripModel* model = browser_->tab_strip_model();
  if (model) {
    model->AddObserver(this);
  }
}

void MahoSidebarNowPlayingView::AddedToWidget() {
  if (service_) {
    service_->media_item_manager()->SetDialogDelegate(this);
  }
}

void MahoSidebarNowPlayingView::SetupDelegate() {}

MahoSidebarNowPlayingView::~MahoSidebarNowPlayingView() {
  SwapCard(nullptr);
  if (browser_ && browser_->tab_strip_model()) {
    browser_->tab_strip_model()->RemoveObserver(this);
  }
  if (service_) {
    service_->media_item_manager()->SetDialogDelegate(nullptr);
  }
}

global_media_controls::MediaItemUI* MahoSidebarNowPlayingView::ShowMediaItem(
    const std::string& id,
    base::WeakPtr<media_message_center::MediaNotificationItem> item) {
  if (!item || item->GetSourceType() == media_message_center::SourceType::kCast) {
    return nullptr;
  }

  active_items_[id] = item;
  current_item_id_ = id;

  // Destroy the old card before the new card is constructed.
  // This prevents the old card's destructor from setting item's view to nullptr.
  SwapCard(nullptr);

  auto card = std::make_unique<MahoNowPlayingCard>(id, item, browser_);
  auto* card_ptr = card.get();
  SwapCard(std::move(card));

  UpdateVisibilityState();
  return card_ptr;
}

void MahoSidebarNowPlayingView::HideMediaItem(const std::string& id) {
  active_items_.erase(id);

  if (id == current_item_id_) {
    current_item_id_.clear();
    SwapCard(nullptr);

    // Promote the next active item if available, skipping stale null WeakPtrs
    while (!active_items_.empty()) {
      auto next_it = active_items_.begin();
      if (next_it->second) {
        ShowMediaItem(next_it->first, next_it->second);
        break;
      }
      active_items_.erase(next_it);
    }
  }
  UpdateVisibilityState();
}

void MahoSidebarNowPlayingView::RefreshMediaItem(
    const std::string& id,
    base::WeakPtr<media_message_center::MediaNotificationItem> item) {
  if (item) {
    active_items_[id] = item;
  }
  if (id == current_item_id_ && card_ && item) {
    item->SetView(card_);
  }
}

void MahoSidebarNowPlayingView::HideMediaDialog() {
  current_item_id_.clear();
  SwapCard(nullptr);
  AnimateHide();
}

void MahoSidebarNowPlayingView::Focus() {
  if (card_) {
    card_->RequestFocus();
  }
}

gfx::Size MahoSidebarNowPlayingView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  if (!card_ || !GetVisible()) {
    return gfx::Size();
  }
  return card_->GetPreferredSize(available_size);
}

void MahoSidebarNowPlayingView::ChildPreferredSizeChanged(views::View* child) {
  PreferredSizeChanged();
}

void MahoSidebarNowPlayingView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed()) {
    UpdateVisibilityState();
  }
}

void MahoSidebarNowPlayingView::UpdateVisibilityState() {
  if (current_item_id_.empty() || !card_) {
    show_timer_.Stop();
    AnimateHide();
    return;
  }

  PrefService* prefs = browser_->profile()->GetPrefs();
  if (!sidebar_prefs::IsSidebarPanelExpanded(prefs)) {
    show_timer_.Stop();
    AnimateHide();
    return;
  }

  content::WebContents* contents =
      content::MediaSession::GetWebContentsFromRequestId(current_item_id_);
  if (!contents || !browser_) {
    show_timer_.Stop();
    AnimateHide();
    return;
  }

  TabStripModel* model = browser_->tab_strip_model();
  if (model) {
    int index = model->GetIndexOfWebContents(contents);
    if (index == model->active_index()) {
      show_timer_.Stop();
      AnimateHide();
      return;
    }
  }

  if (!GetVisible() && !show_timer_.IsRunning()) {
    show_timer_.Start(FROM_HERE, base::Milliseconds(500),
                      base::BindOnce(&MahoSidebarNowPlayingView::AnimateShow,
                                     base::Unretained(this)));
  }
}

void MahoSidebarNowPlayingView::SwapCard(std::unique_ptr<MahoNowPlayingCard> new_card) {
  if (card_) {
    RemoveChildViewT(card_.get());
  }
  card_ = nullptr;
  if (new_card) {
    card_ = AddChildView(std::move(new_card));
  }
  PreferredSizeChanged();
  InvalidateLayout();
}

void MahoSidebarNowPlayingView::AnimateShow() {
  if (GetVisible()) return;
  SetVisible(true);
  if (parent()) {
    parent()->InvalidateLayout();
    parent()->DeprecatedLayoutImmediately();
  }
  PreferredSizeChanged();
}

void MahoSidebarNowPlayingView::AnimateHide() {
  if (!GetVisible()) return;
  SetVisible(false);
  if (parent()) {
    parent()->InvalidateLayout();
    parent()->DeprecatedLayoutImmediately();
  }
  PreferredSizeChanged();
}

BEGIN_METADATA(MahoSidebarNowPlayingView)
END_METADATA

}  // namespace maho
