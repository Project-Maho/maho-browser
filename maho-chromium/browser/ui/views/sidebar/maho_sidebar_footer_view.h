// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_FOOTER_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_FOOTER_VIEW_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "maho_sidebar_action_popover_view.h"
#include "maho_sidebar_space_dot_view.h"
#include "maho_sidebar_state_models.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/events/event.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

class Browser;
class MahoSpaceContextMenu;
struct MahoCore;

namespace ui {
class Event;
}  // namespace ui

namespace views {
class MdTextButton;
class Label;
class ScrollView;
class View;
class MenuRunner;
class Widget;
}  // namespace views

namespace maho {

class MahoSidebarFooterView : public views::View,
                              public views::ContextMenuController,
                              public ui::SimpleMenuModel::Delegate,
                              public views::WidgetObserver,
                              public MahoSidebarSpaceDotDelegate {
  METADATA_HEADER(MahoSidebarFooterView, views::View)

 public:
  explicit MahoSidebarFooterView(Browser* browser);
  MahoSidebarFooterView(const MahoSidebarFooterView&) = delete;
  MahoSidebarFooterView& operator=(const MahoSidebarFooterView&) = delete;
  ~MahoSidebarFooterView() override;

  void Update(const MahoSidebarFooterModel& model);
  void OnSidebarPaletteChanged(const MahoSidebarPalette& palette);
  void RefreshPreferredSize();

  views::MdTextButton* plus_button_for_testing() { return plus_button_; }
  views::MdTextButton* archive_button_for_testing() { return archive_button_; }
  views::Widget* action_popover_widget_for_testing() const {
    return action_popover_widget_;
  }
  static void ReorderSpaceForTesting(::MahoCore* core,
                                     const std::string& space_id,
                                     int from_index,
                                     int to_index);
  views::View* GetSpaceDotForTesting(int index);
  bool command_overlay_controller_visible_for_testing() const {
    return action_popover_widget_ != nullptr;
  }
  const MahoSidebarPalette& sidebar_palette_for_testing() const {
    return palette_;
  }

  // views::View:
  void OnThemeChanged() override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  bool OnMouseWheel(const ui::MouseWheelEvent& event) override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(views::View* source,
                                  const gfx::Point& point,
                                  ui::mojom::MenuSourceType source_type) override;

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdEnabled(int command_id) const override;
  bool IsCommandIdChecked(int command_id) const override;

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  void UpdateUpdatePill(const MahoSidebarFooterUpdatePillModel& model);
  void OnPlusPressed(const ui::Event& event);
  void OnArchivePressed(const ui::Event& event);
  void OpenSpaceCreationSurface();
  void HandleActionPopoverAction(
      MahoSidebarActionPopoverAction action);
  void CloseActionPopover();
  void RebuildDots(int space_count, int active_index,
                   const std::vector<std::string>& space_ids,
                   const std::vector<std::string>& space_icons,
                   const std::vector<std::string>& space_names,
                   const std::vector<std::string>& space_colors);
  void UpdateFooterChromeAppearance();
  void UpdateDotSelectionState();
  void UpdateFadeMask();
  void ScrollToActiveSpace();

  // MahoSidebarSpaceDotDelegate:
  void OnSpaceDotActivated(MahoSidebarSpaceDotView* dot) override;
  void OnSpaceDotContextMenu(MahoSidebarSpaceDotView* dot,
                             const gfx::Point& screen_point) override;
  void OnSpaceDotDragStarted(MahoSidebarSpaceDotView* dot,
                             const gfx::Point& press_point) override;
  void OnSpaceDotDragMoved(MahoSidebarSpaceDotView* dot,
                           const gfx::Point& screen_point) override;
  void OnSpaceDotDragEnded(MahoSidebarSpaceDotView* dot,
                           bool cancelled) override;
  void OnSpaceDotSpringLoad(MahoSidebarSpaceDotView* dot) override;
  void OnSpaceDotDropCompleted() override;

  int GetDropTargetIndex(const gfx::Point& point_in_dot_container) const;
  void ShowDropIndicator(int target_index);
  void HideDropIndicator();
  void PerformReorder(int from_index, int to_index);

  int ClampDotIndex(int index) const;
  MahoSidebarSpaceDotView* GetDotViewAt(int index) const;
  void ResetWheelGestureLatch();

  base::OneShotTimer wheel_cooldown_timer_;
  int accumulated_wheel_delta_ = 0;
  bool wheel_gesture_latched_ = false;

  MahoSidebarFooterModel model_;
  MahoSidebarPalette palette_;
  raw_ptr<Browser> browser_;
  raw_ptr<views::View> update_pill_ = nullptr;
  raw_ptr<views::View> update_pill_indicator_ = nullptr;
  raw_ptr<views::Label> update_pill_label_ = nullptr;
  raw_ptr<views::View> footer_region_ = nullptr;
  raw_ptr<views::View> spaces_cluster_ = nullptr;
  raw_ptr<views::View> dot_container_ = nullptr;
  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<views::View> fade_mask_ = nullptr;
  raw_ptr<views::MdTextButton> plus_button_ = nullptr;
  raw_ptr<views::MdTextButton> archive_button_ = nullptr;
  raw_ptr<views::Widget> action_popover_widget_ = nullptr;
  bool open_create_space_after_popover_close_ = false;
  std::string active_space_color_;
  int selected_dot_index_ = -1;
  raw_ptr<views::View> drop_indicator_ = nullptr;
  int drag_source_index_ = -1;
  std::unique_ptr<MahoSpaceContextMenu> active_space_context_menu_;
  std::unique_ptr<ui::SimpleMenuModel> active_context_menu_model_;
  std::unique_ptr<views::MenuRunner> context_menu_runner_;
  base::WeakPtrFactory<MahoSidebarFooterView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_FOOTER_VIEW_H_
