#include "maho/browser/ui/views/space_config/maho_space_config_dialog.h"

#include <memory>
#include <utility>

#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/profiles/profile.h"
#include "maho/components/constants/webui_url_constants.h"
#include "net/base/url_util.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "url/gurl.h"

namespace {

constexpr int kDialogWidth = 420;
constexpr int kDialogHeight = 680;

views::Widget* g_space_config_dialog_widget = nullptr;
MahoSpaceConfigDialog::ActiveMode g_active_mode =
    MahoSpaceConfigDialog::ActiveMode::kNone;

const char* FocusToString(maho_space_config::mojom::InitialFocus focus) {
  switch (focus) {
    case maho_space_config::mojom::InitialFocus::kName:
      return "name";
    case maho_space_config::mojom::InitialFocus::kIcon:
      return "icon";
    case maho_space_config::mojom::InitialFocus::kColor:
      return "color";
    case maho_space_config::mojom::InitialFocus::kProfile:
      return "profile";
  }
  return "name";
}

class MahoSpaceConfigDialogDelegate : public views::DialogDelegate {
 public:
  MahoSpaceConfigDialogDelegate(Profile* profile, const GURL& url) {
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    // Intentionally NOT kWindow: on macOS, ModalType::kWindow forces
    // NSWindowStyleMaskTitled in native_widget_mac.mm StyleMaskForParams()
    // regardless of remove_standard_frame, which puts a native titlebar on
    // top of our WebUI. We use ModalType::kNone + z-order floating +
    // parent-child window relationship instead (see Open()).
    SetModalType(ui::mojom::ModalType::kNone);
    SetShowCloseButton(false);
    SetShowTitle(false);
    SetCanResize(false);
    set_fixed_width(kDialogWidth);
    set_margins(gfx::Insets());
    set_corner_radius(12);

    auto web_view = std::make_unique<views::WebView>(profile);
    web_view_ = web_view.get();
    web_view_->SetPreferredSize(gfx::Size(kDialogWidth, kDialogHeight));
    web_view_->LoadInitialURL(url);
    SetContentsView(std::move(web_view));
  }

  MahoSpaceConfigDialogDelegate(const MahoSpaceConfigDialogDelegate&) = delete;
  MahoSpaceConfigDialogDelegate& operator=(
      const MahoSpaceConfigDialogDelegate&) = delete;

  void WindowClosing() override {
    g_space_config_dialog_widget = nullptr;
    g_active_mode = MahoSpaceConfigDialog::ActiveMode::kNone;

    if (web_view_) {
      web_view_->SetWebContents(nullptr);
    }
  }

 private:
  raw_ptr<views::WebView> web_view_ = nullptr;
};

}  // namespace

// static
void MahoSpaceConfigDialog::Open(
    Profile* profile,
    gfx::NativeWindow parent_window,
    const std::string& space_id,
    maho_space_config::mojom::InitialFocus focus) {
  if (!profile || space_id.empty()) {
    return;
  }

  if (g_space_config_dialog_widget) {
    DLOG(WARNING) << "MahoSpaceConfigDialog::Open: closing stale widget before "
                     "reopening for space_id="
                  << space_id;
    g_space_config_dialog_widget->CloseNow();
    g_space_config_dialog_widget = nullptr;
    g_active_mode = ActiveMode::kNone;
  }

  GURL url(maho::kMahoSpaceConfigURL);
  url = net::AppendQueryParameter(url, "space_id", space_id);
  url = net::AppendQueryParameter(url, "focus", FocusToString(focus));

  auto dialog = std::make_unique<MahoSpaceConfigDialogDelegate>(profile, url);
  views::Widget* widget = views::DialogDelegate::CreateDialogWidget(
      std::move(dialog), parent_window, gfx::NativeView());
  if (!widget) {
    return;
  }

  widget->SetZOrderLevel(ui::ZOrderLevel::kFloatingWindow);

  g_space_config_dialog_widget = widget;
  g_active_mode = ActiveMode::kEdit;
  widget->Show();
  widget->Activate();
}

// static
void MahoSpaceConfigDialog::CloseActiveDialog() {
  if (g_space_config_dialog_widget) {
    g_space_config_dialog_widget->Close();
  }
}

// static
MahoSpaceConfigDialog::ActiveMode
MahoSpaceConfigDialog::GetActiveModeForTesting() {
  return g_active_mode;
}

// static
views::Widget* MahoSpaceConfigDialog::GetWidgetForTesting() {
  return g_space_config_dialog_widget;
}
