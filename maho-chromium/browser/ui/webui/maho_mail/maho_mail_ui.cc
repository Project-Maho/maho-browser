// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_mail/maho_mail_ui.h"

#include <memory>
#include <utility>

#include "base/strings/strcat.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/sanitized_image/sanitized_image_source.h"
#include "chrome/grit/maho_mail_resources.h"
#include "chrome/grit/maho_mail_resources_map.h"
#include "components/performance_manager/public/decorators/page_live_state_decorator.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/webui/maho_mail/maho_mail_page_handler.h"
#include "maho/components/constants/webui_url_constants.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/webui/webui_util.h"

// MahoMailUIConfig

MahoMailUIConfig::MahoMailUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, maho::kMahoMailHost) {}

bool MahoMailUIConfig::IsWebUIEnabled(content::BrowserContext* browser_context) {
  // Mail is an opt-in Beta gated by maho.mail.enabled. New profiles default to
  // false until users enable it from Settings. With it off, chrome://maho-mail
  // is never registered.
  Profile* profile = Profile::FromBrowserContext(browser_context);
  return profile &&
         maho::sidebar_prefs::IsMahoMailEnabled(profile->GetPrefs());
}

// MahoMailUI

WEB_UI_CONTROLLER_TYPE_IMPL(MahoMailUI)

MahoMailUI::MahoMailUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui, /*enable_chrome_send=*/true) {
  Profile* profile = Profile::FromWebUI(web_ui);
  // Trusted chrome:// WebUI renderers are not allowed to fetch and decode
  // external network images directly (such loads fail with
  // ERR_BLOCKED_BY_CLIENT). Register the shared sanitizing image proxy so the
  // email body can request remote images as chrome://image?url=<original>:
  // the browser process fetches the bytes, an isolated utility process decodes
  // and re-encodes them, and only the sanitized bitmap reaches the renderer.
  content::URLDataSource::Add(profile,
                              std::make_unique<SanitizedImageSource>(profile));
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      profile, maho::kMahoMailHost);
  webui::SetupWebUIDataSource(source, kMahoMailResources,
                              IDR_MAHO_MAIL_MAIL_HTML);
  // app.ts registers a Trusted Types policy named "maho-mail-app" for its
  // innerHTML sinks. SetupWebUIDataSource only allows the default policy set,
  // so extend the trusted-types allowlist with the app's policy name; without
  // it, createPolicy() throws and the page never renders.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes,
      base::StrCat({webui::kDefaultTrustedTypesPolicies, " maho-mail-app default;"}));
  // Remote images load through the sanitizing chrome://image proxy, never as
  // direct network requests from this trusted renderer. SetupWebUIDataSource's
  // default trusted img-src already allowlists chrome://image (plus 'self',
  // data:, blob:), so it is intentionally not overridden here.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::MediaSrc,
      "media-src 'self' chrome-res: data: blob:;");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::FrameSrc,
      "frame-src 'self' chrome-res: data: blob:;");
  // Memory Saver treats chrome:// pages as discardable, and Maho runs it in
  // Aggressive mode (discard after 2h in the background). A discarded Mail tab
  // reboots the whole SPA on return: booting screen, account/folder/list
  // round-trips, then the list. Mail is a long-lived app surface (live sync,
  // unread badge), so keep its tab resident like an extension-protected tab.
  performance_manager::PageLiveStateDecorator::SetIsAutoDiscardable(
      web_ui->GetWebContents(), false);
}

MahoMailUI::~MahoMailUI() = default;

void MahoMailUI::BindInterface(
    mojo::PendingReceiver<maho_mail::mojom::PageHandlerFactory> receiver) {
  page_factory_receiver_.reset();
  page_factory_receiver_.Bind(std::move(receiver));
}

void MahoMailUI::CreatePageHandler(
    mojo::PendingRemote<maho_mail::mojom::Page> page,
    mojo::PendingReceiver<maho_mail::mojom::PageHandler> receiver) {
  auto* profile = Profile::FromWebUI(web_ui());
  page_handler_ = std::make_unique<MahoMailPageHandler>(
      std::move(receiver), std::move(page), profile, web_ui()->GetWebContents());
}
