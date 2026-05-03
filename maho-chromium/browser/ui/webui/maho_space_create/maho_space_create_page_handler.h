#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_CREATE_MAHO_SPACE_CREATE_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_CREATE_MAHO_SPACE_CREATE_PAGE_HANDLER_H_

#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "maho/browser/ui/webui/maho_space_create/maho_space_create.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

class Browser;

namespace content {
class WebContents;
}

class MahoSpaceCreatePageHandler
    : public maho_space_create::mojom::PageHandler {
 public:
  MahoSpaceCreatePageHandler(
      mojo::PendingReceiver<maho_space_create::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_space_create::mojom::Page> page,
      Browser* browser,
      content::WebContents* web_contents);
  MahoSpaceCreatePageHandler(const MahoSpaceCreatePageHandler&) = delete;
  MahoSpaceCreatePageHandler& operator=(const MahoSpaceCreatePageHandler&) =
      delete;
  ~MahoSpaceCreatePageHandler() override;

 private:
  void GetProfiles(GetProfilesCallback callback) override;
  void GetSpaceCount(GetSpaceCountCallback callback) override;
  void CreateSpace(const std::string& name,
                   const std::string& color,
                   const std::string& icon,
                   const std::optional<std::string>& profile_id,
                   const std::optional<std::string>& theme_json,
                   CreateSpaceCallback callback) override;
  void CloseDialog() override;

  mojo::Receiver<maho_space_create::mojom::PageHandler> receiver_;
  mojo::Remote<maho_space_create::mojom::Page> page_;
  raw_ptr<Browser> browser_;
  raw_ptr<content::WebContents> web_contents_;
  base::WeakPtrFactory<MahoSpaceCreatePageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_CREATE_MAHO_SPACE_CREATE_PAGE_HANDLER_H_
