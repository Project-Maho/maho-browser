// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_NOW_PLAYING_COORDINATOR_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_NOW_PLAYING_COORDINATOR_H_

#include <string>
#include <vector>
#include <map>
#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "chrome/browser/ui/browser_window/public/browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/global_media_controls/public/media_dialog_delegate.h"

class Browser;
class Profile;
class BrowserWindowInterface;

namespace global_media_controls {
class MediaItemManager;
class MediaItemUI;
}  // namespace global_media_controls

namespace media_message_center {
class MediaNotificationItem;
}  // namespace media_message_center

namespace maho {

class MahoNowPlayingCard;

// Interface for a window-local now playing host.
class MahoNowPlayingCardHost {
 public:
  virtual ~MahoNowPlayingCardHost() = default;
  virtual Browser* GetBrowser() = 0;
  virtual global_media_controls::MediaItemUI* ShowMediaItem(
      const std::string& id,
      base::WeakPtr<media_message_center::MediaNotificationItem> item) = 0;
  virtual void HideMediaItem(const std::string& id) = 0;
  virtual void RefreshMediaItem(
      const std::string& id,
      base::WeakPtr<media_message_center::MediaNotificationItem> item) = 0;
  virtual void HideMediaDialog() = 0;
  virtual void Focus() = 0;

  virtual std::unique_ptr<MahoNowPlayingCard> TakeCard() = 0;
  virtual void AdoptCard(std::unique_ptr<MahoNowPlayingCard> card,
                         base::WeakPtr<media_message_center::MediaNotificationItem> item,
                         const std::string& id) = 0;
  virtual void SetTrackedItemState(
      const std::string& id,
      base::WeakPtr<media_message_center::MediaNotificationItem> item) = 0;
  virtual bool HasCard() const = 0;
  virtual std::string GetCurrentItemId() const = 0;
  virtual std::map<std::string, base::WeakPtr<media_message_center::MediaNotificationItem>> TakeActiveItems() = 0;
  virtual void SetActiveItems(
      std::map<std::string, base::WeakPtr<media_message_center::MediaNotificationItem>> items) = 0;
};

class MahoNowPlayingCoordinator : public KeyedService,
                                  public global_media_controls::MediaDialogDelegate,
                                  public BrowserCollectionObserver {
 public:
  MahoNowPlayingCoordinator(Profile* profile, global_media_controls::MediaItemManager* manager);
  ~MahoNowPlayingCoordinator() override;

  MahoNowPlayingCoordinator(const MahoNowPlayingCoordinator&) = delete;
  MahoNowPlayingCoordinator& operator=(const MahoNowPlayingCoordinator&) = delete;

  // KeyedService:
  void Shutdown() override;

  // global_media_controls::MediaDialogDelegate:
  global_media_controls::MediaItemUI* ShowMediaItem(
      const std::string& id,
      base::WeakPtr<media_message_center::MediaNotificationItem> item) override;
  void HideMediaItem(const std::string& id) override;
  void RefreshMediaItem(
      const std::string& id,
      base::WeakPtr<media_message_center::MediaNotificationItem> item) override;
  void HideMediaDialog() override;
  void Focus() override;

  // BrowserCollectionObserver:
  void OnBrowserActivated(BrowserWindowInterface* browser) override;

  // Host Registration:
  void RegisterHost(MahoNowPlayingCardHost* host);
  void UnregisterHost(MahoNowPlayingCardHost* host);

  // Tab move / routing helper
  void OnTabStripModelChanged(Browser* browser);

  const std::vector<raw_ptr<MahoNowPlayingCardHost, VectorExperimental>>& GetHostsForTesting() const {
    return hosts_;
  }

 private:
  MahoNowPlayingCardHost* FindHostForBrowser(Browser* browser);
  MahoNowPlayingCardHost* GetFallbackHost();

  void ReplayActiveItems();

  const raw_ptr<Profile> profile_;
  raw_ptr<global_media_controls::MediaItemManager> manager_;

  std::vector<raw_ptr<MahoNowPlayingCardHost, VectorExperimental>> hosts_;
  raw_ptr<MahoNowPlayingCardHost> last_active_host_ = nullptr;

  // Track items currently playing: item_id -> host that hosts it
  std::map<std::string, raw_ptr<MahoNowPlayingCardHost>> item_to_host_map_;
  std::map<std::string, base::WeakPtr<media_message_center::MediaNotificationItem>> item_id_to_item_weak_map_;

  // Pending items waiting for a host to register: item_id -> Browser*
  std::map<std::string, raw_ptr<Browser>> pending_items_;

  // Pending cards: item_id -> card
  std::map<std::string, std::unique_ptr<MahoNowPlayingCard>> pending_cards_;

  base::ScopedObservation<BrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<MahoNowPlayingCoordinator> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_NOW_PLAYING_COORDINATOR_H_
