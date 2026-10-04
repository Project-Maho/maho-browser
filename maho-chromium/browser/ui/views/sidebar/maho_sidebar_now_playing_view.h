// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_NOW_PLAYING_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_NOW_PLAYING_VIEW_H_

#include <map>
#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "maho/browser/ui/views/sidebar/maho_now_playing_coordinator.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "ui/views/view.h"

class Browser;

namespace maho {

class MahoNowPlayingCard;
class MahoSidebarContainerView;

class MahoSidebarNowPlayingView : public views::View,
                                  public TabStripModelObserver,
                                  public MahoNowPlayingCardHost {
  METADATA_HEADER(MahoSidebarNowPlayingView, views::View)

 public:
  explicit MahoSidebarNowPlayingView(Browser* browser);
  ~MahoSidebarNowPlayingView() override;

  MahoSidebarNowPlayingView(const MahoSidebarNowPlayingView&) = delete;
  MahoSidebarNowPlayingView& operator=(const MahoSidebarNowPlayingView&) = delete;

  // MahoNowPlayingCardHost:
  Browser* GetBrowser() override;
  std::unique_ptr<MahoNowPlayingCard> TakeCard() override;
  void AdoptCard(std::unique_ptr<MahoNowPlayingCard> card,
                 base::WeakPtr<media_message_center::MediaNotificationItem> item,
                 const std::string& id) override;
  void SetTrackedItemState(
      const std::string& id,
      base::WeakPtr<media_message_center::MediaNotificationItem> item) override;
  bool HasCard() const override;
  std::string GetCurrentItemId() const override;
  std::map<std::string, base::WeakPtr<media_message_center::MediaNotificationItem>> TakeActiveItems() override;
  void SetActiveItems(
      std::map<std::string, base::WeakPtr<media_message_center::MediaNotificationItem>> items) override;

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

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void ChildPreferredSizeChanged(views::View* child) override;

  void UpdateVisibilityState();
  void SetupDelegate();
  void SetSidebarPalette(const MahoSidebarPalette& palette);

 private:
  MahoSidebarContainerView* GetContainer();
  void SwapCard(std::unique_ptr<MahoNowPlayingCard> new_card);
  void AnimateShow();
  void AnimateHide();

  const raw_ptr<Browser> browser_;

  std::string current_item_id_;
  raw_ptr<MahoNowPlayingCard> card_ = nullptr;
  base::OneShotTimer show_timer_;
  std::map<std::string, base::WeakPtr<media_message_center::MediaNotificationItem>> active_items_;
  MahoSidebarPalette palette_;

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<MahoSidebarNowPlayingView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_NOW_PLAYING_VIEW_H_
