#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_ACTION_POPOVER_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_ACTION_POPOVER_VIEW_H_

#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/events/event.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/view.h"

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

  bool OnKeyPressed(const ui::KeyEvent& event) override;
  void SetSidebarPalette(const MahoSidebarPalette& palette);

 private:
  void AddActionRow(MahoSidebarActionPopoverAction action,
                    const gfx::VectorIcon& icon,
                    const std::u16string& title,
                    const std::u16string& subtitle);
  void OnRowHovered(int index);
  void OnRowClicked(int index);
  void UpdateHighlightState();
  void ActivateHighlightedRow();

  ActionCallback action_callback_;
  std::vector<raw_ptr<views::View>> action_rows_;
  std::vector<MahoSidebarActionPopoverAction> action_types_;
  int highlighted_index_ = 0;
  MahoSidebarPalette palette_;
  base::CallbackListSubscription palette_subscription_;

  base::WeakPtrFactory<MahoSidebarActionPopoverView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_ACTION_POPOVER_VIEW_H_
