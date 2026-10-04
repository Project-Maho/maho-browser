// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_WELCOME_MAHO_WELCOME_WINDOW_H_
#define MAHO_BROWSER_UI_VIEWS_WELCOME_MAHO_WELCOME_WINDOW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/views/widget/widget_observer.h"

class Profile;
class ScopedKeepAlive;

namespace content {
class WebContents;
}

namespace views {
class Widget;
}

namespace maho {

// Frameless top-level Widget that hosts the chrome://maho-welcome/ WebUI.
// Created at startup when first-run welcome is required, instead of using
// Browser::CreateForApp.  Borderless on all platforms (no title bar, no
// traffic lights, no info bars), so the React onboarding shell fills the
// window edge-to-edge.
//
// Singleton: only one welcome window per process; subsequent Show() calls
// raise the existing one.  Closing the widget destroys the singleton.
class MahoWelcomeWindow : public views::WidgetObserver {
 public:
  MahoWelcomeWindow(const MahoWelcomeWindow&) = delete;
  MahoWelcomeWindow& operator=(const MahoWelcomeWindow&) = delete;

  // Create + show the welcome window.  If one already exists, activates it.
  static void Show(Profile* profile);
  static bool ActivateIfOpen();
  static content::WebContents* GetHostedWebContents();
  static content::WebContents* GetByokSettingsDialogWebContents();

  // Close the active welcome window if any.  Safe to call when none exists.
  static void CloseInstance();

  // Look up the window hosting the given WebContents (used by the page
  // handler to ask its hosting window to close).  Returns nullptr if the
  // contents is not the active welcome WebUI.
  static MahoWelcomeWindow* FromWebContents(content::WebContents* web_contents);

  // Returns the underlying Widget so callers can parent dialogs to it.
  views::Widget* GetWidget();

  // Closes the widget.
  void Close();

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

  ~MahoWelcomeWindow() override;

 private:
  explicit MahoWelcomeWindow(Profile* profile);

  void CreateAndShowWidget();

  raw_ptr<Profile> profile_;
  std::unique_ptr<ScopedKeepAlive> keep_alive_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<content::WebContents> web_contents_ = nullptr;

  base::WeakPtrFactory<MahoWelcomeWindow> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_WELCOME_MAHO_WELCOME_WINDOW_H_
