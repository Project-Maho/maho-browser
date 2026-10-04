// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_space_create/maho_space_create_page_handler.h"

#include "maho/browser/ui/webui/maho_webui_private_boundary.h"

#include <utility>

#include "base/check.h"
#include "base/sequence_checker.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/browser/ui/views/space_create/maho_space_theme_picker_dialog.h"
#include "ui/color/color_provider_manager.h"
#include "ui/views/widget/root_view.h"
#include "ui/views/widget/widget.h"

MahoSpaceCreatePageHandler::MahoSpaceCreatePageHandler(
    mojo::PendingReceiver<maho_space_create::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_space_create::mojom::Page> page,
    Browser* browser,
    content::WebContents* web_contents)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      browser_(browser),
      web_contents_(web_contents) {
  // R-4 fail-closed: forged Mojo receiver bypasses config-level denial. Do not remove.
  if (web_contents &&
      !MahoIsWebUIEnabled(web_contents->GetBrowserContext())) {
    receiver_.reset();
    page_.reset();
    return;
  }
  // Deliver the initial theme selection (or empty default) to the WebUI so
  // its React store can transition out of the loading=true state.
  auto initial = maho_space_create::mojom::ThemeSelection::New();
  initial->theme_json = MahoSpaceThemePickerDialog::ConsumePendingInitialThemeJson(
      browser_, web_contents_);
  page_->Initialize(std::move(initial));
}

MahoSpaceCreatePageHandler::~MahoSpaceCreatePageHandler() {
  if (MahoSpaceThemeState::HasPreviewOverride(browser_)) {
    MahoSpaceThemeState::ClearPreviewOverride(browser_);
    MahoSpaceThemeState::UpdateFromCore();
    MahoSpaceThemePickerDialog::RepaintBrowser(browser_);
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
  bool ok =
      MahoSpaceThemeState::SetPreviewOverride(browser_, selection->theme_json);
  if (ok) {
    MahoSpaceThemePickerDialog::RepaintBrowser(browser_);
  }
}

void MahoSpaceCreatePageHandler::CommitTheme(
    maho_space_create::mojom::ThemeSelectionPtr selection) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Persistence + UpdateFromCore + repaint happen in the dialog's result
  // callback (the single source of truth). Reading core here would repaint
  // with the pre-persist (stale) theme; leaving the preview override in place
  // keeps the chosen theme visible until the commit lands.
  if (commit_callback_) {
    std::move(commit_callback_).Run(selection->theme_json);
  }
}

void MahoSpaceCreatePageHandler::CancelTheme() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MahoSpaceThemeState::ClearPreviewOverride(browser_);
  MahoSpaceThemeState::UpdateFromCore();
  MahoSpaceThemePickerDialog::RepaintBrowser(browser_);
  if (cancel_callback_) {
    std::move(cancel_callback_).Run();
  }
}
