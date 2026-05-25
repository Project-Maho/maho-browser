// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_SPACE_DOT_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_SPACE_DOT_VIEW_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
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
};

// Renders a single space. If the space has an icon, it renders the icon glyph
// at rest in all states. If it doesn't have an icon, it falls back to drawing
// a circle dot at a size tier depending on state (active, selected, hovered).
// Active spaces also render a subtle outer halo ring.
class MahoSidebarSpaceDotView : public views::View {
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
  void ConfigureProfileBadge(SkColor color, bool visible);
  void set_delegate(MahoSidebarSpaceDotDelegate* delegate) {
    delegate_ = delegate;
  }

  const std::string& space_id() const { return space_id_; }
  bool is_active() const { return is_active_; }

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

 private:
  static constexpr int kViewSize = 15;
  static constexpr int kIndicatorSize = 11;
  // Size tier constants for icon-less fallback dots. Tuned to read as a
  // footer navigation affordance.
  static constexpr float kDotSizeDefault = 4.0f;
  static constexpr float kDotSizeSelected = 4.8f;
  static constexpr float kDotSizeHoveredNoIcon = 5.8f;
  static constexpr float kDotSizeActive = 7.6f;
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

  float dot_opacity_ = 1.0f;

  raw_ptr<views::Label> icon_label_ = nullptr;
  raw_ptr<views::View> profile_badge_ = nullptr;

  gfx::Point drag_start_point_;
  bool did_start_drag_ = false;

  friend class MahoSidebarSpaceDotViewTest;

  raw_ptr<MahoSidebarSpaceDotDelegate> delegate_ = nullptr;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_SPACE_DOT_VIEW_H_
