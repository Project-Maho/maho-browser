// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_VIEW_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/timer/timer.h"
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
#include "ui/gfx/geometry/point.h"
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

class MahoCommandActionSelectorView;

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
  MahoCommandModel* GetModelForTesting() const { return model_.get(); }
  size_t GetRenderedResultCountForTesting() const;
  int GetResultViewRebuildCountForTesting() const {
    return result_view_rebuild_count_for_testing_;
  }
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
  PaletteAction GetPaletteActionForTesting() const { return palette_action_; }
  MahoCommandActionSelectorView* GetActionSelectorForTesting() const { return action_selector_; }
  CommandOverlayMode OriginModeForTesting() const { return origin_mode_; }
  void SetContextClassForTesting(MahoPrivateContextClass klass) {
    context_class_ = klass;
  }
  bool AiIngressAllowedForTesting() const { return AiIngressAllowed(); }
  // Pure inline-autocomplete rule: returns |typed| extended to the first
  // history/tab row whose scheme-less host (optionally without "www.") starts
  // with it, or nullopt when nothing should be completed.
  static std::optional<std::string> ComputeInlineCompletion(
      const std::string& typed,
      const std::vector<CommandSuggestion>& results);
  // Text the field shows when |suggestion| is keyboard-selected: the query for
  // search rows, the URL for URL-bearing rows, empty (keep typed text) else.
  static std::string TextfieldTextForSuggestion(
      const CommandSuggestion& suggestion);
  void OnRowHoveredForTesting(int index) { OnRowHovered(index); }
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
  void AddedToWidget() override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  gfx::Size GetMinimumSize() const override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

  static constexpr int kPopupWidthDp = 744;
  static constexpr int kPopupMinHeightDp = 48;

  // True if |screen_point| is inside the search-engine picker; lets the
  // controller's outside-click monitor spare picker clicks.
  bool PickerContainsScreenPoint(const gfx::Point& screen_point) const;

  // True while the search-engine picker bubble is open, so the controller does
  // not dismiss the palette when the picker steals activation.
  bool IsPickerOpen() const;

 private:
  void OnSearchResults(std::vector<CommandSuggestion> results);
  void OnSearchResultsSequenced(uint64_t seq, std::vector<CommandSuggestion> results);
  void TriggerSearchForCurrentText();
  void UpdateResultViews(const std::vector<CommandSuggestion>& results);
  void UpdateSelection(int new_index);
  void UpdateSelectionImpl(int new_index, bool sync_textfield);
  void OnRowHovered(int index);
  void RecordSuggestionUsage(const CommandSuggestion& suggestion);
  void MaybeApplyInlineAutocomplete();
  void ActivateResultAtIndex(int index);
  void ActivateSelected();
  void ActivateFirstNavigation();
  void NavigateTypedText();
  WindowOpenDisposition DispositionForMode() const;
  void Dismiss();
  // Shared Escape handling for the focused-textfield path and the palette-wide
  // Escape accelerator. Returns false only when an active IME composition
  // should cancel instead of dismissing.
  bool HandleEscape();
  void ResizeWidgetToContents();
  void UpdateLayoutForMode();
  void UpdateTextfieldPresentation();
  void UpdateSearchLeadingAccessory();
  void UpdateTrailingAccessoryPresentation();
  void UpdateSearchRowChrome();
  void ExitSiteSearchMode();
  void OnPaletteActionChanged(PaletteAction action);
  // AI ingress (ai+Tab, AI-mode Enter, pending-query pref write) is denied in
  // every OTR class; allowed only for regular (kNull = unit tests without a
  // Browser, treated as allowed so existing AI-mode tests keep passing).
  bool AiIngressAllowed() const;
  void EnterCommandsOnlyMode();
  void ExitCommandsOnlyMode();
  void ApplyPlaceholderForMode();
  void UpdateModeChipVisibility();
  void UpdateModeChip();

  raw_ptr<Browser> browser_;
  CommandOverlayMode mode_;
  MahoPrivateContextClass context_class_ = MahoPrivateContextClass::kNull;
  std::unique_ptr<MahoCommandModel> model_;
  DismissCallback dismiss_callback_;
  ResizeCallback resize_callback_;

  raw_ptr<views::View> field_shell_ = nullptr;
  raw_ptr<views::View> search_icon_lane_ = nullptr;
  raw_ptr<views::ImageView> favicon_image_ = nullptr;
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
  raw_ptr<MahoCommandActionSelectorView> action_selector_ = nullptr;
  PaletteAction palette_action_ = PaletteAction::kSearch;

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
  // |initial_url_| as displayed (percent-escaped UTF-8 decoded).
  std::string initial_display_text_;
  bool suppress_search_ = false;
  bool suppress_inline_autocomplete_ = false;
  base::ScopedObservation<views::Widget, views::WidgetObserver> picker_observation_{this};

  base::OneShotTimer debounce_timer_;
  bool first_search_since_open_ = true;
  bool user_navigated_results_ = false;
  bool esc_accelerator_registered_ = false;
  uint64_t search_seq_ = 0;
  int last_resized_height_ = -1;
  int result_view_rebuild_count_for_testing_ = 0;

  base::WeakPtrFactory<MahoCommandOverlayView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_VIEW_H_
