#ifndef MAHO_BROWSER_UI_VIEWS_MAHO_SPACE_DELETE_CONFIRMATION_DIALOG_H_
#define MAHO_BROWSER_UI_VIEWS_MAHO_SPACE_DELETE_CONFIRMATION_DIALOG_H_

#include <string>

class Browser;

class MahoSpaceDeleteConfirmationDialog {
 public:
  static void Show(Browser* browser, const std::string& space_id);

 private:
  MahoSpaceDeleteConfirmationDialog() = delete;
};

#endif  // MAHO_BROWSER_UI_VIEWS_MAHO_SPACE_DELETE_CONFIRMATION_DIALOG_H_
