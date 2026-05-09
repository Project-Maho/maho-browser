#ifndef MAHO_BROWSER_UI_VIEWS_SPACE_CREATE_MAHO_SPACE_CREATE_DIALOG_H_
#define MAHO_BROWSER_UI_VIEWS_SPACE_CREATE_MAHO_SPACE_CREATE_DIALOG_H_

class Browser;

namespace views {
class Widget;
}  // namespace views

namespace maho {
class MahoSidebarCreateSpaceView;
}  // namespace maho

class MahoSpaceCreateDialog {
 public:
  static void Open(Browser* parent_browser);
  static views::Widget* GetWidgetForTesting();
  static maho::MahoSidebarCreateSpaceView* GetCreateSpaceViewForTesting();

 private:
  MahoSpaceCreateDialog() = delete;
};

#endif  // MAHO_BROWSER_UI_VIEWS_SPACE_CREATE_MAHO_SPACE_CREATE_DIALOG_H_
