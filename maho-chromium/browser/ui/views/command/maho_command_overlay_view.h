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
  const std::vector<CommandSuggestion>& GetSeededResultsForTesting() const {
    return seeded_results_;
  }
  bool IsSeededStateForTesting() const { return is_seeded_state_; }
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
  void InitializeSeededStateForTesting(
      std::vector<CommandSuggestion> seeds);
  void ApplyHydratedSeededIconForTesting(size_t result_index,
                                         const ImageData& icon) {
    ApplyHydratedSeededIcon(result_index, icon);
  }
  WindowOpenDisposition DispositionForModeForTesting() const {
    return DispositionForMode();
  }
  CommandOverlayMode CurrentModeForTesting() const { return mode_; }
  CommandOverlayMode OriginModeForTesting() const { return origin_mode_; }
  static size_t GetMaxSeededVisibleResultsForTesting();
  static CommandSuggestion BuildCurrentTabSeedForTesting(
      const std::string& url,
      const std::string& title) {
    return BuildCurrentTabSeed(url, title, std::nullopt);
  }
  static std::vector<CommandSuggestion>
  BuildCurrentTabSeedResultsFromHistoryForTesting(
      const std::string& current_url,
      const std::string& current_title,
      const std::vector<std::pair<std::string, std::string>>& history_entries);
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
  struct SeededResultHydration {
    size_t result_index = 0u;
    GURL url;
  };

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
  void InitializeCurrentTabSeedState(const std::string& initial_text);
  void StartSeededFaviconHydration();
  void OnSeededFaviconHydrated(
      uint32_t hydration_generation,
      SeededResultHydration hydration,
      const favicon_base::FaviconImageResult& image_result);
  void ApplyHydratedSeededIcon(size_t result_index, const ImageData& icon);
  bool ShouldClearSeededStateForText(std::u16string_view text) const;

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
  void ExitSiteSearchMode();
  void EnterAIMode();
  void ExitAIMode();
  void EnterCommandsOnlyMode();
  void ExitCommandsOnlyMode();
  void ApplyPlaceholderForMode();
  void UpdateModeChipVisibility();
  void UpdateModeChip();

  static CommandSuggestion BuildCurrentTabSeed(
      const std::string& url,
      const std::string& title,
      std::optional<ImageData> icon);
  static std::vector<CommandSuggestion> BuildCurrentTabSeedResultsFromHistory(
      const std::string& current_url,
      const std::string& current_title,
      const std::optional<ImageData>& current_icon,
      const std::vector<CommandSuggestion>& history_entries);
  static std::vector<CommandSuggestion> BuildCurrentTabSeedResults(
      Browser* browser,
      const std::string& url);

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

  void OnSearchEngineBadgePressed();
  void OnSearchEngineSelected(const std::string& engine_id);

  std::vector<CommandSuggestion> seeded_results_;
  bool is_seeded_state_ = false;
  std::string initial_url_;
  base::CancelableTaskTracker seeded_favicon_task_tracker_;
  uint32_t seeded_favicon_hydration_generation_ = 0;

  raw_ptr<views::Widget> picker_widget_ = nullptr;
  base::ScopedObservation<views::Widget, views::WidgetObserver> picker_observation_{this};

  base::WeakPtrFactory<MahoCommandOverlayView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_VIEW_H_
