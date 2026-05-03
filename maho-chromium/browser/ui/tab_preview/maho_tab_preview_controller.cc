// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/tab_preview/maho_tab_preview_controller.h"

#include "base/logging.h"

MahoTabPreviewController::MahoTabPreviewController(Browser* browser)
    : browser_(browser) {}

MahoTabPreviewController::~MahoTabPreviewController() = default;

void MahoTabPreviewController::ShowPreview(int tab_index) {
  LOG(INFO) << "MahoTabPreviewController::ShowPreview index=" << tab_index
            << " browser=" << browser_.get();
}

void MahoTabPreviewController::HidePreview() {
  LOG(INFO) << "MahoTabPreviewController::HidePreview";
}
