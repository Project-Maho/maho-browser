// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_ACTION_SELECTOR_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_ACTION_SELECTOR_VIEW_H_

#include "base/functional/callback.h"
#include "base/scoped_observation.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget_observer.h"

namespace views {
class Button;
class Label;
class Widget;
}

namespace maho {

class MahoCommandActionSelectorView : public views::View,
                                      public views::WidgetObserver {
  friend class MahoCommandActionSelectorViewTest;

 public:
  METADATA_HEADER(MahoCommandActionSelectorView, views::View)

 public:
  explicit MahoCommandActionSelectorView(
      base::RepeatingCallback<void(PaletteAction)> on_change);
  MahoCommandActionSelectorView(const MahoCommandActionSelectorView&) = delete;
  MahoCommandActionSelectorView& operator=(
      const MahoCommandActionSelectorView&) = delete;
  ~MahoCommandActionSelectorView() override;

  void SetSelected(PaletteAction action);
  PaletteAction selected() const { return selected_; }

  void SetEnabled(bool enabled);
  bool enabled() const { return enabled_; }

  // views::View overrides:
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void VisibilityChanged(views::View* starting_from, bool is_visible) override;
  void OnThemeChanged() override;
  void Layout(PassKey) override;

  // views::WidgetObserver overrides:
  void OnWidgetVisibilityChanged(views::Widget* widget,
                                 bool visible) override;
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetDestroyed(views::Widget* widget) override;

  void OnSegmentClickedForTesting(PaletteAction action) {
    OnSegmentClicked(action);
  }

 private:
  bool IsFlowBackgroundInstalledForTesting(PaletteAction action) const;
  bool IsFlowAnimatingForTesting(PaletteAction action) const;
  SkColor FlowAccentForTesting(PaletteAction action) const;
  SkColor SegmentTextColorForTesting(PaletteAction action) const;
  bool SegmentUsesSubpixelRenderingForTesting(PaletteAction action) const;
  static int GetRunningFlowAnimationCountForTesting();
  static int GetSegmentCornerRadiusForTesting();

  bool ShouldAnimateFlow() const;
  void StopFlowAnimations();
  void UpdateFlowAnimationState();
  void OnSegmentClicked(PaletteAction action);
  void UpdateVisuals();

  base::RepeatingCallback<void(PaletteAction)> on_change_;
  PaletteAction selected_ = PaletteAction::kSearch;
  bool enabled_ = true;

  raw_ptr<views::Button> search_button_ = nullptr;
  raw_ptr<views::Button> ask_maho_button_ = nullptr;
  raw_ptr<views::Label> hint_label_ = nullptr;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_ACTION_SELECTOR_VIEW_H_
