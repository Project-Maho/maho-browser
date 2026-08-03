// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_now_playing_card.h"

#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_muted_utils.h"
#include "components/global_media_controls/public/views/media_action_button.h"
#include "components/global_media_controls/public/views/media_progress_view.h"
#include "components/global_media_controls/public/media_item_ui_observer.h"
#include "components/global_media_controls/public/media_session_notification_item.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/color/color_id.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"

namespace maho {

class NotePulseOverlay : public views::View, public gfx::AnimationDelegate {
  METADATA_HEADER(NotePulseOverlay, views::View)

 public:
  NotePulseOverlay() : animation_(this) {
    animation_.SetDuration(base::Milliseconds(2400));
    SetCanProcessEventsWithinSubtree(false);
  }
  ~NotePulseOverlay() override = default;

  void SetActive(bool active) {
    if (active == active_) return;
    active_ = active;
    if (active_) {
      animation_.Start();
    } else {
      animation_.Stop();
    }
    SchedulePaint();
  }

  void OnPaint(gfx::Canvas* canvas) override {
    if (!active_) return;
    const gfx::Rect bounds = GetContentsBounds();
    const int center_x = bounds.width() / 2;
    const double t0 = animation_.GetCurrentValue();
    const gfx::FontList font_list = gfx::FontList().DeriveWithHeightUpperBound(12);
    for (int i = 0; i < 3; i++) {
      double phase = t0 + (i / 3.0);
      if (phase > 1.0) phase -= 1.0;
      const float y_start = static_cast<float>(bounds.height() - 4);
      const float y_end = -8.0f;
      const float y = y_start + (y_end - y_start) * static_cast<float>(phase);
      const float alpha_f = std::sin(phase * 3.14159f);
      const uint8_t alpha =
          static_cast<uint8_t>(std::clamp(alpha_f, 0.0f, 1.0f) * 200.0f);
      if (alpha < 8) continue;
      const int x_offset = ((i % 3) - 1) * 4;
      SkColor color = SkColorSetA(SK_ColorWHITE, alpha);
      canvas->DrawStringRectWithFlags(
          u"\u266A",
          font_list,
          color,
          gfx::Rect(center_x + x_offset - 6, static_cast<int>(y), 12, 14),
          gfx::Canvas::TEXT_ALIGN_CENTER);
    }
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(20, 20);
  }

  void AnimationProgressed(const gfx::Animation* animation) override {
    SchedulePaint();
  }
  void AnimationEnded(const gfx::Animation* animation) override {
    if (active_) animation_.Start();
  }

 private:
  gfx::LinearAnimation animation_;
  bool active_ = false;
};

class GlassHoverImageButton : public views::ImageButton {
  METADATA_HEADER(GlassHoverImageButton, views::ImageButton)

 public:
  explicit GlassHoverImageButton(PressedCallback callback)
      : views::ImageButton(std::move(callback)) {}
  ~GlassHoverImageButton() override = default;

