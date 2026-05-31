// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_DOM_SCREENSHOT_ACTION_DIALOG_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_DOM_SCREENSHOT_ACTION_DIALOG_VIEW_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget_observer.h"

namespace views {
class Button;
class Widget;
}  // namespace views

namespace maho {

struct MahoDomScreenshotActionRow {
  MahoDomScreenshotActionRow();
  MahoDomScreenshotActionRow(std::u16string label,
                             raw_ptr<const gfx::VectorIcon> icon,
                             bool enabled,
                             bool destructive,
                             base::OnceClosure activate);
  MahoDomScreenshotActionRow(MahoDomScreenshotActionRow&&);
  MahoDomScreenshotActionRow& operator=(MahoDomScreenshotActionRow&&);
  ~MahoDomScreenshotActionRow();

  std::u16string label;
  raw_ptr<const gfx::VectorIcon> icon = nullptr;
  bool enabled = true;
  bool destructive = false;
  base::OnceClosure activate;
};

struct MahoDomScreenshotActionMenu {
  MahoDomScreenshotActionMenu();
  MahoDomScreenshotActionMenu(MahoDomScreenshotActionMenu&&);
  MahoDomScreenshotActionMenu& operator=(MahoDomScreenshotActionMenu&&);
  ~MahoDomScreenshotActionMenu();

  std::u16string header_label;
  raw_ptr<const gfx::VectorIcon> header_icon = nullptr;
  base::OnceClosure header_activate;
  std::vector<MahoDomScreenshotActionRow> rows;
  base::OnceClosure dismiss_callback;
};

class MahoDomScreenshotActionDialogView
    : public views::View,
      public views::WidgetObserver {
 public:
  explicit MahoDomScreenshotActionDialogView(MahoDomScreenshotActionMenu menu);
  MahoDomScreenshotActionDialogView(
      const MahoDomScreenshotActionDialogView&) = delete;
  MahoDomScreenshotActionDialogView& operator=(
      const MahoDomScreenshotActionDialogView&) = delete;
  ~MahoDomScreenshotActionDialogView() override;

  void RequestFocus() override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;

  static views::Widget* Show(gfx::NativeView parent_window,
                             const gfx::Rect& selection_rect_screen,
                             MahoDomScreenshotActionMenu menu);

  static views::Widget* Show(gfx::NativeView parent_window,
                             const gfx::Rect& selection_rect_screen,
                             base::OnceClosure copy_callback,
                             base::OnceClosure download_callback,
                             base::OnceClosure dismiss_callback);

  static gfx::Rect GetAnchorRectForSelection(
      gfx::NativeView parent_window,
      const gfx::Rect& selection_rect_screen);
  static views::BubbleBorder::Arrow GetArrowForSelection(
      gfx::NativeView parent_window,
      const gfx::Rect& selection_rect_screen);

  size_t row_count_for_testing() const { return row_buttons_.size(); }
  std::u16string header_label_for_testing() const;
  std::u16string row_label_for_testing(size_t index) const;
  bool row_enabled_for_testing(size_t index) const;
  views::Button* header_button_for_testing() const { return header_button_; }
  views::Button* row_button_for_testing(size_t index) const;

  void PressHeaderForTesting();
  void PressRowForTesting(size_t index);

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  void ActivateHeader();
  void ActivateRow(size_t index);
  void DismissForAction();
  bool MoveFocusBy(int delta);
  void RunDismissCallback();

  MahoDomScreenshotActionMenu menu_;
  raw_ptr<views::Button> header_button_ = nullptr;
  std::vector<raw_ptr<views::Button>> row_buttons_;
  bool action_invoked_ = false;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
  base::WeakPtrFactory<MahoDomScreenshotActionDialogView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_DOM_SCREENSHOT_ACTION_DIALOG_VIEW_H_
