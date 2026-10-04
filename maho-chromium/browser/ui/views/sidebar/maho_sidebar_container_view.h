// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_CONTAINER_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_CONTAINER_VIEW_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "components/prefs/pref_change_registrar.h"
#include "maho/browser/ui/views/peek/maho_peek_route.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/resize_area_delegate.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget_observer.h"

class Browser;

namespace content {
class WebContents;
}

namespace maho {

class MahoSidebarNowPlayingView;
class MahoSpacesOverlayController;
class MahoBrowserFrameOverlayHost;

class MahoSidebarContainerView : public views::View,
                                  public views::ResizeAreaDelegate,
                                  public gfx::AnimationDelegate,
                                  public views::WidgetObserver {
  METADATA_HEADER(MahoSidebarContainerView, views::View)

 public:
  DECLARE_CLASS_ELEMENT_IDENTIFIER_VALUE(kMahoSidebarContainerElementId);

  explicit MahoSidebarContainerView(Browser* browser);
  ~MahoSidebarContainerView() override;

  MahoSidebarContainerView(const MahoSidebarContainerView&) = delete;
  MahoSidebarContainerView& operator=(const MahoSidebarContainerView&) = delete;

  views::View* sidebar_view() { return sidebar_view_; }
  void ShowCommandOverlayForCurrentTab();
  void ShowCommandOverlayForNewTab();
  void ShowCommandOverlayForSearch(const std::string& initial_query);
  bool ShowAppMenu();

  bool TryActivateFavoriteByIndex(size_t index);

  // Resolves the source tab role used by Peek link routing. Native pinned
  // state takes precedence; Favorite is derived from the matching maho-core
  // tab role. Returns kUnresolved when any required browser/sidebar mapping is
  // unavailable rather than guessing a normal role.
  static PeekSourceRole GetPeekSourceRole(
      Browser* browser,
      content::WebContents* web_contents);

  bool IsPositionInWindowCaption(const gfx::Point& point) const;

  double GetRevealFraction() const;
  bool IsAutoHideMode() const;
  // Width the sidebar occupies floating over the contents without reserving
  // layout space: 0 when pinned, the invisible hover strip while auto-hidden,
  // and the full sidebar while hover-revealed or animating.
  int GetFloatingOverlayWidth() const;
  bool IsOverlayVisible() const;
  void ToggleSpacesOverlay();
  void DismissSpacesOverlay();
  bool IsSpacesOverlayVisible() const;
  void ToggleLibraryOverlay(MahoSidebarLibraryRailView::Category category);
  void DismissLibraryOverlay();
  bool IsLibraryOverlayVisible() const;
  int GetLibraryRailWidth() const;
  MahoBrowserFrameOverlayHost* overlay_host() { return overlay_host_; }
  MahoBrowserFrameOverlayHost* GetOrCreateOverlayHost();

  bool IsResizeDragEnabled() const;
  bool IsResizeDragging() const;
  void BeginResizeDrag(const ui::MouseEvent& event);
  void UpdateResizeDrag(const ui::MouseEvent& event);
  void EndResizeDrag();
  void UpdateResizeHandleBounds();

  // views::View:
  void Layout(PassKey) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void OnScrollEvent(ui::ScrollEvent* event) override;
  bool OnMouseWheel(const ui::MouseWheelEvent& event) override;
  void OnThemeChanged() override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;

  // views::ResizeAreaDelegate:
  void OnResize(int resize_amount, bool done_resizing) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

  // views::WidgetObserver:
  void OnWidgetVisibilityChanged(views::Widget* widget, bool visible) override;

  gfx::Insets GetSidebarSlotInsets() const;
  bool IsFullyAutoHidden() const;

  // R-11: Presentation-only private-appearance injection point.
  // Accepts fully-built private identity image + semantic color IDs.
  // No core/pref read; called from R-12 BrowserView projection.
  void SetPrivateAppearance(const ui::ImageModel& identity_icon,
                            ui::ColorId surface_color_id,
                            ui::ColorId text_color_id);
  bool IsPrivateMode() const { return is_private_; }
  bool IsPanelExpanded() const { return ReadPanelExpanded(); }

  // Toggles expanded/collapsed. Private/OTR flips in-memory state only, never
  // sidebar prefs; R-10 `toggle_sidebar` routes here.
  void TogglePanelExpanded();

 private:
  void OnPanelExpandedChanged();
  void OnSidebarWidthChanged();
  void ApplyPanelState();
  // Private/OTR: read/write in-memory state, never sidebar prefs. Regular:
  // delegate to PrefService (byte-identical to prior behavior).
  bool ReadPanelExpanded() const;
  int ReadSidebarWidth() const;
  void WriteSidebarWidth(int clamped_width);
  bool ReadSpacesFullViewport() const;
  void CheckAndNotifyAutoHide();
  // Attaches the overlay host as a BrowserView child. Called via PostTask
  // from AddedToWidget() to avoid mutating the parent's child list while
  // the view tree is still iterating our attachment.
  void AttachOverlayHost();
  void CreateNowPlayingHostIfNeeded();

 public:
  void ApplyPreferredWidthFromPrefs();

 private:
  void MaybePersistSidebarWidth(int width);
  int ComputeExpandedWidth() const;
  void ApplyRevealTransform();
  void ApplySidebarViewBackground();
  void UpdateZOrder();
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;
  bool IsCursorInsideContainerScreenBounds() const;

  void StartReveal();
  void StartHide();
  void OnHideDelayExpired();
  void OnHoverRevealSuppressionExpired();

  raw_ptr<Browser> browser_;
  raw_ptr<views::View> sidebar_view_ = nullptr;
  raw_ptr<views::View> resize_handle_view_ = nullptr;
  raw_ptr<MahoSidebarNowPlayingView> now_playing_view_ = nullptr;
  PrefChangeRegistrar pref_registrar_;

  float swipe_accumulated_x_ = 0.0f;

  gfx::SlideAnimation reveal_animation_;
  bool is_hover_revealed_ = false;
  base::OneShotTimer hide_delay_timer_;
  base::OneShotTimer hover_reveal_suppression_timer_;
  bool suppress_hover_reveal_ = false;
  bool was_panel_expanded_ = false;
  bool is_resizing_ = false;
  int resize_drag_start_width_ = -1;
  raw_ptr<MahoBrowserFrameOverlayHost> overlay_host_ = nullptr;
  bool is_private_ = false;
  bool private_panel_expanded_ = true;
  int private_sidebar_width_ = 0;

  base::WeakPtrFactory<MahoSidebarContainerView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_CONTAINER_VIEW_H_