  void StateChanged(ButtonState old_state) override {
    views::ImageButton::StateChanged(old_state);
    const ButtonState state = GetState();
    if (state == STATE_HOVERED || state == STATE_PRESSED) {
      SetBackground(views::CreateRoundedRectBackground(
          kMahoColorTabRowHoverBackground, 6));
    } else {
      SetBackground(nullptr);
    }
  }
};

MahoNowPlayingCard::MahoNowPlayingCard(
    const std::string& item_id,
    base::WeakPtr<media_message_center::MediaNotificationItem> item,
    Browser* browser)
    : item_id_(item_id), item_(item), browser_(browser) {
  BuildLayout();
  if (item_) {
    item_->SetView(this);
  }
}

MahoNowPlayingCard::~MahoNowPlayingCard() {
  for (auto& observer : observers_) {
    observer.OnMediaItemUIDestroyed(item_id_);
  }
  if (item_) {
    item_->SetView(nullptr);
  }
}

void MahoNowPlayingCard::SetForcedExpandedState(bool* forced_expanded_state) {}
void MahoNowPlayingCard::SetExpanded(bool expanded) {}
void MahoNowPlayingCard::UpdateCornerRadius(int top_radius, int bottom_radius) {}

void MahoNowPlayingCard::UpdateWithMediaSessionInfo(
    const media_session::mojom::MediaSessionInfoPtr& session_info) {
  if (!session_info) return;
  bool is_playing = (session_info->playback_state ==
                     media_session::mojom::MediaPlaybackState::kPlaying);
  if (note_pulse_) {
    note_pulse_->SetActive(is_playing);
  }
  if (play_pause_button_) {
    if (is_playing) {
      play_pause_button_->Update(
          static_cast<int>(media_session::mojom::MediaSessionAction::kPause),
          maho_lucide_icons::kPauseIcon,
          IDS_MEDIA_MESSAGE_CENTER_MEDIA_NOTIFICATION_ACTION_PAUSE,
          kMahoColorSidebarPrimaryText);
    } else {
      play_pause_button_->Update(
          static_cast<int>(media_session::mojom::MediaSessionAction::kPlay),
          maho_lucide_icons::kPlayIcon,
          IDS_MEDIA_MESSAGE_CENTER_MEDIA_NOTIFICATION_ACTION_PLAY,
          kMahoColorSidebarPrimaryText);
    }
  }
}

void MahoNowPlayingCard::UpdateWithMediaMetadata(
    const media_session::MediaMetadata& metadata) {
  if (title_label_) {
    title_label_->SetText(metadata.title);
  }
  if (artist_label_) {
    artist_label_->SetText(metadata.artist.empty() ? metadata.source_title : metadata.artist);
  }
  UpdateFaviconFromWebContents();
  for (auto& observer : observers_) {
    observer.OnMediaItemUIMetadataChanged();
  }
}

void MahoNowPlayingCard::UpdateWithMediaActions(
    const base::flat_set<media_session::mojom::MediaSessionAction>& actions) {
  prev_button_->SetVisible(actions.contains(media_session::mojom::MediaSessionAction::kPreviousTrack));
  next_button_->SetVisible(actions.contains(media_session::mojom::MediaSessionAction::kNextTrack));
  play_pause_button_->SetVisible(
      actions.contains(media_session::mojom::MediaSessionAction::kPlay) ||
      actions.contains(media_session::mojom::MediaSessionAction::kPause));
  for (auto& observer : observers_) {
    observer.OnMediaItemUIActionsChanged();
  }
}

void MahoNowPlayingCard::UpdateWithMediaPosition(
    const media_session::MediaPosition& position) {
  duration_ = position.duration();
  current_position_ = position.GetPosition();
  duration_label_->SetText(FormatPosition(duration_));
  current_timestamp_label_->SetText(FormatPosition(current_position_));
  if (progress_view_) {
    progress_view_->UpdateProgress(position);
  }
}

void MahoNowPlayingCard::UpdateWithMediaArtwork(const gfx::ImageSkia& image) {}
void MahoNowPlayingCard::UpdateWithChapterArtwork(int index, const gfx::ImageSkia& image) {}

void MahoNowPlayingCard::UpdateWithFavicon(const gfx::ImageSkia& icon) {
  if (favicon_view_ && !icon.isNull()) {
    favicon_view_->SetImageModel(views::Button::STATE_NORMAL,
                                 ui::ImageModel::FromImageSkia(icon));
    return;
  }
  UpdateFaviconFromWebContents();
}

void MahoNowPlayingCard::UpdateFaviconFromWebContents() {
  if (!favicon_view_) {
    return;
  }
  content::WebContents* contents =
      content::MediaSession::GetWebContentsFromRequestId(item_id_);
  if (!contents) {
    return;
  }
  content::NavigationEntry* entry =
      contents->GetController().GetLastCommittedEntry();
  if (!entry) {
    return;
  }
  const content::FaviconStatus& favicon = entry->GetFavicon();
  if (favicon.valid && !favicon.image.IsEmpty()) {
    favicon_view_->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromImageSkia(favicon.image.AsImageSkia()));
  }
}

void MahoNowPlayingCard::UpdateWithVectorIcon(const gfx::VectorIcon* vector_icon) {}

void MahoNowPlayingCard::UpdateWithMuteStatus(bool mute) {
  is_muted_ = mute;
  UpdateMuteButton();
}

void MahoNowPlayingCard::UpdateWithVolume(float volume) {}
void MahoNowPlayingCard::UpdateDeviceSelectorVisibility(bool visible) {}
void MahoNowPlayingCard::UpdateDeviceSelectorAvailability(bool has_devices) {}

