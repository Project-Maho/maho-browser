// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SPACE_CREATE_MAHO_SPACE_THEME_PICKER_DIALOG_H_
#define MAHO_BROWSER_UI_VIEWS_SPACE_CREATE_MAHO_SPACE_THEME_PICKER_DIALOG_H_

#include <string>

#include "base/functional/callback.h"

class Browser;

namespace views {
class Widget;
}  // namespace views

class MahoSpaceThemePickerDialog {
 public:
  // Callback fired when user clicks Done (cancelled=false, theme_json set) or
  // Cancel (cancelled=true, theme_json empty). Invoked at most once before the
  // widget closes.
  using ResultCallback =
      base::OnceCallback<void(const std::string& committed_theme_json,
                              bool cancelled)>;

  // Opens (or focuses) the singleton theme picker dialog parented to `browser`.
  // `initial_theme_json` is delivered to the WebUI via Page::Initialize.
  // `on_result` fires once with either committed JSON (cancelled=false) or
  // empty string (cancelled=true).
  static views::Widget* Open(Browser* browser,
                             const std::string& initial_theme_json,
                             ResultCallback on_result);

  // Test/diagnostic helper — returns widget if currently open, else nullptr.
  static views::Widget* GetActiveWidgetForTesting();

  // Called by MahoSpaceCreatePageHandler from its CommitTheme Mojo handler.
  // Triggers the result callback with committed_=true and closes the widget.
  static void DispatchCommitToActive(const std::string& theme_json);

  // Called by MahoSpaceCreatePageHandler from its CancelTheme Mojo handler.
  // Triggers the result callback with cancelled=true and closes the widget.
  static void DispatchCancelToActive();

  // Called by maho_space_create_ui.cc's CreatePageHandler so the handler can
  // deliver the initial theme_json via Page::Initialize after it binds.
  // Returns the stored initial_theme_json and clears it (consume-once).
  static std::string ConsumePendingInitialThemeJsonForActive();

 private:
  MahoSpaceThemePickerDialog() = delete;
};

#endif  // MAHO_BROWSER_UI_VIEWS_SPACE_CREATE_MAHO_SPACE_THEME_PICKER_DIALOG_H_
