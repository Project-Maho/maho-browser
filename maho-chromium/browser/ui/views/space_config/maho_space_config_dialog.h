#ifndef MAHO_BROWSER_UI_VIEWS_SPACE_CONFIG_MAHO_SPACE_CONFIG_DIALOG_H_
#define MAHO_BROWSER_UI_VIEWS_SPACE_CONFIG_MAHO_SPACE_CONFIG_DIALOG_H_

#include <string>

#include "maho/browser/ui/webui/maho_space_config/maho_space_config.mojom.h"

class Browser;

class MahoSpaceConfigDialog {
 public:
  static void Open(Browser* parent_browser,
                   const std::string& space_id,
                   maho_space_config::mojom::InitialFocus focus);

 private:
  MahoSpaceConfigDialog() = delete;
};

#endif  // MAHO_BROWSER_UI_VIEWS_SPACE_CONFIG_MAHO_SPACE_CONFIG_DIALOG_H_
