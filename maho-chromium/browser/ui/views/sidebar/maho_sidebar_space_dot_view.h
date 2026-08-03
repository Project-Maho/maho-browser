// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_SPACE_DOT_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_SPACE_DOT_VIEW_H_

#include <set>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"

namespace views {
class Label;
}  // namespace views

namespace maho {

class MahoSidebarSpaceDotView;

class MahoSidebarSpaceDotDelegate {
 public:
  virtual ~MahoSidebarSpaceDotDelegate() = default;
  virtual void OnSpaceDotActivated(MahoSidebarSpaceDotView* dot) = 0;
  virtual void OnSpaceDotContextMenu(MahoSidebarSpaceDotView* dot,
                                     const gfx::Point& screen_point) = 0;
  virtual void OnSpaceDotDragStarted(MahoSidebarSpaceDotView* dot,
                                     const gfx::Point& press_point) = 0;
  virtual void OnSpaceDotDragMoved(MahoSidebarSpaceDotView* dot,
                                   const gfx::Point& screen_point) = 0;
  virtual void OnSpaceDotDragEnded(MahoSidebarSpaceDotView* dot,
                                   bool cancelled) = 0;
  // Fired when a sidebar item has hovered over |dot| long enough during a drag
  // to spring-load: the delegate should switch the sidebar to that dot's Space.
  virtual void OnSpaceDotSpringLoad(MahoSidebarSpaceDotView* dot) {}
  // Fired right after a successful drop onto a dot moved an item to that Space,
  // so the delegate can refresh the sidebar synchronously (instant settle).
  virtual void OnSpaceDotDropCompleted() {}
};

// Renders a single space. If the space has an icon, it renders the icon glyph
// at rest in all states. If it doesn't have an icon, it falls back to drawing
// a circle dot at a size tier depending on state (active, selected, hovered).
// Active spaces also render a subtle outer halo ring.
class MahoSidebarSpaceDotView : public views::View,
                                public gfx::AnimationDelegate {
  METADATA_HEADER(MahoSidebarSpaceDotView, views::View)

 public:
  MahoSidebarSpaceDotView();
  MahoSidebarSpaceDotView(const MahoSidebarSpaceDotView&) = delete;
  MahoSidebarSpaceDotView& operator=(const MahoSidebarSpaceDotView&) = delete;
  ~MahoSidebarSpaceDotView() override;

  void Configure(const std::string& space_id,
                 const std::string& name,
                 const std::string& icon,
                 SkColor color,
                 bool is_active);

  void SetActive(bool active);
  void SetSelected(bool selected);
  void SetSidebarPalette(const MahoSidebarPalette& palette);
  void ConfigureProfileBadge(SkColor color, bool visible);
  void set_delegate(MahoSidebarSpaceDotDelegate* delegate) {
    delegate_ = delegate;
  }

  const std::string& space_id() const { return space_id_; }
  bool is_active() const { return is_active_; }
  bool has_icon_for_testing() const { return has_icon_; }
  float dot_opacity_for_testing() const { return dot_opacity_; }
  float icon_layer_opacity_for_testing() const {
    return icon_label_ && icon_label_->layer()
               ? icon_label_->layer()->GetTargetOpacity()
               : 0.0f;
  }
  float current_dot_size_for_testing() const { return GetCurrentDotSize(); }
  gfx::SlideAnimation& hover_animation_for_testing() {
    return hover_animation_;
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void OnPaint(gfx::Canvas* canvas) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseCaptureLost() override;

  // views::View drop target: a tab/folder/split dragged from the sidebar can be
  // dropped onto a space dot to move it into that space.
  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;
  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;

 private:
  static constexpr int kViewSize = 15;
  static constexpr int kIndicatorSize = 11;
  // Size tier constants for icon-less fallback dots. Tuned to read as a
  // footer navigation affordance.
  static constexpr float kDotSizeDefault = 8.0f;
  static constexpr float kDotSizeSelected = 9.0f;
  static constexpr float kDotSizeHoveredNoIcon = 11.0f;
  static constexpr float kDotSizeActive = 10.0f;
  static constexpr float kDotSizeActiveHovered = 12.0f;
  static constexpr float kDragStartThreshold = 4.0f;

  float GetCurrentDotSize() const;
  void UpdateAppearance();

  std::string space_id_;
  std::string name_;
  std::string icon_;
  SkColor dot_color_ = SK_ColorGRAY;
  bool is_active_ = false;
  bool is_selected_ = false;
  bool is_hovered_ = false;
  bool has_icon_ = false;
  bool drag_over_ = false;
  MahoSidebarPalette palette_;
  base::OneShotTimer spring_load_timer_;

  float dot_opacity_ = 1.0f;

  gfx::SlideAnimation hover_animation_{this};

  raw_ptr<views::Label> icon_label_ = nullptr;
  raw_ptr<views::View> profile_badge_ = nullptr;

  gfx::Point drag_start_point_;
  bool did_start_drag_ = false;

  friend class MahoSidebarSpaceDotViewTest;

  raw_ptr<MahoSidebarSpaceDotDelegate> delegate_ = nullptr;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_SPACE_DOT_VIEW_H_
