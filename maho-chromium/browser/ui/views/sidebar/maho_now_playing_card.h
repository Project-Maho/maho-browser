// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_NOW_PLAYING_CARD_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_NOW_PLAYING_CARD_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "components/global_media_controls/public/media_item_ui.h"
#include "components/media_message_center/media_notification_view.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "services/media_session/public/mojom/media_session.mojom.h"
#include "ui/views/view.h"

class Browser;

namespace views {
class Label;
class ImageButton;
class View;
} // namespace views

namespace media_message_center {
class MediaNotificationItem;
} // namespace media_message_center

namespace maho {

class NotePulseOverlay;
class PaletteProgressView;

class MahoNowPlayingCard : public media_message_center::MediaNotificationView,
                           public global_media_controls::MediaItemUI {
  METADATA_HEADER(MahoNowPlayingCard,
                  media_message_center::MediaNotificationView)

public:
  MahoNowPlayingCard(
      const std::string &item_id,
      base::WeakPtr<media_message_center::MediaNotificationItem> item,
      Browser *browser);
  ~MahoNowPlayingCard() override;

  MahoNowPlayingCard(const MahoNowPlayingCard &) = delete;
  MahoNowPlayingCard &operator=(const MahoNowPlayingCard &) = delete;

  // media_message_center::MediaNotificationView:
  void SetForcedExpandedState(bool *forced_expanded_state) override;
  void SetExpanded(bool expanded) override;
  void UpdateCornerRadius(int top_radius, int bottom_radius) override;
  void UpdateWithMediaSessionInfo(
      const media_session::mojom::MediaSessionInfoPtr &session_info) override;
  void UpdateWithMediaMetadata(
      const media_session::MediaMetadata &metadata) override;
  void UpdateWithMediaActions(
      const base::flat_set<media_session::mojom::MediaSessionAction> &actions)
      override;
  void UpdateWithMediaPosition(
      const media_session::MediaPosition &position) override;
  void UpdateWithMediaArtwork(const gfx::ImageSkia &image) override;
  void UpdateWithChapterArtwork(int index,
                                const gfx::ImageSkia &image) override;
  void UpdateWithFavicon(const gfx::ImageSkia &icon) override;
  void UpdateWithVectorIcon(const gfx::VectorIcon *vector_icon) override;
  void UpdateWithMuteStatus(bool mute) override;
  void UpdateWithVolume(float volume) override;
  void UpdateDeviceSelectorVisibility(bool visible) override;
  void UpdateDeviceSelectorAvailability(bool has_devices) override;
  void SetSidebarPalette(const MahoSidebarPalette &palette);

  // global_media_controls::MediaItemUI:
  void
  AddObserver(global_media_controls::MediaItemUIObserver *observer) override;
  void
  RemoveObserver(global_media_controls::MediaItemUIObserver *observer) override;

  // views::View:
  void OnMouseEntered(const ui::MouseEvent &event) override;
  void OnMouseExited(const ui::MouseEvent &event) override;

private:
  void BuildLayout();
  void UpdateExpandedState();
  void UpdateFaviconFromWebContents();
  std::u16string FormatPosition(base::TimeDelta time) const;
  void UpdateMuteButton();

  void OnSeek(double seek_value);

  // Button actions
  void OnActionPressed(media_session::mojom::MediaSessionAction action);
  void OnClosePressed();
  void OnFaviconPressed();
  void OnMutePressed();
  void OnPlayPausePressed();

  const std::string item_id_;
  base::WeakPtr<media_message_center::MediaNotificationItem> item_;
  const raw_ptr<Browser> browser_;
  base::ObserverList<global_media_controls::MediaItemUIObserver> observers_;

  base::TimeDelta duration_;
  base::TimeDelta current_position_;
  bool is_muted_ = false;
  bool is_hovered_ = false;

  // UI Components
  raw_ptr<views::View> title_row_ = nullptr;
  raw_ptr<views::View> progress_row_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> artist_label_ = nullptr;
  raw_ptr<views::Label> current_timestamp_label_ = nullptr;
  raw_ptr<views::Label> duration_label_ = nullptr;
  raw_ptr<views::ImageButton> close_button_ = nullptr;
  raw_ptr<PaletteProgressView> progress_view_ = nullptr;
  raw_ptr<views::ImageButton> favicon_view_ = nullptr;
  raw_ptr<views::ImageButton> prev_button_ = nullptr;
  raw_ptr<views::ImageButton> play_pause_button_ = nullptr;
  raw_ptr<views::ImageButton> next_button_ = nullptr;
  raw_ptr<views::ImageButton> mute_button_ = nullptr;
  raw_ptr<NotePulseOverlay> note_pulse_ = nullptr;
  MahoSidebarPalette palette_;
};

} // namespace maho

#endif // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_NOW_PLAYING_CARD_H_
