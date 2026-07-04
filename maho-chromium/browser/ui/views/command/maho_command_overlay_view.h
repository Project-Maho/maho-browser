// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_VIEW_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/scoped_observation.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "url/gurl.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/window_open_disposition.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget_observer.h"

class Browser;

namespace favicon_base {
struct FaviconImageResult;
}

namespace views {
class ImageView;
class Label;
class ScrollView;
class Separator;
class View;
class MdTextButton;
class Widget;
}  // namespace views

namespace maho {

class MahoCommandModel;
struct CommandSuggestion;

class MahoCommandOverlayView : public views::View,
                               public views::TextfieldController,
                               public views::WidgetObserver {
  METADATA_HEADER(MahoCommandOverlayView, views::View)

 public:
  using DismissCallback = base::OnceClosure;
  using ResizeCallback = base::RepeatingCallback<void(int)>;

  explicit MahoCommandOverlayView(Browser* browser,
                                   CommandOverlayMode mode,
                                   const std::string& initial_text,
                                   bool select_initial_text,
                                   DismissCallback dismiss_callback,
                                   ResizeCallback resize_callback);

  // Test-only constructor: accepts nullptr for |browser| and skips the
  // production DCHECK so that unit tests can exercise key-handling and mode
  // transitions without a live Browser instance.
  struct ForTestingTag {};
  MahoCommandOverlayView(ForTestingTag,
                         CommandOverlayMode mode,
                         const std::string& initial_text,
                         bool select_initial_text,
                         DismissCallback dismiss_callback,
                         ResizeCallback resize_callback);
  MahoCommandOverlayView(const MahoCommandOverlayView&) = delete;
  MahoCommandOverlayView& operator=(const MahoCommandOverlayView&) = delete;
  ~MahoCommandOverlayView() override;

  views::Textfield* textfield() { return textfield_; }
  size_t GetRenderedResultCountForTesting() const;
  views::View* GetResultsContainerForTesting() const { return results_container_; }
  bool IsModeChipVisibleForTesting() const {
    return mode_chip_ && mode_chip_->GetVisible();
  }
  bool IsDividerVisibleForTesting() const;
  bool TextfieldControlsResultsContainerForTesting() const;
  bool TextfieldIsCollapsedForTesting() const;
  bool TextfieldIsExpandedForTesting() const;
  views::ViewAccessibility* GetActiveDescendantForTesting() const;
  views::View* GetSearchRowForTesting() const { return search_row_; }
  views::MdTextButton* GetTrailingAccessoryForTesting() const {
    return trailing_accessory_;
  }
  void UpdateResultViewsForTesting(
      const std::vector<CommandSuggestion>& results);
  void UpdateSelectionA11yForTesting(int old_index, int new_index);
  WindowOpenDisposition DispositionForModeForTesting() const {
    return DispositionForMode();
  }
  CommandOverlayMode CurrentModeForTesting() const { return mode_; }
  CommandOverlayMode OriginModeForTesting() const { return origin_mode_; }
  static int GetPanelCornerRadiusForTesting();
  static int GetSearchRowCornerRadiusForTesting();
  static int GetTopbarHorizontalInsetForTesting();
  static gfx::Insets GetPanelInsetsForTesting();
  static gfx::Insets GetEmptyStateInsetsForTesting(bool compact);
  static int GetPanelSectionSpacingForTesting();
  static int GetResultRowSpacingForTesting();
  static gfx::Insets GetDividerInsetsForTesting();

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  void OnAfterUserAction(views::Textfield* sender) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // views::View:
  void RequestFocus() override;
  void OnThemeChanged() override;

 private:
  void OnSearchResults(std::vector<CommandSuggestion> results);
  void TriggerSearchForCurrentText();
  void UpdateResultViews(const std::vector<CommandSuggestion>& results);
  void UpdateSelection(int new_index);
  void ActivateResultAtIndex(int index);
  void ActivateSelected();
  void ActivateFirstNavigation();
  void NavigateTypedText();
  WindowOpenDisposition DispositionForMode() const;
  void Dismiss();
  void ResizeWidgetToContents();
  void UpdateLayoutForMode();
  void UpdateTextfieldPresentation();
  void UpdateSearchLeadingAccessory();
  void UpdateTrailingAccessoryPresentation();
  void UpdateSearchRowChrome();
  void ExitSiteSearchMode();
  void EnterAIMode();
  void ExitAIMode();
  void EnterCommandsOnlyMode();
  void ExitCommandsOnlyMode();
  void ApplyPlaceholderForMode();
  void UpdateModeChipVisibility();
  void UpdateModeChip();

  raw_ptr<Browser> browser_;
  CommandOverlayMode mode_;
  std::unique_ptr<MahoCommandModel> model_;
  DismissCallback dismiss_callback_;
  ResizeCallback resize_callback_;

  raw_ptr<views::View> field_shell_ = nullptr;
  raw_ptr<views::View> search_icon_lane_ = nullptr;
  raw_ptr<views::ImageView> search_context_icon_ = nullptr;
  raw_ptr<views::Textfield> textfield_ = nullptr;
  raw_ptr<views::View> search_row_ = nullptr;
  raw_ptr<views::View> input_background_ = nullptr;
  raw_ptr<views::MdTextButton> trailing_accessory_ = nullptr;
  raw_ptr<views::View> mode_chip_ = nullptr;
  raw_ptr<views::View> mode_chip_dot_ = nullptr;
  raw_ptr<views::Separator> divider_ = nullptr;
  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<views::View> results_container_ = nullptr;
  raw_ptr<views::Label> hint_label_ = nullptr;
  raw_ptr<views::Label> mode_label_ = nullptr;
  raw_ptr<views::Label> trailing_accessory_label_ = nullptr;

  std::string site_search_prefix_;
  std::string site_search_engine_name_;
  // The mode active before entering a transient sub-mode (AI / SiteSearch).
  // Used to restore the correct disposition when the sub-mode is exited.
  CommandOverlayMode origin_mode_;
  const bool select_initial_text_;

  void EnterSiteSearchMode(const std::string& prefix,
                           const std::string& engine_name);

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

  void ApplyVibrancyBackgroundTransparency(bool vibrancy_active);

  void OnSearchEngineBadgePressed();
  void OnSearchEngineSelected(const std::string& engine_id);



  raw_ptr<views::Widget> picker_widget_ = nullptr;
  std::string initial_url_;
  bool suppress_search_ = false;
  base::ScopedObservation<views::Widget, views::WidgetObserver> picker_observation_{this};

  base::WeakPtrFactory<MahoCommandOverlayView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_VIEW_H_