void MahoNowPlayingCard::SetSidebarPalette(const MahoSidebarPalette& palette) {
  palette_ = palette;
  const SkColor surface = palette_.normal_contrast_surfaces.empty()
                              ? palette_.row_selected
                              : palette_.normal_contrast_surfaces.front();
  SetBackground(views::CreateRoundedRectBackground(surface, 12));
  for (views::Label* label : {title_label_.get(), artist_label_.get(),
                              current_timestamp_label_.get(),
                              duration_label_.get()}) {
    if (label) {
      label->SetEnabledColor(label == title_label_ ? palette_.primary_text
                                                   : palette_.secondary_text);
    }
  }
  UpdateMuteButton();
  SchedulePaint();
}

void MahoNowPlayingCard::AddObserver(
    global_media_controls::MediaItemUIObserver* observer) {
  observers_.AddObserver(observer);
}

void MahoNowPlayingCard::RemoveObserver(
    global_media_controls::MediaItemUIObserver* observer) {
  observers_.RemoveObserver(observer);
}

void MahoNowPlayingCard::OnMouseEntered(const ui::MouseEvent& event) {
  is_hovered_ = true;
  UpdateExpandedState();
}

void MahoNowPlayingCard::OnMouseExited(const ui::MouseEvent& event) {
  is_hovered_ = false;
  UpdateExpandedState();
}

void MahoNowPlayingCard::UpdateExpandedState() {
  const bool expanded = is_hovered_;
  if (title_row_) title_row_->SetVisible(expanded);
  if (artist_label_) artist_label_->SetVisible(expanded);
  if (progress_row_) progress_row_->SetVisible(expanded);
  PreferredSizeChanged();
  InvalidateLayout();
}

