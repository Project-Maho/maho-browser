#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_CONFIG_MAHO_SPACE_CONFIG_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_CONFIG_MAHO_SPACE_CONFIG_PAGE_HANDLER_H_

#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "maho/browser/ui/webui/maho_space_config/maho_space_config.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

class Browser;

namespace content {
class WebContents;
}

class MahoSpaceConfigPageHandler
    : public maho_space_config::mojom::PageHandler {
 public:
  static maho_space_config::mojom::SpaceInfoPtr ParseSpaceInfoFromDictForTesting(
      const base::DictValue& dict);
  static maho_space_config::mojom::ProfileInfoPtr
  ParseProfileInfoFromDictForTesting(const base::DictValue& dict);
  static std::optional<std::string> BuildProfileUpdateEventJsonForTesting(
      const std::string& space_id,
      const std::optional<std::string>& profile_id);
  static bool CoreAcceptedSpaceConfigUpdateForTesting(
      const std::string& result_json,
      const std::string& expected_space_id,
      const std::string& expected_profile_id);

  MahoSpaceConfigPageHandler(
      mojo::PendingReceiver<maho_space_config::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_space_config::mojom::Page> page,
      Browser* browser,
      content::WebContents* web_contents,
      const std::string& space_id,
      maho_space_config::mojom::InitialFocus initial_focus);
  MahoSpaceConfigPageHandler(const MahoSpaceConfigPageHandler&) = delete;
  MahoSpaceConfigPageHandler& operator=(const MahoSpaceConfigPageHandler&) =
      delete;
  ~MahoSpaceConfigPageHandler() override;

 private:
  void GetSpaceInfo(GetSpaceInfoCallback callback) override;
  void GetProfiles(GetProfilesCallback callback) override;
  void UpdateName(const std::string& name,
                  UpdateNameCallback callback) override;
  void UpdateIcon(const std::string& icon,
                  UpdateIconCallback callback) override;
  void UpdateColor(const std::string& color_hex,
                   const std::optional<std::string>& theme_json,
                   UpdateColorCallback callback) override;
  void UpdateProfile(const std::optional<std::string>& profile_id,
                     UpdateProfileCallback callback) override;
  void CloseDialog() override;

  void RefreshSidebar();

  mojo::Receiver<maho_space_config::mojom::PageHandler> receiver_;
  mojo::Remote<maho_space_config::mojom::Page> page_;
  raw_ptr<Browser> browser_;
  raw_ptr<content::WebContents> web_contents_;
  std::string space_id_;
  maho_space_config::mojom::InitialFocus initial_focus_;
  std::optional<std::string> cached_theme_json_;
  base::WeakPtrFactory<MahoSpaceConfigPageHandler> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_CONFIG_MAHO_SPACE_CONFIG_PAGE_HANDLER_H_
