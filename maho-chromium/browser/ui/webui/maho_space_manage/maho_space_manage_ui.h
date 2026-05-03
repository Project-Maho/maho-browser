#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_MANAGE_MAHO_SPACE_MANAGE_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_MANAGE_MAHO_SPACE_MANAGE_UI_H_

#include <memory>

#include "content/public/browser/webui_config.h"
#include "maho/browser/ui/webui/maho_space_manage/maho_space_manage.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"

class MahoSpaceManagePageHandler;

namespace content {
class WebUI;
}

class MahoSpaceManageUI;

class MahoSpaceManageUIConfig
    : public content::DefaultWebUIConfig<MahoSpaceManageUI> {
 public:
  MahoSpaceManageUIConfig();
};

class MahoSpaceManageUI : public ui::MojoWebUIController,
                          public maho_space_manage::mojom::PageHandlerFactory {
 public:
  explicit MahoSpaceManageUI(content::WebUI* web_ui);
  MahoSpaceManageUI(const MahoSpaceManageUI&) = delete;
  MahoSpaceManageUI& operator=(const MahoSpaceManageUI&) = delete;
  ~MahoSpaceManageUI() override;

  void BindInterface(
      mojo::PendingReceiver<maho_space_manage::mojom::PageHandlerFactory>
          receiver);

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();

  void CreatePageHandler(
      mojo::PendingRemote<maho_space_manage::mojom::Page> page,
      mojo::PendingReceiver<maho_space_manage::mojom::PageHandler> receiver)
      override;

  std::unique_ptr<MahoSpaceManagePageHandler> page_handler_;
  mojo::Receiver<maho_space_manage::mojom::PageHandlerFactory>
      page_factory_receiver_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_MANAGE_MAHO_SPACE_MANAGE_UI_H_
