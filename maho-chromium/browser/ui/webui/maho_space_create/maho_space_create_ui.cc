// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_space_create/maho_space_create_ui.h"

#include <memory>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/grit/maho_space_create_resources.h"
#include "chrome/grit/maho_space_create_resources_map.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "maho/browser/ui/views/space_create/maho_space_theme_picker_dialog.h"
#include "maho/browser/ui/webui/maho_space_create/maho_space_create_page_handler.h"
#include "maho/components/constants/webui_url_constants.h"
#include "ui/webui/webui_util.h"

#include "maho/browser/ui/webui/maho_webui_private_boundary.h"

MahoSpaceCreateUIConfig::MahoSpaceCreateUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme,
                         maho::kMahoSpaceCreateHost) {}

bool MahoSpaceCreateUIConfig::IsWebUIEnabled(content::BrowserContext* browser_context) {
  return MahoIsWebUIEnabled(browser_context);
}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoSpaceCreateUI)

MahoSpaceCreateUI::MahoSpaceCreateUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), maho::kMahoSpaceCreateHost);
  webui::SetupWebUIDataSource(source, kMahoSpaceCreateResources,
                              IDR_MAHO_SPACE_CREATE_SPACE_CREATE_HTML);
}

MahoSpaceCreateUI::~MahoSpaceCreateUI() = default;

void MahoSpaceCreateUI::BindInterface(
    mojo::PendingReceiver<maho_space_create::mojom::PageHandlerFactory>
        receiver) {
  page_factory_receiver_.reset();
  page_factory_receiver_.Bind(std::move(receiver));
}

void MahoSpaceCreateUI::CreatePageHandler(
    mojo::PendingRemote<maho_space_create::mojom::Page> page,
    mojo::PendingReceiver<maho_space_create::mojom::PageHandler> receiver) {
  Browser* browser = MahoSpaceThemePickerDialog::GetOwnerBrowserForWebContents(
      web_ui()->GetWebContents());
  if (!browser) {
    browser = static_cast<Browser*>(
        GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(
            web_ui()->GetWebContents()));
  }
  if (!browser) {
    auto* profile = Profile::FromWebUI(web_ui());
    if (profile) {
      ProfileBrowserCollection* collection =
          ProfileBrowserCollection::GetForProfile(profile);
      browser = static_cast<Browser*>(
          collection ? collection->GetLastActiveBrowser() : nullptr);
    }
  }
  if (!browser) {
    return;
  }

  page_handler_ = std::make_unique<MahoSpaceCreatePageHandler>(
      std::move(receiver), std::move(page), browser,
      web_ui()->GetWebContents());
  page_handler_->SetDialogCallbacks(
      base::BindOnce(&MahoSpaceThemePickerDialog::DispatchCommitToActive,
                     browser),
      base::BindOnce(&MahoSpaceThemePickerDialog::DispatchCancelToActive,
                     browser));
}
