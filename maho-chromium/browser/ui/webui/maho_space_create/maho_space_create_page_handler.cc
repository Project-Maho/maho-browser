// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_space_create/maho_space_create_page_handler.h"

#include <utility>

#include "base/check.h"
#include "base/sequence_checker.h"
#include "chrome/browser/ui/browser.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "ui/color/color_provider_manager.h"

MahoSpaceCreatePageHandler::MahoSpaceCreatePageHandler(
    mojo::PendingReceiver<maho_space_create::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_space_create::mojom::Page> page,
    Browser* browser,
    content::WebContents* web_contents)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      browser_(browser),
      web_contents_(web_contents) {}

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
  if (MahoSpaceThemeState::SetPreviewOverride(selection->theme_json)) {
    ui::ColorProviderManager::Get().ResetColorProviderCache();
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
