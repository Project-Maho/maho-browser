// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_space_create/maho_space_create_page_handler.h"

#include <utility>

#include "base/check.h"
#include "base/logging.h"
#include "base/sequence_checker.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/browser/ui/views/space_create/maho_space_theme_picker_dialog.h"
#include "ui/color/color_provider_manager.h"
#include "ui/views/widget/root_view.h"
#include "ui/views/widget/widget.h"

namespace {

void RepaintBrowserAfterThemeStateChange(Browser* browser) {
  ui::ColorProviderManager::Get().ResetColorProviderCache();
  LOG(ERROR) << "MahoThemeDebug Repaint browser=" << browser;
  if (!browser) return;
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  LOG(ERROR) << "MahoThemeDebug browser_view=" << browser_view;
  if (!browser_view) return;
  auto* widget = browser_view->GetWidget();
  LOG(ERROR) << "MahoThemeDebug widget=" << widget
             << " closed=" << (widget ? widget->IsClosed() : true);
  if (!widget || widget->IsClosed()) return;
  auto* root_view = static_cast<views::internal::RootView*>(
      widget->GetRootView());
  LOG(ERROR) << "MahoThemeDebug root_view=" << root_view;
  if (root_view) {
    root_view->ThemeChanged();
    LOG(ERROR) << "MahoThemeDebug RootView::ThemeChanged returned";
  }
  widget->NotifyColorProviderChanged();
  LOG(ERROR) << "MahoThemeDebug NotifyColorProviderChanged returned";
}

}  // namespace

MahoSpaceCreatePageHandler::MahoSpaceCreatePageHandler(
    mojo::PendingReceiver<maho_space_create::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_space_create::mojom::Page> page,
    Browser* browser,
    content::WebContents* web_contents)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      browser_(browser),
      web_contents_(web_contents) {
  // Deliver the initial theme selection (or empty default) to the WebUI so
  // its React store can transition out of the loading=true state. When opened
  // through MahoSpaceThemePickerDialog, the dialog wrapper stages the parent
  // form's current theme_json into a global slot that we consume here. When
  // opened directly as a tab (no parent dialog), the slot is empty and the
  // store falls back to defaults.
  auto initial = maho_space_create::mojom::ThemeSelection::New();
  initial->theme_json =
      MahoSpaceThemePickerDialog::ConsumePendingInitialThemeJsonForActive();
  page_->Initialize(std::move(initial));
}

MahoSpaceCreatePageHandler::~MahoSpaceCreatePageHandler() {
  if (MahoSpaceThemeState::HasPreviewOverride()) {
    MahoSpaceThemeState::ClearPreviewOverride();
    MahoSpaceThemeState::UpdateFromCore();
    ui::ColorProviderManager::Get().ResetColorProviderCache();
  }
}

void MahoSpaceCreatePageHandler::SetDialogCallbacks(
    CommitCallback on_commit,
    CancelCallback on_cancel) {
  commit_callback_ = std::move(on_commit);
  cancel_callback_ = std::move(on_cancel);
}

void MahoSpaceCreatePageHandler::PreviewTheme(
    maho_space_create::mojom::ThemeSelectionPtr selection) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  bool ok = MahoSpaceThemeState::SetPreviewOverride(selection->theme_json);
  LOG(ERROR) << "MahoThemeDebug PreviewTheme ok=" << ok
             << " json_len=" << selection->theme_json.size();
  if (ok) {
    RepaintBrowserAfterThemeStateChange(browser_);
  }
}

void MahoSpaceCreatePageHandler::CommitTheme(
    maho_space_create::mojom::ThemeSelectionPtr selection) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoSpaceThemeState::ClearPreviewOverride();
  MahoSpaceThemeState::UpdateFromCore();
  ui::ColorProviderManager::Get().ResetColorProviderCache();
  if (commit_callback_) {
    std::move(commit_callback_).Run(selection->theme_json);
  }
}

void MahoSpaceCreatePageHandler::CancelTheme() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoSpaceThemeState::ClearPreviewOverride();
  MahoSpaceThemeState::UpdateFromCore();
  ui::ColorProviderManager::Get().ResetColorProviderCache();
  if (cancel_callback_) {
    std::move(cancel_callback_).Run();
  }
}
