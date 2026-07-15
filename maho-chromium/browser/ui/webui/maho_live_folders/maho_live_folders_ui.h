// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_LIVE_FOLDERS_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_LIVE_FOLDERS_UI_H_

#include <memory>

#include "content/public/browser/webui_config.h"
#include "maho/browser/ui/webui/maho_live_folders/maho_live_folders.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"

class MahoLiveFolderPageHandler;

namespace content {
class WebUI;
}

class MahoLiveFoldersUI;

namespace content {
class BrowserContext;
}

class MahoLiveFoldersUIConfig
    : public content::DefaultWebUIConfig<MahoLiveFoldersUI> {
 public:
  MahoLiveFoldersUIConfig();
  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

class MahoLiveFoldersUI
    : public ui::MojoWebUIController,
      public maho_live_folders::mojom::PageHandlerFactory {
 public:
  explicit MahoLiveFoldersUI(content::WebUI* web_ui);
  MahoLiveFoldersUI(const MahoLiveFoldersUI&) = delete;
  MahoLiveFoldersUI& operator=(const MahoLiveFoldersUI&) = delete;
  ~MahoLiveFoldersUI() override;

  void BindInterface(
      mojo::PendingReceiver<maho_live_folders::mojom::PageHandlerFactory>
          receiver);

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();

  // maho_live_folders::mojom::PageHandlerFactory:
  void CreatePageHandler(
      mojo::PendingRemote<maho_live_folders::mojom::Page> page,
      mojo::PendingReceiver<maho_live_folders::mojom::PageHandler> receiver)
      override;

  std::unique_ptr<MahoLiveFolderPageHandler> page_handler_;
  mojo::Receiver<maho_live_folders::mojom::PageHandlerFactory>
      page_factory_receiver_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_LIVE_FOLDERS_UI_H_
