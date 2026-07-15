// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_WELCOME_MAHO_WELCOME_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_WELCOME_MAHO_WELCOME_UI_H_

#include <memory>

#include "content/public/browser/webui_config.h"
#include "maho/browser/ui/webui/maho_welcome/maho_welcome.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"

class MahoWelcomePageHandler;

namespace content {
class WebUI;
}

class MahoWelcomeUI;

namespace content {
class BrowserContext;
}

class MahoWelcomeUIConfig
    : public content::DefaultWebUIConfig<MahoWelcomeUI> {
 public:
  MahoWelcomeUIConfig();
  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

class MahoWelcomeUI : public ui::MojoWebUIController,
                      public maho_welcome::mojom::PageHandlerFactory {
 public:
  explicit MahoWelcomeUI(content::WebUI* web_ui);
  MahoWelcomeUI(const MahoWelcomeUI&) = delete;
  MahoWelcomeUI& operator=(const MahoWelcomeUI&) = delete;
  ~MahoWelcomeUI() override;

  void BindInterface(
      mojo::PendingReceiver<maho_welcome::mojom::PageHandlerFactory>
          receiver);

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();

  // maho_welcome::mojom::PageHandlerFactory:
  void CreatePageHandler(
      mojo::PendingRemote<maho_welcome::mojom::Page> page,
      mojo::PendingReceiver<maho_welcome::mojom::PageHandler> receiver)
      override;

  std::unique_ptr<MahoWelcomePageHandler> page_handler_;
  mojo::Receiver<maho_welcome::mojom::PageHandlerFactory>
      page_factory_receiver_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_WELCOME_MAHO_WELCOME_UI_H_
