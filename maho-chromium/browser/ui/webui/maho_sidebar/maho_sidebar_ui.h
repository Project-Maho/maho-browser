// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SIDEBAR_MAHO_SIDEBAR_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SIDEBAR_MAHO_SIDEBAR_UI_H_

#include <memory>

#include "content/public/browser/webui_config.h"
#include "maho/browser/ui/webui/maho_sidebar/maho_sidebar.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"

class MahoSidebarPageHandler;

namespace content {
class WebUI;
}

class MahoSidebarUI;

class MahoSidebarUIConfig : public content::DefaultWebUIConfig<MahoSidebarUI> {
 public:
  MahoSidebarUIConfig();
};

class MahoSidebarUI : public ui::MojoWebUIController,
                      public maho_sidebar::mojom::PageHandlerFactory {
 public:
  explicit MahoSidebarUI(content::WebUI* web_ui);
  MahoSidebarUI(const MahoSidebarUI&) = delete;
  MahoSidebarUI& operator=(const MahoSidebarUI&) = delete;
  ~MahoSidebarUI() override;

  void BindInterface(
      mojo::PendingReceiver<maho_sidebar::mojom::PageHandlerFactory> receiver);

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();

  // maho_sidebar::mojom::PageHandlerFactory:
  void CreatePageHandler(
      mojo::PendingRemote<maho_sidebar::mojom::Page> page,
      mojo::PendingReceiver<maho_sidebar::mojom::PageHandler> receiver)
      override;

  std::unique_ptr<MahoSidebarPageHandler> page_handler_;
  mojo::Receiver<maho_sidebar::mojom::PageHandlerFactory>
      page_factory_receiver_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SIDEBAR_MAHO_SIDEBAR_UI_H_
