// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_CONTAINER_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_CONTAINER_VIEW_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

class Browser;

namespace maho {

class MahoCommandOverlayController;

class MahoSidebarContainerView : public views::View,
                                  public gfx::AnimationDelegate {
  METADATA_HEADER(MahoSidebarContainerView, views::View)

 public:
  DECLARE_CLASS_ELEMENT_IDENTIFIER_VALUE(kMahoSidebarContainerElementId);

  explicit MahoSidebarContainerView(Browser* browser);
  ~MahoSidebarContainerView() override;

  MahoSidebarContainerView(const MahoSidebarContainerView&) = delete;
  MahoSidebarContainerView& operator=(const MahoSidebarContainerView&) = delete;

  views::View* sidebar_view() { return sidebar_view_; }

  MahoCommandOverlayController* command_overlay_controller() {
    return command_overlay_controller_.get();
  }

  void ShowCreateSpace();
  void ToggleCommandOverlay();
  void ShowCommandOverlayForCurrentTab(const std::string& current_url);
  void ShowCommandOverlayForNewTab();
  void ShowCommandOverlayForSearch(const std::string& initial_query);

  bool IsPositionInWindowCaption(const gfx::Point& point) const;

  double GetRevealFraction() const;
  bool IsAutoHideMode() const;
  bool IsOverlayVisible() const;

  bool IsResizeDragEnabled() const;
  bool IsResizeDragging() const;
  void BeginResizeDrag(const ui::MouseEvent& event);
  void UpdateResizeDrag(const ui::MouseEvent& event);
  void EndResizeDrag();
  void UpdateResizeHandleBounds();

  // views::View:
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

 private:
  void OnPanelExpandedChanged();
  void OnSidebarWidthChanged();
  void ApplyPanelState();
  void ApplyPreferredWidthFromPrefs();
  void MaybePersistSidebarWidth(int width);
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;
  bool IsCursorInsideContainerScreenBounds() const;

  void StartReveal();
  void StartHide();
  void OnHideDelayExpired();
  void OnHoverRevealSuppressionExpired();

  raw_ptr<Browser> browser_;
  raw_ptr<views::View> sidebar_view_ = nullptr;
  raw_ptr<views::View> resize_handle_view_ = nullptr;
  std::unique_ptr<MahoCommandOverlayController> command_overlay_controller_;
  PrefChangeRegistrar pref_registrar_;

  gfx::SlideAnimation reveal_animation_;
  bool is_hover_revealed_ = false;
  base::OneShotTimer hide_delay_timer_;
  base::OneShotTimer hover_reveal_suppression_timer_;
  bool suppress_hover_reveal_ = false;
  bool was_panel_expanded_ = false;
  bool is_resizing_ = false;
  int resize_drag_start_x_ = 0;
  int resize_drag_start_width_ = 0;

  base::WeakPtrFactory<MahoSidebarContainerView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_CONTAINER_VIEW_H_
