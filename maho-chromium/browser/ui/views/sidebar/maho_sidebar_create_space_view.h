// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_CREATE_SPACE_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_CREATE_SPACE_VIEW_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class Browser;

namespace views {
class Button;
class ImageView;
class Label;
class MdTextButton;
class MenuRunner;
class ScrollView;
class Textfield;
}  // namespace views

namespace maho {

enum class MahoSidebarCreateSpaceThemeMode {
  kSolid,
  kZen,
};

struct MahoSidebarCreateSpaceThemeSelection {
  int preset_index = 7;
  int brightness = 90;
  int grain = 0;
  MahoSidebarCreateSpaceThemeMode mode =
      MahoSidebarCreateSpaceThemeMode::kZen;
  std::string zen_scheme = "auto";
  double opacity = 0.0;
};

// Reusable native create-space form content, hosted inline inside the sidebar
// and exercised directly by sidebar interactive tests.
class MahoSidebarCreateSpaceView : public views::View,
                                   public views::TextfieldController,
                                   public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(MahoSidebarCreateSpaceView, views::View)

 public:
  explicit MahoSidebarCreateSpaceView(Browser* browser);
  MahoSidebarCreateSpaceView(const MahoSidebarCreateSpaceView&) = delete;
  MahoSidebarCreateSpaceView& operator=(const MahoSidebarCreateSpaceView&) =
      delete;
  ~MahoSidebarCreateSpaceView() override;

  void OnThemeChanged() override;
  void SetSidebarPalette(const MahoSidebarPalette& palette);

  void SetCloseCallback(base::RepeatingClosure close_callback);
  void PrepareForOpen();
  void RequestNameFocus();

  // Test-only accessors/seams for interactive browser tests.
  views::Textfield* name_field_for_testing() { return name_field_; }
  views::MdTextButton* create_button_for_testing() { return create_button_; }
  views::Label* error_label_for_testing() { return error_label_; }
  views::Button* theme_button_for_testing() { return theme_button_; }
  void SetNameForTesting(const std::u16string& text);
  void CompleteCreateSpaceForTesting(bool success,
                                     const std::string& new_space_id,
                                     const std::string& profile_id = "default");

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdChecked(int command_id) const override;

 private:
  struct ProfileMenuItem {
    std::string id;
    std::u16string label;
    bool is_default = false;
    bool is_active = false;
  };

  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& event) override;
  void OnAfterUserAction(views::Textfield* sender) override;
  void ResetFormState();
  void RefreshProfiles();
  void SetSelectedProfile(const ProfileMenuItem& profile);
  void UpdateProfileButtonLabel();
  void OnProfilePressed();
  void OnIconPressed();
  void RestoreSavedNameText();
  void UpdateThemeSummary();
  void UpdateNameLeadingIconButton();
  void UpdateCreateButtonState();
  void OnCreatePressed();
  void OnCancelPressed();
  void OnThemePressed();
  void ApplyThemeSelection(const MahoSidebarCreateSpaceThemeSelection& selection);
  void OnSpaceCreated(bool success,
                      const std::string& new_space_id,
                      const std::string& profile_id);
  void ApplyPalette();

  raw_ptr<Browser> browser_;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> subtitle_label_ = nullptr;
  raw_ptr<views::View> name_row_ = nullptr;
  raw_ptr<views::Textfield> name_field_ = nullptr;
  raw_ptr<views::MdTextButton> create_button_ = nullptr;
  raw_ptr<views::Label> error_label_ = nullptr;
  raw_ptr<views::MdTextButton> cancel_button_ = nullptr;
  raw_ptr<views::View> profile_row_ = nullptr;
  raw_ptr<views::Label> profile_label_ = nullptr;
  raw_ptr<views::Button> profile_button_ = nullptr;
  raw_ptr<views::Label> profile_button_label_ = nullptr;
  raw_ptr<views::ImageView> profile_disclosure_ = nullptr;
  raw_ptr<views::Button> theme_button_ = nullptr;
  raw_ptr<views::Label> theme_label_ = nullptr;
  raw_ptr<views::ImageView> theme_disclosure_ = nullptr;
  raw_ptr<views::Button> name_leading_icon_button_ = nullptr;
  raw_ptr<views::View> theme_preview_tile_ = nullptr;
  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  MahoSidebarCreateSpaceThemeSelection theme_selection_;
  std::string selected_icon_;
  std::string selected_profile_id_;
  std::u16string selected_profile_label_;
  std::vector<ProfileMenuItem> profile_menu_items_;
  std::unique_ptr<ui::SimpleMenuModel> profile_menu_model_;
  std::unique_ptr<views::MenuRunner> profile_menu_runner_;
  std::u16string saved_name_text_;
  bool awaiting_icon_input_ = false;
  bool restoring_name_text_ = false;
  base::RepeatingClosure close_callback_;
  MahoSidebarPalette palette_;
  base::WeakPtrFactory<MahoSidebarCreateSpaceView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_CREATE_SPACE_VIEW_H_
