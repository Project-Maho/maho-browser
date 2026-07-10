// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_NOW_PLAYING_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_NOW_PLAYING_VIEW_H_

#include <string>
#include <memory>
#include <map>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/global_media_controls/public/media_dialog_delegate.h"
#include "base/timer/timer.h"
#include "ui/views/view.h"

class Browser;
class MediaNotificationService;

namespace maho {

class MahoNowPlayingCard;

class MahoSidebarNowPlayingView : public views::View,
                                  public global_media_controls::MediaDialogDelegate,
                                  public TabStripModelObserver {
  METADATA_HEADER(MahoSidebarNowPlayingView, views::View)

 public:
  explicit MahoSidebarNowPlayingView(Browser* browser);
  ~MahoSidebarNowPlayingView() override;

  MahoSidebarNowPlayingView(const MahoSidebarNowPlayingView&) = delete;
  MahoSidebarNowPlayingView& operator=(const MahoSidebarNowPlayingView&) = delete;

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
  void AddedToWidget() override;

  void UpdateVisibilityState();
  void SetupDelegate();

 private:
  void SwapCard(std::unique_ptr<MahoNowPlayingCard> new_card);
  void AnimateShow();
  void AnimateHide();

  const raw_ptr<Browser> browser_;
  raw_ptr<MediaNotificationService> service_ = nullptr;

  std::string current_item_id_;
  raw_ptr<MahoNowPlayingCard> card_ = nullptr;
  base::OneShotTimer show_timer_;
  std::map<std::string, base::WeakPtr<media_message_center::MediaNotificationItem>> active_items_;

  base::WeakPtrFactory<MahoSidebarNowPlayingView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_NOW_PLAYING_VIEW_H_