void MahoNowPlayingCard::BuildLayout() {
  SetNotifyEnterExitOnChild(true);
  SetBackground(views::CreateRoundedRectBackground(
      kMahoColorCardBackground, 12));

  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(8, 10), 6));

  title_row_ = AddChildView(std::make_unique<views::View>());
  auto* layout1 = title_row_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
  layout1->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  title_label_ = title_row_->AddChildView(std::make_unique<views::Label>(
      u"", views::style::CONTEXT_LABEL, views::style::STYLE_PRIMARY));
  title_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_label_->SetSkipSubpixelRenderingOpacityCheck(true);
  layout1->SetFlexForView(title_label_, 1);

  close_button_ = title_row_->AddChildView(views::CreateVectorImageButtonWithNativeTheme(
       base::BindRepeating(&MahoNowPlayingCard::OnClosePressed,
                           base::Unretained(this)),
       maho_lucide_icons::kXIcon, 12, kMahoColorSidebarSecondaryText,
       kMahoColorSidebarPrimaryText, kMahoColorSidebarPrimaryText));
  close_button_->SetBorder(nullptr);
  close_button_->SetTooltipText(u"Dismiss");
  close_button_->SetAccessibleName(u"Dismiss Now Playing");
  views::InstallCircleHighlightPathGenerator(close_button_);

  artist_label_ = AddChildView(std::make_unique<views::Label>(
      u"", views::style::CONTEXT_LABEL, views::style::STYLE_SECONDARY));
  artist_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  artist_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  artist_label_->SetEnabledColor(kMahoColorSidebarSecondaryText);
  artist_label_->SetSkipSubpixelRenderingOpacityCheck(true);

  progress_row_ = AddChildView(std::make_unique<views::View>());
  auto* layout3 = progress_row_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
  layout3->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  current_timestamp_label_ = progress_row_->AddChildView(std::make_unique<views::Label>(
      u"0:00", views::style::CONTEXT_LABEL, views::style::STYLE_SECONDARY));
  current_timestamp_label_->SetEnabledColor(kMahoColorSidebarSecondaryText);
  current_timestamp_label_->SetSkipSubpixelRenderingOpacityCheck(true);

  progress_view_ = progress_row_->AddChildView(std::make_unique<global_media_controls::MediaProgressView>(
      /*use_squiggly_line=*/false,
      /*playing_foreground_color_id=*/kMahoColorAccentBlue,
      /*playing_background_color_id=*/kMahoColorLibraryGlyphBorder,
      /*paused_foreground_color_id=*/kMahoColorAccentBlue,
      /*paused_background_color_id=*/kMahoColorLibraryGlyphBorder,
      /*focus_ring_color_id=*/ui::kColorSysStateFocusRing,
      /*drag_state_change_callback=*/
      base::BindRepeating(&MahoNowPlayingCard::OnDragStateChanged,
                          base::Unretained(this)),
      /*playback_state_change_for_dragging_callback=*/
      base::BindRepeating(&MahoNowPlayingCard::OnPlaybackStateChangeForDragging,
                          base::Unretained(this)),
      /*seek_callback=*/
      base::BindRepeating(&MahoNowPlayingCard::OnSeek,
                          base::Unretained(this)),
      /*on_update_progress_callback=*/
      base::BindRepeating(&MahoNowPlayingCard::OnUpdateProgress,
                          base::Unretained(this))
  ));
  layout3->SetFlexForView(progress_view_, 1);

  duration_label_ = progress_row_->AddChildView(std::make_unique<views::Label>(
      u"0:00", views::style::CONTEXT_LABEL, views::style::STYLE_SECONDARY));
  duration_label_->SetEnabledColor(kMahoColorSidebarSecondaryText);
  duration_label_->SetSkipSubpixelRenderingOpacityCheck(true);

  auto* row4 = AddChildView(std::make_unique<views::View>());
  auto* layout4 = row4->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  layout4->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto* favicon_stack = row4->AddChildView(std::make_unique<views::View>());
  favicon_stack->SetLayoutManager(std::make_unique<views::FillLayout>());
  favicon_stack->SetPreferredSize(gfx::Size(26, 26));

  favicon_view_ = favicon_stack->AddChildView(std::make_unique<GlassHoverImageButton>(
      base::BindRepeating(&MahoNowPlayingCard::OnFaviconPressed,
                          base::Unretained(this))));
  favicon_view_->SetBorder(nullptr);
  favicon_view_->SetPreferredSize(gfx::Size(26, 26));
  favicon_view_->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  favicon_view_->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  favicon_view_->SetAccessibleName(u"Jump to source tab");
  favicon_view_->SetTooltipText(u"Jump to source tab");
  views::InstallCircleHighlightPathGenerator(favicon_view_);

  note_pulse_ = favicon_stack->AddChildView(std::make_unique<NotePulseOverlay>());

  // Spacer
  auto* spacer1 = row4->AddChildView(std::make_unique<views::View>());
  layout4->SetFlexForView(spacer1, 1);

  // Prev Button
  prev_button_ = row4->AddChildView(std::make_unique<global_media_controls::MediaActionButton>(
      base::BindRepeating(&MahoNowPlayingCard::OnActionPressed,
                          base::Unretained(this),
                          media_session::mojom::MediaSessionAction::kPreviousTrack),
      static_cast<int>(media_session::mojom::MediaSessionAction::kPreviousTrack),
      IDS_MEDIA_MESSAGE_CENTER_MEDIA_NOTIFICATION_ACTION_PREVIOUS_TRACK,
      14, maho_lucide_icons::kSkipBackIcon, gfx::Size(24, 24),
       kMahoColorSidebarPrimaryText, kMahoColorSidebarSecondaryText,
       ui::kColorSysStateFocusRing));

  // Play/Pause Button
  play_pause_button_ = row4->AddChildView(std::make_unique<global_media_controls::MediaActionButton>(
      base::BindRepeating(&MahoNowPlayingCard::OnPlayPausePressed,
                          base::Unretained(this)),
      static_cast<int>(media_session::mojom::MediaSessionAction::kPlay),
      IDS_MEDIA_MESSAGE_CENTER_MEDIA_NOTIFICATION_ACTION_PLAY,
      18, maho_lucide_icons::kPlayIcon, gfx::Size(32, 32),
       kMahoColorSidebarPrimaryText, kMahoColorSidebarSecondaryText,
       ui::kColorSysStateFocusRing));

  // Next Button
  next_button_ = row4->AddChildView(std::make_unique<global_media_controls::MediaActionButton>(
      base::BindRepeating(&MahoNowPlayingCard::OnActionPressed,
                          base::Unretained(this),
                          media_session::mojom::MediaSessionAction::kNextTrack),
      static_cast<int>(media_session::mojom::MediaSessionAction::kNextTrack),
      IDS_MEDIA_MESSAGE_CENTER_MEDIA_NOTIFICATION_ACTION_NEXT_TRACK,
      14, maho_lucide_icons::kSkipForwardIcon, gfx::Size(24, 24),
       kMahoColorSidebarPrimaryText, kMahoColorSidebarSecondaryText,
       ui::kColorSysStateFocusRing));

  // Spacer
  auto* spacer2 = row4->AddChildView(std::make_unique<views::View>());
  layout4->SetFlexForView(spacer2, 1);

  // Mute Button
  mute_button_ = row4->AddChildView(std::make_unique<views::ImageButton>(
      base::BindRepeating(&MahoNowPlayingCard::OnMutePressed,
                          base::Unretained(this))));
  mute_button_->SetBorder(nullptr);
  mute_button_->SetAccessibleName(u"Toggle Mute");
  views::InstallCircleHighlightPathGenerator(mute_button_);
  UpdateMuteButton();

  UpdateExpandedState();
}

