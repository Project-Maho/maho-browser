// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_UPDATE_NOTIFICATION_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_UPDATE_NOTIFICATION_VIEW_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_update_notification_model.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"

namespace maho {

// Hover-to-expand sidebar update notification, modelled on Arc/Zen's pill +
// expanded card pattern. Default state shows a compact pill with just the
// heading; entering the view smoothly animates the card into its full form
// (close button, divider, action rows). Exiting collapses back to the pill.
class MahoSidebarUpdateNotificationView : public views::View,
                                          public gfx::AnimationDelegate {
  METADATA_HEADER(MahoSidebarUpdateNotificationView, views::View)

 public:
  MahoSidebarUpdateNotificationView();
  MahoSidebarUpdateNotificationView(
      const MahoSidebarUpdateNotificationView&) = delete;
  MahoSidebarUpdateNotificationView& operator=(
      const MahoSidebarUpdateNotificationView&) = delete;
  ~MahoSidebarUpdateNotificationView() override;

  void Update(const MahoSidebarUpdateNotificationModel& model);
  void SetSidebarPalette(const MahoSidebarPalette& palette);

  size_t action_row_count_for_testing() const { return action_rows_.size(); }
  views::Button* action_row_for_testing(size_t index);
  views::Label* heading_for_testing() { return heading_; }
  const MahoSidebarPalette& sidebar_palette_for_testing() const {
    return palette_;
  }

  // views::View:
  void OnPaintBackground(gfx::Canvas* canvas) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;

 private:
  void RebuildBody(const MahoSidebarUpdateNotificationModel& model);
  void AnimateVisibilityChange(bool target_visible);
  void UpdateExpandedState();
  int CollapsedHeight() const;
  int ExpandedHeight(const views::SizeBounds& available) const;

  raw_ptr<views::View> header_ = nullptr;
  raw_ptr<views::Label> heading_ = nullptr;
  raw_ptr<views::View> divider_ = nullptr;
  raw_ptr<views::View> body_ = nullptr;
  raw_ptr<views::Checkbox> checkbox_ = nullptr;
  std::vector<raw_ptr<views::Button>> action_rows_;

  base::RepeatingClosure on_dismiss_;
  bool last_visible_ = false;
  gfx::SlideAnimation expand_animation_{this};
  MahoSidebarPalette palette_;
  base::WeakPtrFactory<MahoSidebarUpdateNotificationView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_UPDATE_NOTIFICATION_VIEW_H_
