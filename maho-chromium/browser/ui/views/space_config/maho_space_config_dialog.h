#ifndef MAHO_BROWSER_UI_VIEWS_SPACE_CONFIG_MAHO_SPACE_CONFIG_DIALOG_H_
#define MAHO_BROWSER_UI_VIEWS_SPACE_CONFIG_MAHO_SPACE_CONFIG_DIALOG_H_

#include <string>

#include "maho/browser/ui/webui/maho_space_config/maho_space_config.mojom.h"
#include "ui/gfx/native_ui_types.h"

class Profile;

namespace views {
class Widget;
}  // namespace views

class MahoSpaceConfigDialog {
 public:
  enum class ActiveMode {
    kNone,
    kEdit,
  };

  static void Open(Profile* profile,
                   gfx::NativeWindow parent_window,
                   const std::string& space_id,
                   maho_space_config::mojom::InitialFocus focus);

  static void CloseActiveDialog();

  static ActiveMode GetActiveModeForTesting();

  static views::Widget* GetWidgetForTesting();

 private:
  MahoSpaceConfigDialog() = delete;
};

#endif  // MAHO_BROWSER_UI_VIEWS_SPACE_CONFIG_MAHO_SPACE_CONFIG_DIALOG_H_
