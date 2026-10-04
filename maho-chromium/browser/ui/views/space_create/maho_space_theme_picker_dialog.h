// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SPACE_CREATE_MAHO_SPACE_THEME_PICKER_DIALOG_H_
#define MAHO_BROWSER_UI_VIEWS_SPACE_CREATE_MAHO_SPACE_THEME_PICKER_DIALOG_H_

#include <string>

#include "base/functional/callback.h"

class Browser;

namespace content {
class WebContents;
}  // namespace content

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

  // Opens (or focuses) the theme picker dialog owned by `browser`. At most one
  // picker is active for each browser; pickers in separate browsers are
  // intentionally independent.
  // `initial_theme_json` is delivered to the WebUI via Page::Initialize.
  // `on_result` fires once with either committed JSON (cancelled=false) or
  // empty string (cancelled=true).
  static views::Widget* Open(Browser* browser,
                             const std::string& initial_theme_json,
                             ResultCallback on_result);

  // Test/diagnostic helper — returns an open widget, if any.
  static views::Widget* GetActiveWidgetForTesting();

  // Called by MahoSpaceCreatePageHandler from its CommitTheme Mojo handler.
  // Triggers the result callback with committed_=true and closes the widget.
  static void DispatchCommitToActive(Browser* browser,
                                     const std::string& theme_json);

  // Called by MahoSpaceCreatePageHandler from its CancelTheme Mojo handler.
  // Triggers the result callback with cancelled=true and closes the widget.
  static void DispatchCancelToActive(Browser* browser);

  // Called by maho_space_create_ui.cc's CreatePageHandler so the handler can
  // deliver the initial theme_json via Page::Initialize after it binds.
  // Returns the owning dialog's initial theme JSON and clears it
  // (consume-once). A WebContents must belong to that dialog.
  static std::string ConsumePendingInitialThemeJson(
      Browser* browser,
      content::WebContents* web_contents);

  static void RepaintBrowser(Browser* browser);

  // Returns the browser that owns the picker hosting `web_contents`, or null
  // for a standalone chrome://maho-space-create tab.
  static Browser* GetOwnerBrowserForWebContents(
      content::WebContents* web_contents);

 private:
  MahoSpaceThemePickerDialog() = delete;
};

#endif  // MAHO_BROWSER_UI_VIEWS_SPACE_CREATE_MAHO_SPACE_THEME_PICKER_DIALOG_H_
