// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_MAIL_MAHO_MAIL_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_MAIL_MAHO_MAIL_UI_H_

#include <memory>

#include "content/public/browser/webui_config.h"
#include "maho/browser/ui/webui/maho_mail/maho_mail.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"

class Profile;
class MahoMailPageHandler;

class MahoMailUI;

class MahoMailUIConfig : public content::DefaultWebUIConfig<MahoMailUI> {
 public:
  MahoMailUIConfig();
  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

class MahoMailUI : public ui::MojoWebUIController,
                   public maho_mail::mojom::PageHandlerFactory {
 public:
  explicit MahoMailUI(content::WebUI* web_ui);

  MahoMailUI(const MahoMailUI&) = delete;
  MahoMailUI& operator=(const MahoMailUI&) = delete;

  ~MahoMailUI() override;

  void BindInterface(
      mojo::PendingReceiver<maho_mail::mojom::PageHandlerFactory> receiver);

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();

  void CreatePageHandler(
      mojo::PendingRemote<maho_mail::mojom::Page> page,
      mojo::PendingReceiver<maho_mail::mojom::PageHandler> receiver) override;

  std::unique_ptr<MahoMailPageHandler> page_handler_;
  mojo::Receiver<maho_mail::mojom::PageHandlerFactory>
      page_factory_receiver_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_MAIL_MAHO_MAIL_UI_H_
