// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_CREATE_MAHO_SPACE_CREATE_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_CREATE_MAHO_SPACE_CREATE_UI_H_

#include <memory>

#include "content/public/browser/webui_config.h"
#include "maho/browser/ui/webui/maho_space_create/maho_space_create.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"

class MahoSpaceCreatePageHandler;

namespace content {
class WebUI;
}

class MahoSpaceCreateUI;

namespace content {
class BrowserContext;
}

class MahoSpaceCreateUIConfig
    : public content::DefaultWebUIConfig<MahoSpaceCreateUI> {
 public:
  MahoSpaceCreateUIConfig();
  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

class MahoSpaceCreateUI : public ui::MojoWebUIController,
                          public maho_space_create::mojom::PageHandlerFactory {
 public:
  explicit MahoSpaceCreateUI(content::WebUI* web_ui);
  MahoSpaceCreateUI(const MahoSpaceCreateUI&) = delete;
  MahoSpaceCreateUI& operator=(const MahoSpaceCreateUI&) = delete;
  ~MahoSpaceCreateUI() override;

  void BindInterface(
      mojo::PendingReceiver<maho_space_create::mojom::PageHandlerFactory>
          receiver);

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();

  void CreatePageHandler(
      mojo::PendingRemote<maho_space_create::mojom::Page> page,
      mojo::PendingReceiver<maho_space_create::mojom::PageHandler> receiver)
      override;

  std::unique_ptr<MahoSpaceCreatePageHandler> page_handler_;
  mojo::Receiver<maho_space_create::mojom::PageHandlerFactory>
      page_factory_receiver_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_CREATE_MAHO_SPACE_CREATE_UI_H_