std::u16string MahoNowPlayingCard::FormatPosition(base::TimeDelta time) const {
  if (time.is_negative()) return u"0:00";
  int64_t total_seconds = time.InSeconds();
  int64_t minutes = total_seconds / 60;
  int64_t seconds = total_seconds % 60;
  return base::ASCIIToUTF16(base::StringPrintf("%d:%02d", static_cast<int>(minutes), static_cast<int>(seconds)));
}

void MahoNowPlayingCard::UpdateMuteButton() {
  if (!mute_button_) return;
  if (is_muted_) {
    views::SetImageFromVectorIconWithColor(
        mute_button_, maho_lucide_icons::kVolumeXIcon, 14,
        views::IconColors(kMahoColorSidebarSecondaryText,
                          kMahoColorSidebarSecondaryText));
    mute_button_->SetTooltipText(u"Unmute");
  } else {
    views::SetImageFromVectorIconWithColor(
        mute_button_, maho_lucide_icons::kVolume2Icon, 14,
        views::IconColors(kMahoColorSidebarSecondaryText,
                          kMahoColorSidebarSecondaryText));
    mute_button_->SetTooltipText(u"Mute");
  }
}

void MahoNowPlayingCard::OnDragStateChanged(global_media_controls::DragState state) {}
void MahoNowPlayingCard::OnPlaybackStateChangeForDragging(
    global_media_controls::PlaybackStateChangeForDragging state) {}

void MahoNowPlayingCard::OnSeek(double seek_value) {
  if (item_ && duration_ > base::TimeDelta()) {
    item_->SeekTo(duration_ * seek_value);
  }
}

void MahoNowPlayingCard::OnUpdateProgress(base::TimeDelta progress) {
  current_position_ = progress;
  current_timestamp_label_->SetText(FormatPosition(progress));
}

void MahoNowPlayingCard::OnClosePressed() {
  if (item_) {
    item_->Dismiss();
  }
}

void MahoNowPlayingCard::OnFaviconPressed() {
  content::WebContents* contents =
      content::MediaSession::GetWebContentsFromRequestId(item_id_);
  if (contents && browser_) {
    TabStripModel* model = browser_->tab_strip_model();
    if (model) {
      int index = model->GetIndexOfWebContents(contents);
      if (index != TabStripModel::kNoTab) {
        model->ActivateTabAt(index, TabStripUserGestureDetails(
            TabStripUserGestureDetails::GestureType::kMouse));
      }
    }
  }
}

void MahoNowPlayingCard::OnMutePressed() {
  content::WebContents* contents =
      content::MediaSession::GetWebContentsFromRequestId(item_id_);
  if (contents) {
    ::SetTabAudioMuted(contents, !contents->IsAudioMuted(),
                             TabMutedReason::kAudioIndicator, std::string());
  }
}

void MahoNowPlayingCard::OnActionPressed(media_session::mojom::MediaSessionAction action) {
  if (item_) {
    item_->OnMediaSessionActionButtonPressed(action);
  }
}

void MahoNowPlayingCard::OnPlayPausePressed() {
  if (item_) {
    item_->OnMediaSessionActionButtonPressed(
        play_pause_button_->GetID() == static_cast<int>(media_session::mojom::MediaSessionAction::kPlay)
            ? media_session::mojom::MediaSessionAction::kPlay
            : media_session::mojom::MediaSessionAction::kPause);
  }
}

BEGIN_METADATA(NotePulseOverlay)
END_METADATA

BEGIN_METADATA(GlassHoverImageButton)
END_METADATA

BEGIN_METADATA(MahoNowPlayingCard)
END_METADATA

}  // namespace maho
