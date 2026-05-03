#ifndef MAHO_BROWSER_UI_VIEWS_SPACE_CREATE_MAHO_SPACE_CREATE_DIALOG_H_
#define MAHO_BROWSER_UI_VIEWS_SPACE_CREATE_MAHO_SPACE_CREATE_DIALOG_H_

class Browser;

class MahoSpaceCreateDialog {
 public:
  static void Open(Browser* parent_browser);

 private:
  MahoSpaceCreateDialog() = delete;
};

#endif  // MAHO_BROWSER_UI_VIEWS_SPACE_CREATE_MAHO_SPACE_CREATE_DIALOG_H_
