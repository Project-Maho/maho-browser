// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_UI_H_

#include <memory>

#include "content/public/browser/webui_config.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"

class MahoSettingsPageHandler;

namespace content {
class WebUI;
}

class MahoSettingsUI;

namespace content {
class BrowserContext;
}

class MahoSettingsUIConfig
    : public content::DefaultWebUIConfig<MahoSettingsUI> {
 public:
  MahoSettingsUIConfig();
  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

class MahoSettingsUI : public ui::MojoWebUIController,
                       public maho_settings::mojom::PageHandlerFactory {
 public:
  explicit MahoSettingsUI(content::WebUI* web_ui);
  MahoSettingsUI(const MahoSettingsUI&) = delete;
  MahoSettingsUI& operator=(const MahoSettingsUI&) = delete;
  ~MahoSettingsUI() override;

  void BindInterface(
      mojo::PendingReceiver<maho_settings::mojom::PageHandlerFactory>
          receiver);

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();

  // maho_settings::mojom::PageHandlerFactory:
  void CreatePageHandler(
      mojo::PendingRemote<maho_settings::mojom::Page> page,
      mojo::PendingReceiver<maho_settings::mojom::PageHandler> receiver)
      override;

  std::unique_ptr<MahoSettingsPageHandler> page_handler_;
  mojo::Receiver<maho_settings::mojom::PageHandlerFactory>
      page_factory_receiver_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SETTINGS_MAHO_SETTINGS_UI_H_
