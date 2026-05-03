#ifndef MAHO_BROWSER_UI_VIEWS_SPACE_MANAGE_MAHO_SPACE_MANAGE_DIALOG_H_
#define MAHO_BROWSER_UI_VIEWS_SPACE_MANAGE_MAHO_SPACE_MANAGE_DIALOG_H_

#include "chrome/browser/ui/browser.h"

namespace maho {

class MahoSpaceManageDialog {
 public:
  static void Open(Browser* parent_browser);

 private:
  MahoSpaceManageDialog() = delete;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SPACE_MANAGE_MAHO_SPACE_MANAGE_DIALOG_H_
