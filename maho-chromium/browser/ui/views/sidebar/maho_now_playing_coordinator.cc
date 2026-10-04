// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_now_playing_coordinator.h"

#include <algorithm>

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/browser_process.h"
#include "components/global_media_controls/public/media_item_manager.h"
#include "content/public/browser/media_session.h"
#include "components/media_message_center/media_notification_item.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "maho/browser/ui/views/sidebar/maho_now_playing_card.h"

namespace maho {

MahoNowPlayingCoordinator::MahoNowPlayingCoordinator(Profile* profile, global_media_controls::MediaItemManager* manager)
    : profile_(profile), manager_(manager) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (g_browser_process && g_browser_process->GetFeatures()) {
    auto* collection = GlobalBrowserCollection::GetInstance();
    if (collection) {
      browser_collection_observation_.Observe(collection);
    }
  }
}

MahoNowPlayingCoordinator::~MahoNowPlayingCoordinator() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Shutdown();
}

void MahoNowPlayingCoordinator::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  browser_collection_observation_.Reset();
  if (manager_) {
    manager_->SetDialogDelegate(nullptr);
    manager_ = nullptr;
  }
  hosts_.clear();
  item_to_host_map_.clear();
  item_id_to_item_weak_map_.clear();
  pending_items_.clear();
  pending_cards_.clear();
}

global_media_controls::MediaItemUI* MahoNowPlayingCoordinator::ShowMediaItem(
    const std::string& id,
    base::WeakPtr<media_message_center::MediaNotificationItem> item) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!item || item->GetSourceType() == media_message_center::SourceType::kCast) {
    return nullptr;
  }

  item_id_to_item_weak_map_[id] = item;

  content::WebContents* contents =
      content::MediaSession::GetWebContentsFromRequestId(id);
  
  MahoNowPlayingCardHost* target_host = nullptr;
  
  if (contents) {
    Browser* browser = static_cast<Browser*>(
        GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(contents));
    if (browser && browser->GetProfile() == profile_) {
      target_host = FindHostForBrowser(browser);
      if (!target_host) {
        // Delayed visible host
        pending_items_[id] = browser;
        return nullptr;
      }
    } else {
      // Cross-profile or no browser, reject
      return nullptr;
    }
  } else {
    // Non-WebContents: route to fallback (last active host)
    target_host = GetFallbackHost();
    if (!target_host) {
      return nullptr;
    }
  }

  // Route to the host
  item_to_host_map_[id] = target_host;
  pending_items_.erase(id);
  
  return target_host->ShowMediaItem(id, item);
}

void MahoNowPlayingCoordinator::HideMediaItem(const std::string& id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  item_id_to_item_weak_map_.erase(id);
  pending_items_.erase(id);
  pending_cards_.erase(id);

  auto it = item_to_host_map_.find(id);
  if (it != item_to_host_map_.end()) {
    MahoNowPlayingCardHost* host = it->second;
    item_to_host_map_.erase(it);
    if (host) {
      host->HideMediaItem(id);
    }
  }
}

void MahoNowPlayingCoordinator::RefreshMediaItem(
    const std::string& id,
    base::WeakPtr<media_message_center::MediaNotificationItem> item) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!item) return;

  item_id_to_item_weak_map_[id] = item;

  // Let's re-resolve the routing in case WebContents browser has changed
  content::WebContents* contents =
      content::MediaSession::GetWebContentsFromRequestId(id);
  
  MahoNowPlayingCardHost* target_host = nullptr;
  if (contents) {
    Browser* browser = static_cast<Browser*>(
        GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(contents));
    if (browser && browser->GetProfile() == profile_) {
      target_host = FindHostForBrowser(browser);
    }
  } else {
    target_host = GetFallbackHost();
  }

  if (!target_host) {
    return;
  }

  auto it = item_to_host_map_.find(id);
  if (it != item_to_host_map_.end() && it->second != target_host) {
    // Host changed! Reparent the card.
    MahoNowPlayingCardHost* old_host = it->second;
    item_to_host_map_[id] = target_host;

    if (old_host && old_host->GetCurrentItemId() == id) {
      auto card = old_host->TakeCard();
      if (card) {
        auto old_items = old_host->TakeActiveItems();
        old_items.erase(id);
        old_host->SetActiveItems(old_items);

        auto new_items = target_host->TakeActiveItems();
        new_items[id] = item;
        target_host->SetActiveItems(new_items);

        target_host->AdoptCard(std::move(card), item, id);
        old_host->HideMediaItem(id);
        return;
      }
    }
  }

  item_to_host_map_[id] = target_host;
  target_host->RefreshMediaItem(id, item);
}

void MahoNowPlayingCoordinator::HideMediaDialog() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (MahoNowPlayingCardHost* host : hosts_) {
    host->HideMediaDialog();
  }
  item_to_host_map_.clear();
  pending_items_.clear();
  pending_cards_.clear();
}

void MahoNowPlayingCoordinator::Focus() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (last_active_host_ && last_active_host_->HasCard()) {
    last_active_host_->Focus();
    return;
  }
  for (MahoNowPlayingCardHost* host : hosts_) {
    if (host->HasCard()) {
      host->Focus();
      return;
    }
  }
  if (!item_to_host_map_.empty()) {
    auto last_routed_host = item_to_host_map_.rbegin()->second;
    if (last_routed_host) {
      last_routed_host->Focus();
      return;
    }
  }
}

