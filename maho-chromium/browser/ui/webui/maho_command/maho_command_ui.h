// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_COMMAND_MAHO_COMMAND_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_COMMAND_MAHO_COMMAND_UI_H_

#include <memory>

#include "content/public/browser/webui_config.h"
#include "maho/browser/ui/webui/maho_command/maho_command.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"

class MahoCommandPageHandler;

namespace content {
class WebUI;
}

class MahoCommandUI;

class MahoCommandUIConfig : public content::DefaultWebUIConfig<MahoCommandUI> {
 public:
  MahoCommandUIConfig();
};

class MahoCommandUI : public ui::MojoWebUIController,
                      public maho_command::mojom::PageHandlerFactory {
 public:
  explicit MahoCommandUI(content::WebUI* web_ui);
  MahoCommandUI(const MahoCommandUI&) = delete;
  MahoCommandUI& operator=(const MahoCommandUI&) = delete;
  ~MahoCommandUI() override;

  void BindInterface(
      mojo::PendingReceiver<maho_command::mojom::PageHandlerFactory> receiver);

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();

  // maho_command::mojom::PageHandlerFactory:
  void CreatePageHandler(
      mojo::PendingRemote<maho_command::mojom::Page> page,
      mojo::PendingReceiver<maho_command::mojom::PageHandler> receiver)
      override;

  std::unique_ptr<MahoCommandPageHandler> page_handler_;
  mojo::Receiver<maho_command::mojom::PageHandlerFactory>
      page_factory_receiver_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_COMMAND_MAHO_COMMAND_UI_H_
