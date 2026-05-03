// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_EASEL_MAHO_EASEL_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_EASEL_MAHO_EASEL_PAGE_HANDLER_H_

#include <string>

#include "maho/browser/ui/webui/maho_easel/maho_easel.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

class MahoEaselPageHandler : public maho_easel::mojom::PageHandler {
 public:
  MahoEaselPageHandler(
      mojo::PendingReceiver<maho_easel::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_easel::mojom::Page> page);
  MahoEaselPageHandler(const MahoEaselPageHandler&) = delete;
  MahoEaselPageHandler& operator=(const MahoEaselPageHandler&) = delete;
  ~MahoEaselPageHandler() override;

  void GetCanvas(GetCanvasCallback callback) override;
  void AddItem(const std::string& type,
               double x,
               double y,
               const std::string& content,
               AddItemCallback callback) override;
  void MoveItem(const std::string& id,
                double x,
                double y,
                MoveItemCallback callback) override;
  void DeleteItem(const std::string& id,
                  DeleteItemCallback callback) override;

 private:
  mojo::Receiver<maho_easel::mojom::PageHandler> receiver_;
  mojo::Remote<maho_easel::mojom::Page> page_;
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_EASEL_MAHO_EASEL_PAGE_HANDLER_H_
