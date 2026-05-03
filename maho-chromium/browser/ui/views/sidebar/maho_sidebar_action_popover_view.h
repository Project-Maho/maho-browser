#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_ACTION_POPOVER_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_ACTION_POPOVER_VIEW_H_

#include <string>

#include "base/functional/callback_forward.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace ui {
class Event;
}  // namespace ui

namespace views {
class Widget;
}  // namespace views

namespace maho {

enum class MahoSidebarActionPopoverAction {
  kNewSpace,
  kNewFolder,
  kNewTab,
};

class MahoSidebarActionPopoverView : public views::View {
  METADATA_HEADER(MahoSidebarActionPopoverView, views::View)

 public:
  using ActionCallback =
      base::RepeatingCallback<void(MahoSidebarActionPopoverAction)>;

  explicit MahoSidebarActionPopoverView(ActionCallback action_callback);
  MahoSidebarActionPopoverView(const MahoSidebarActionPopoverView&) = delete;
  MahoSidebarActionPopoverView& operator=(
      const MahoSidebarActionPopoverView&) = delete;
  ~MahoSidebarActionPopoverView() override;

  static views::Widget* Show(views::View* anchor_view,
                             ActionCallback action_callback);

 private:
  void AddActionButton(MahoSidebarActionPopoverAction action,
                       const std::u16string& title,
                       const std::u16string& subtitle);
  void OnActionPressed(MahoSidebarActionPopoverAction action,
                       const ui::Event& event);

  ActionCallback action_callback_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_ACTION_POPOVER_VIEW_H_