void MahoNowPlayingCoordinator::OnBrowserActivated(BrowserWindowInterface* browser) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (browser && browser->GetProfile() == profile_) {
    MahoNowPlayingCardHost* host = FindHostForBrowser(static_cast<Browser*>(browser));
    if (host) {
      last_active_host_ = host;
    }
  }
}

void MahoNowPlayingCoordinator::RegisterHost(MahoNowPlayingCardHost* host) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (std::find(hosts_.begin(), hosts_.end(), host) == hosts_.end()) {
    hosts_.push_back(host);
  }
  if (hosts_.size() == 1 && manager_) {
    manager_->SetDialogDelegate(this);
  }
  
  Browser* browser = host->GetBrowser();
  if (browser) {
    // 1. Pending cards
    std::vector<std::string> cards_to_adopt;
    for (const auto& pair : pending_cards_) {
      if (pending_items_[pair.first] == browser) {
        cards_to_adopt.push_back(pair.first);
      }
    }
    for (const std::string& id : cards_to_adopt) {
      auto card_it = pending_cards_.find(id);
      if (card_it != pending_cards_.end()) {
        auto card = std::move(card_it->second);
        pending_cards_.erase(card_it);
        auto item_weak = item_id_to_item_weak_map_[id];
        item_to_host_map_[id] = host;
        
        auto host_items = host->TakeActiveItems();
        host_items[id] = item_weak;
        host->SetActiveItems(host_items);
        
        host->AdoptCard(std::move(card), item_weak, id);
        pending_items_.erase(id);
      }
    }
    
    // 2. Pending items
    std::vector<std::string> items_to_replay;
    for (const auto& pair : pending_items_) {
      if (pair.second == browser) {
        items_to_replay.push_back(pair.first);
      }
    }
    if (!items_to_replay.empty()) {
      ReplayActiveItems();
    }
  }
}

void MahoNowPlayingCoordinator::UnregisterHost(MahoNowPlayingCardHost* host) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = std::find(hosts_.begin(), hosts_.end(), host);
  if (it != hosts_.end()) {
    hosts_.erase(it);
  }
  
  if (last_active_host_ == host) {
    last_active_host_ = nullptr;
  }
  
  for (auto map_it = item_to_host_map_.begin(); map_it != item_to_host_map_.end();) {
    if (map_it->second == host) {
      map_it = item_to_host_map_.erase(map_it);
    } else {
      ++map_it;
    }
  }
  
  if (hosts_.empty() && manager_) {
    manager_->SetDialogDelegate(nullptr);
  }
}

void MahoNowPlayingCoordinator::OnTabStripModelChanged(Browser* browser) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (browser->GetProfile() != profile_) return;
  
  MahoNowPlayingCardHost* host = FindHostForBrowser(browser);
  if (!host) return;

  std::vector<std::string> items_to_recheck;
  for (const auto& pair : item_to_host_map_) {
    if (pair.second == host) {
      items_to_recheck.push_back(pair.first);
    }
  }

  for (const std::string& id : items_to_recheck) {
    content::WebContents* contents =
        content::MediaSession::GetWebContentsFromRequestId(id);
    if (!contents) continue;

    Browser* current_browser = static_cast<Browser*>(
        GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(contents));
    if (current_browser && current_browser != browser && current_browser->GetProfile() == profile_) {
      MahoNowPlayingCardHost* new_host = FindHostForBrowser(current_browser);
      auto item_weak = item_id_to_item_weak_map_[id];
      if (new_host) {
        item_to_host_map_[id] = new_host;

        if (host->GetCurrentItemId() == id) {
          auto card = host->TakeCard();
          if (card) {
            auto old_items = host->TakeActiveItems();
            old_items.erase(id);
            host->SetActiveItems(old_items);

            auto new_items = new_host->TakeActiveItems();
            new_items[id] = item_weak;
            new_host->SetActiveItems(new_items);

            new_host->AdoptCard(std::move(card), item_weak, id);
            host->HideMediaItem(id);
          }
        } else {
          auto old_items = host->TakeActiveItems();
          old_items.erase(id);
          host->SetActiveItems(old_items);

          auto new_items = new_host->TakeActiveItems();
          new_items[id] = item_weak;
          new_host->SetActiveItems(new_items);
        }
      } else {
        pending_items_[id] = current_browser;
        item_to_host_map_.erase(id);

        if (host->GetCurrentItemId() == id) {
          auto card = host->TakeCard();
          if (card) {
            auto old_items = host->TakeActiveItems();
            old_items.erase(id);
            host->SetActiveItems(old_items);

            pending_cards_[id] = std::move(card);
            host->HideMediaItem(id);
          }
        } else {
          auto old_items = host->TakeActiveItems();
          old_items.erase(id);
          host->SetActiveItems(old_items);
        }
      }
    }
  }
}

void MahoNowPlayingCoordinator::ReplayActiveItems() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<MahoNowPlayingCoordinator> self) {
            if (!self || !self->manager_) return;
            self->manager_->SetDialogDelegate(nullptr);
            self->manager_->SetDialogDelegate(self.get());
          },
          weak_factory_.GetWeakPtr()));
}

MahoNowPlayingCardHost* MahoNowPlayingCoordinator::FindHostForBrowser(Browser* browser) {
  for (MahoNowPlayingCardHost* host : hosts_) {
    if (host->GetBrowser() == browser) {
      return host;
    }
  }
  return nullptr;
}

MahoNowPlayingCardHost* MahoNowPlayingCoordinator::GetFallbackHost() {
  if (last_active_host_) {
    return last_active_host_;
  }
  if (!hosts_.empty()) {
    return hosts_.back();
  }
  return nullptr;
}

}  // namespace maho
