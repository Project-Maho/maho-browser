// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_settings/maho_settings_ui.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/grit/maho_settings_resources.h"
#include "chrome/grit/maho_settings_resources_map.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings_page_handler.h"
#include "maho/components/constants/webui_url_constants.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/webui/webui_util.h"

#include "maho/browser/ui/webui/maho_webui_private_boundary.h"

MahoSettingsUIConfig::MahoSettingsUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, maho::kMahoSettingsHost) {}

bool MahoSettingsUIConfig::IsWebUIEnabled(content::BrowserContext* browser_context) {
  return MahoIsWebUIEnabled(browser_context);
}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoSettingsUI)

MahoSettingsUI::MahoSettingsUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui, /*enable_chrome_send=*/true) {
  extensions::TabHelper::CreateForWebContents(web_ui->GetWebContents());
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), maho::kMahoSettingsHost);
  webui::SetupWebUIDataSource(source, kMahoSettingsResources,
                               IDR_MAHO_SETTINGS_SETTINGS_HTML);
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ChildSrc,
      "child-src 'self' chrome://settings chrome://settings/passwords;");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes,
      "trusted-types default static-types;");
  // LemonSqueezy overlay checkout (P1-A): allow loading lemon.js from LS CDN,
  // embedding the LS checkout iframe, and connecting to the LS API for signed
  // checkout URL creation.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ScriptSrc,
      "script-src chrome://resources 'self' https://app.lemonsqueezy.com;");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::FrameSrc,
      "frame-src https://*.lemonsqueezy.com https://lemonsqueezy.com "
      "chrome://settings chrome://settings/passwords;");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ConnectSrc,
      "connect-src 'self' https://api.lemonsqueezy.com;");
}

MahoSettingsUI::~MahoSettingsUI() = default;

void MahoSettingsUI::BindInterface(
    mojo::PendingReceiver<maho_settings::mojom::PageHandlerFactory> receiver) {
  page_factory_receiver_.reset();
  page_factory_receiver_.Bind(std::move(receiver));
}

void MahoSettingsUI::CreatePageHandler(
    mojo::PendingRemote<maho_settings::mojom::Page> page,
    mojo::PendingReceiver<maho_settings::mojom::PageHandler> receiver) {
  page_handler_ = std::make_unique<MahoSettingsPageHandler>(
      std::move(receiver), std::move(page), Profile::FromWebUI(web_ui()),
      web_ui()->GetWebContents());
}
