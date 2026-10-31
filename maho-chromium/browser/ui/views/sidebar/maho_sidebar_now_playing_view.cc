// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_now_playing_view.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/profiles/profile.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "components/global_media_controls/public/media_item_manager.h"
#include "components/media_message_center/media_notification_item.h"
#include "content/public/browser/media_session.h"
#include "maho/browser/ui/views/sidebar/maho_now_playing_card.h"
#include "maho/browser/ui/views/sidebar/maho_now_playing_coordinator_factory.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"

namespace maho {

MahoSidebarNowPlayingView::MahoSidebarNowPlayingView(Browser* browser)
    : browser_(browser) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SetVisible(false);

  SetLayoutManager(std::make_unique<views::FillLayout>());

  TabStripModel* model = browser_->GetTabStripModel();
  if (model) {
    model->AddObserver(this);
  }
}

void MahoSidebarNowPlayingView::SetupDelegate() {}

void MahoSidebarNowPlayingView::SetSidebarPalette(
    const MahoSidebarPalette& palette) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  palette_ = palette;
  if (card_) {
    card_->SetSidebarPalette(palette_);
  }
}

MahoSidebarNowPlayingView::~MahoSidebarNowPlayingView() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SwapCard(nullptr);
  if (browser_ && browser_->GetTabStripModel()) {
    browser_->GetTabStripModel()->RemoveObserver(this);
  }
  auto* coordinator = MahoNowPlayingCoordinatorFactory::GetForProfileIfExists(browser_->GetProfile());
  if (coordinator) {
    coordinator->UnregisterHost(this);
  }
}

global_media_controls::MediaItemUI* MahoSidebarNowPlayingView::ShowMediaItem(
    const std::string& id,
    base::WeakPtr<media_message_center::MediaNotificationItem> item) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!item || item->GetSourceType() == media_message_center::SourceType::kCast) {
    return nullptr;
  }

  active_items_[id] = item;
  current_item_id_ = id;

  // Destroy the old card before the new card is constructed.
  // This prevents the old card's destructor from setting item's view to nullptr.
  SwapCard(nullptr);

  auto card = std::make_unique<MahoNowPlayingCard>(id, item, browser_);
  card->SetSidebarPalette(palette_);
  auto* card_ptr = card.get();
  SwapCard(std::move(card));

  UpdateVisibilityState();
  return card_ptr;
}

void MahoSidebarNowPlayingView::HideMediaItem(const std::string& id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
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
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (item) {
    active_items_[id] = item;
  }
  if (id == current_item_id_ && card_ && item) {
    item->SetView(card_);
  }
}

void MahoSidebarNowPlayingView::HideMediaDialog() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  current_item_id_.clear();
  SwapCard(nullptr);
  AnimateHide();
}

void MahoSidebarNowPlayingView::Focus() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
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
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (selection.active_tab_changed()) {
    UpdateVisibilityState();
  }
  auto* coordinator = MahoNowPlayingCoordinatorFactory::GetForProfileIfExists(browser_->GetProfile());
  if (coordinator) {
    coordinator->OnTabStripModelChanged(browser_);
  }
}

void MahoSidebarNowPlayingView::UpdateVisibilityState() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (current_item_id_.empty() || !card_) {
    show_timer_.Stop();
    AnimateHide();
    return;
  }

  bool expanded = true;
  if (auto* container = GetContainer()) {
    expanded = container->IsPanelExpanded();
  } else {
    expanded = sidebar_prefs::IsSidebarPanelExpanded(browser_->GetProfile()->GetPrefs());
  }
  if (!expanded) {
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

  TabStripModel* model = browser_->GetTabStripModel();
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
                                     weak_factory_.GetWeakPtr()));
  }
}

void MahoSidebarNowPlayingView::SwapCard(std::unique_ptr<MahoNowPlayingCard> new_card) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (card_) {
    auto* card_ptr = card_.get();
    card_ = nullptr;
    RemoveChildViewT(card_ptr);
  }
  if (new_card) {
    new_card->SetSidebarPalette(palette_);
    card_ = AddChildView(std::move(new_card));
  }
  PreferredSizeChanged();
  InvalidateLayout();
}

void MahoSidebarNowPlayingView::AnimateShow() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (GetVisible()) return;
  SetVisible(true);
  if (parent()) {
    parent()->InvalidateLayout();
    parent()->DeprecatedLayoutImmediately();
  }
  PreferredSizeChanged();
}

void MahoSidebarNowPlayingView::AnimateHide() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!GetVisible()) return;
  SetVisible(false);
  if (parent()) {
    parent()->InvalidateLayout();
    parent()->DeprecatedLayoutImmediately();
  }
  PreferredSizeChanged();
}

Browser* MahoSidebarNowPlayingView::GetBrowser() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return browser_;
}

std::unique_ptr<MahoNowPlayingCard> MahoSidebarNowPlayingView::TakeCard() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!card_) {
    return nullptr;
  }
  auto* card_ptr = card_.get();
  card_ = nullptr;
  auto taken_card = RemoveChildViewT(card_ptr);
  PreferredSizeChanged();
  InvalidateLayout();
  return taken_card;
}

void MahoSidebarNowPlayingView::AdoptCard(std::unique_ptr<MahoNowPlayingCard> card,
                                          base::WeakPtr<media_message_center::MediaNotificationItem> item,
                                          const std::string& id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  current_item_id_ = id;
  active_items_[id] = item;
  show_timer_.Stop();
  SwapCard(std::move(card));
  if (item && card_) {
    item->SetView(card_);
  }
  UpdateVisibilityState();
}

void MahoSidebarNowPlayingView::SetTrackedItemState(
    const std::string& id,
    base::WeakPtr<media_message_center::MediaNotificationItem> item) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  current_item_id_ = id;
  active_items_[id] = item;
}

bool MahoSidebarNowPlayingView::HasCard() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return card_ != nullptr;
}

std::string MahoSidebarNowPlayingView::GetCurrentItemId() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return current_item_id_;
}

std::map<std::string, base::WeakPtr<media_message_center::MediaNotificationItem>> MahoSidebarNowPlayingView::TakeActiveItems() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return std::move(active_items_);
}

void MahoSidebarNowPlayingView::SetActiveItems(
    std::map<std::string, base::WeakPtr<media_message_center::MediaNotificationItem>> items) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  active_items_ = std::move(items);
}

MahoSidebarContainerView* MahoSidebarNowPlayingView::GetContainer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (views::View* v = parent(); v; v = v->parent()) {
    if (auto* container = views::AsViewClass<MahoSidebarContainerView>(v)) {
      return container;
    }
  }
  return nullptr;
}

BEGIN_METADATA(MahoSidebarNowPlayingView)
END_METADATA

}  // namespace maho
