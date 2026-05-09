#include "maho/browser/ui/views/space_create/maho_space_create_dialog.h"

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/views/widget/widget.h"

namespace {

constexpr int kDialogWidth = 456;

views::Widget* g_create_space_dialog_widget = nullptr;
maho::MahoSidebarCreateSpaceView* g_create_space_view = nullptr;

class MahoSpaceCreateDialogDelegate : public views::DialogDelegate {
 public:
  explicit MahoSpaceCreateDialogDelegate(Browser* browser) {
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    SetModalType(ui::mojom::ModalType::kWindow);
    SetShowCloseButton(false);
    SetShowTitle(false);
    SetCanResize(false);
    set_fixed_width(kDialogWidth);

    create_space_view_ =
        new maho::MahoSidebarCreateSpaceView(browser);
    create_space_view_->SetCloseCallback(base::BindRepeating(
        &MahoSpaceCreateDialogDelegate::CloseDialog, base::Unretained(this)));
    g_create_space_view = create_space_view_;
  }

  MahoSpaceCreateDialogDelegate(const MahoSpaceCreateDialogDelegate&) = delete;
  MahoSpaceCreateDialogDelegate& operator=(
      const MahoSpaceCreateDialogDelegate&) = delete;

  void CloseDialog() {
    if (auto* widget = GetWidget()) {
      widget->Close();
    }
  }

  views::View* GetContentsView() override { return create_space_view_; }

  void WindowClosing() override {
    g_create_space_dialog_widget = nullptr;
    g_create_space_view = nullptr;
  }

 private:
  ~MahoSpaceCreateDialogDelegate() override = default;

  raw_ptr<maho::MahoSidebarCreateSpaceView> create_space_view_ = nullptr;
};

}  // namespace

void MahoSpaceCreateDialog::Open(Browser* parent_browser) {
  if (!parent_browser || !parent_browser->window()) {
    return;
  }

  if (g_create_space_dialog_widget) {
    g_create_space_dialog_widget->Show();
    g_create_space_dialog_widget->Activate();
    if (g_create_space_view) {
      g_create_space_view->RequestNameFocus();
    }
    return;
  }

  auto* dialog = new MahoSpaceCreateDialogDelegate(parent_browser);
  views::Widget* widget = dialog->CreateDialogWidget(
      dialog, parent_browser->window()->GetNativeWindow(), gfx::NativeView());
  if (!widget) {
    g_create_space_view = nullptr;
    return;
  }

  g_create_space_dialog_widget = widget;
  widget->Show();
  widget->Activate();
  if (g_create_space_view) {
    g_create_space_view->RequestNameFocus();
  }
}

views::Widget* MahoSpaceCreateDialog::GetWidgetForTesting() {
  return g_create_space_dialog_widget;
}

maho::MahoSidebarCreateSpaceView*
MahoSpaceCreateDialog::GetCreateSpaceViewForTesting() {
  return g_create_space_view;
}
