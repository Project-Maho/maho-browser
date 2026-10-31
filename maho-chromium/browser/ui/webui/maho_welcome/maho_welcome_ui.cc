// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_welcome/maho_welcome_ui.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"                                          // nogncheck
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"  // nogncheck
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"  // nogncheck
#include "chrome/browser/ui/webui/favicon_source.h"  // nogncheck
#include "chrome/grit/maho_welcome_resources.h"
#include "chrome/grit/maho_welcome_resources_map.h"
#include "components/favicon_base/favicon_url_parser.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "maho/browser/ui/webui/maho_welcome/maho_welcome_page_handler.h"
#include "maho/components/constants/webui_url_constants.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/webui/webui_util.h"

#include "maho/browser/ui/webui/maho_webui_private_boundary.h"

MahoWelcomeUIConfig::MahoWelcomeUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, maho::kMahoWelcomeHost) {}

bool MahoWelcomeUIConfig::IsWebUIEnabled(content::BrowserContext* browser_context) {
  return MahoIsWebUIEnabled(browser_context);
}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoWelcomeUI)

MahoWelcomeUI::MahoWelcomeUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui, /*enable_chrome_send=*/true) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      profile, maho::kMahoWelcomeHost);
  webui::SetupWebUIDataSource(source, kMahoWelcomeResources,
                               IDR_MAHO_WELCOME_WELCOME_HTML);
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ScriptSrc,
      "script-src chrome://resources 'self' https://app.lemonsqueezy.com;");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::FrameSrc,
      "frame-src chrome://maho-space-create https://*.lemonsqueezy.com "
      "https://lemonsqueezy.com;");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ConnectSrc,
      "connect-src 'self' https://api.lemonsqueezy.com;");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes,
      "trusted-types default static-types;");
  content::URLDataSource::Add(
      profile,
      std::make_unique<FaviconSource>(profile,
                                      chrome::FaviconUrlFormat::kFavicon2));
}

MahoWelcomeUI::~MahoWelcomeUI() = default;

void MahoWelcomeUI::BindInterface(
    mojo::PendingReceiver<maho_welcome::mojom::PageHandlerFactory> receiver) {
  page_factory_receiver_.reset();
  page_factory_receiver_.Bind(std::move(receiver));
}

void MahoWelcomeUI::CreatePageHandler(
    mojo::PendingRemote<maho_welcome::mojom::Page> page,
    mojo::PendingReceiver<maho_welcome::mojom::PageHandler> receiver) {
  content::WebContents* web_contents = web_ui()->GetWebContents();
  Browser* browser = static_cast<Browser*>(
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(web_contents));
  page_handler_ = std::make_unique<MahoWelcomePageHandler>(
      std::move(receiver), std::move(page),
      Profile::FromWebUI(web_ui()), browser, web_contents);
}
