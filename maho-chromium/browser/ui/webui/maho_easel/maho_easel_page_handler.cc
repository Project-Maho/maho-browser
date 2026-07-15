// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_easel/maho_easel_page_handler.h"

#include <utility>
#include <vector>

#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"

MahoEaselPageHandler::MahoEaselPageHandler(
    mojo::PendingReceiver<maho_easel::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_easel::mojom::Page> page,
    BrowserWindowInterface* browser_window,
    content::WebContents* web_contents)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      context_token_(
          browser_window,
          (browser_window && web_contents &&
           browser_window->GetTabStripModel()->GetIndexOfWebContents(web_contents) != TabStripModel::kNoTab)
              ? web_contents
              : nullptr),
      host_web_contents_(web_contents ? web_contents->GetWeakPtr() : nullptr) {}

MahoEaselPageHandler::~MahoEaselPageHandler() = default;

#define EASEL_REVALIDATE_OR_RETURN(default_val)                              \
  if (!context_token_.Revalidate(                                            \
          MahoPrivateCapability::kProcessGlobalWebUI)) {                     \
    receiver_.reset();                                                       \
    page_.reset();                                                           \
    return;                                                                  \
  }

void MahoEaselPageHandler::GetCanvas(GetCanvasCallback callback) {
  EASEL_REVALIDATE_OR_RETURN(std::vector<maho_easel::mojom::CanvasItemPtr>());
  std::vector<maho_easel::mojom::CanvasItemPtr> items;

  auto i1 = maho_easel::mojom::CanvasItem::New();
  i1->id = "item-1";
  i1->type = "text";
  i1->x = 50.0;
  i1->y = 50.0;
  i1->width = 200.0;
  i1->height = 40.0;
  i1->content = "Welcome to Easel";
  items.push_back(std::move(i1));

  auto i2 = maho_easel::mojom::CanvasItem::New();
  i2->id = "item-2";
  i2->type = "text";
  i2->x = 300.0;
  i2->y = 150.0;
  i2->width = 180.0;
  i2->height = 40.0;
  i2->content = "Drag items to move them";
  items.push_back(std::move(i2));

  std::move(callback).Run(std::move(items));
}

void MahoEaselPageHandler::AddItem(const std::string& type,
                                   double x,
                                   double y,
                                   const std::string& content,
                                   AddItemCallback callback) {
  EASEL_REVALIDATE_OR_RETURN(nullptr);
  auto item = maho_easel::mojom::CanvasItem::New();
  item->id = "new-item-1";
  item->type = type;
  item->x = x;
  item->y = y;
  item->width = 150.0;
  item->height = 40.0;
  item->content = content;
  std::move(callback).Run(std::move(item));
}

void MahoEaselPageHandler::MoveItem(const std::string& id,
                                    double x,
                                    double y,
                                    MoveItemCallback callback) {
  EASEL_REVALIDATE_OR_RETURN(false);
  std::move(callback).Run(true);
}

void MahoEaselPageHandler::DeleteItem(const std::string& id,
                                      DeleteItemCallback callback) {
  EASEL_REVALIDATE_OR_RETURN(false);
  std::move(callback).Run(true);
}
