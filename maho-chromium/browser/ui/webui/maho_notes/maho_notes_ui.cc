// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_notes/maho_notes_ui.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/maho_notes_resources.h"
#include "chrome/grit/maho_notes_resources_map.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "maho/browser/ui/webui/maho_notes/maho_notes_page_handler.h"
#include "maho/components/constants/webui_url_constants.h"
#include "ui/webui/webui_util.h"

MahoNotesUIConfig::MahoNotesUIConfig()
    : DefaultTopChromeWebUIConfig(content::kChromeUIScheme,
                                  maho::kMahoNotesHost) {}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoNotesUI)

MahoNotesUI::MahoNotesUI(content::WebUI* web_ui)
    : TopChromeWebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), maho::kMahoNotesHost);
  webui::SetupWebUIDataSource(source, kMahoNotesResources,
                              IDR_MAHO_NOTES_NOTES_HTML);
}

MahoNotesUI::~MahoNotesUI() = default;

void MahoNotesUI::BindInterface(
    mojo::PendingReceiver<maho_notes::mojom::PageHandlerFactory> receiver) {
  page_factory_receiver_.reset();
  page_factory_receiver_.Bind(std::move(receiver));
}

void MahoNotesUI::CreatePageHandler(
    mojo::PendingRemote<maho_notes::mojom::Page> page,
    mojo::PendingReceiver<maho_notes::mojom::PageHandler> receiver) {
  page_handler_ = std::make_unique<MahoNotesPageHandler>(
      std::move(receiver), std::move(page));
}
