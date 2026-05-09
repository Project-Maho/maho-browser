// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_VIEW_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/window_open_disposition.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget_observer.h"

class Browser;

namespace views {
class Label;
class ScrollView;
class Separator;
class Textfield;
class View;
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
  MahoCommandOverlayView(const MahoCommandOverlayView&) = delete;
  MahoCommandOverlayView& operator=(const MahoCommandOverlayView&) = delete;
  ~MahoCommandOverlayView() override;

  views::Textfield* textfield() { return textfield_; }

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  void OnAfterUserAction(views::Textfield* sender) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // views::View:
  void RequestFocus() override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void OnThemeChanged() override;

  // views::WidgetObserver:
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetVisibilityChanged(views::Widget* widget, bool visible) override;
  void OnWidgetDestroying(views::Widget* widget) override;

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

  raw_ptr<Browser> browser_;
  CommandOverlayMode mode_;
  std::unique_ptr<MahoCommandModel> model_;
  DismissCallback dismiss_callback_;
  ResizeCallback resize_callback_;

  raw_ptr<views::View> field_shell_ = nullptr;
  raw_ptr<views::View> current_tab_shell_ = nullptr;
  raw_ptr<views::View> current_tab_context_row_ = nullptr;
  raw_ptr<views::Label> current_tab_context_label_ = nullptr;
  raw_ptr<views::Label> current_tab_url_label_ = nullptr;
  raw_ptr<views::View> search_icon_lane_ = nullptr;
  raw_ptr<views::Textfield> textfield_ = nullptr;
  raw_ptr<views::View> search_row_ = nullptr;
  raw_ptr<views::View> input_background_ = nullptr;
  raw_ptr<views::View> trailing_gutter_ = nullptr;
  raw_ptr<views::View> mode_chip_ = nullptr;
  raw_ptr<views::View> mode_chip_dot_ = nullptr;
  raw_ptr<views::Separator> divider_ = nullptr;
  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<views::View> results_container_ = nullptr;
  raw_ptr<views::Label> hint_label_ = nullptr;
  raw_ptr<views::Label> mode_label_ = nullptr;
  raw_ptr<views::Widget> observed_widget_ = nullptr;

  std::string site_search_prefix_;
  std::string site_search_engine_name_;
  std::string current_tab_url_text_;
  const bool select_initial_text_;

  void EnterSiteSearchMode(const std::string& prefix,
                           const std::string& engine_name);
  void ExitSiteSearchMode();
  void EnterAIMode();
  void ExitAIMode();
  void ApplyPlaceholderForMode();
  void UpdateModeChip();
  void UpdateCurrentTabShell();

  base::WeakPtrFactory<MahoCommandOverlayView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_COMMAND_MAHO_COMMAND_OVERLAY_VIEW_H_
