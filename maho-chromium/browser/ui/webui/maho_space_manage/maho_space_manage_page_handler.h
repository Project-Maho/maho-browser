#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_MANAGE_MAHO_SPACE_MANAGE_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_MANAGE_MAHO_SPACE_MANAGE_PAGE_HANDLER_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "maho/browser/ui/webui/maho_space_manage/maho_space_manage.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

class Browser;

namespace content {
class WebContents;
}

class MahoSpaceManagePageHandler
    : public maho_space_manage::mojom::PageHandler {
 public:
  MahoSpaceManagePageHandler(
      mojo::PendingReceiver<maho_space_manage::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_space_manage::mojom::Page> page,
      Browser* browser,
      content::WebContents* web_contents);
  MahoSpaceManagePageHandler(const MahoSpaceManagePageHandler&) = delete;
  MahoSpaceManagePageHandler& operator=(const MahoSpaceManagePageHandler&) =
      delete;
  ~MahoSpaceManagePageHandler() override;

 private:
  void GetSpaces(GetSpacesCallback callback) override;
  void SwitchToSpace(const std::string& space_id,
                     SwitchToSpaceCallback callback) override;
  void OpenCreateSpace() override;
  void OpenSpaceConfig(const std::string& space_id) override;
  void CloseDialog() override;

  mojo::Receiver<maho_space_manage::mojom::PageHandler> receiver_;
  mojo::Remote<maho_space_manage::mojom::Page> page_;
  raw_ptr<Browser> browser_;
  raw_ptr<content::WebContents> web_contents_;
  base::WeakPtrFactory<MahoSpaceManagePageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_MANAGE_MAHO_SPACE_MANAGE_PAGE_HANDLER_H_
