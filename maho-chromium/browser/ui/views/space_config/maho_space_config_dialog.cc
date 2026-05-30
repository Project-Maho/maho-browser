#include "maho/browser/ui/views/space_config/maho_space_config_dialog.h"

#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/profiles/profile.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/components/constants/webui_url_constants.h"
#include "net/base/url_util.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_provider_manager.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "url/gurl.h"

namespace {

constexpr int kDialogWidth = 420;
constexpr int kDialogHeight = 520;

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
    SetModalType(ui::mojom::ModalType::kWindow);
    SetShowCloseButton(false);
    SetShowTitle(false);
    SetCanResize(false);
    set_fixed_width(kDialogWidth);

    web_view_ = new views::WebView(profile);
    web_view_->SetPreferredSize(gfx::Size(kDialogWidth, kDialogHeight));
    web_view_->LoadInitialURL(url);
  }

  MahoSpaceConfigDialogDelegate(const MahoSpaceConfigDialogDelegate&) = delete;
  MahoSpaceConfigDialogDelegate& operator=(
      const MahoSpaceConfigDialogDelegate&) = delete;

  views::View* GetContentsView() override { return web_view_; }

  void WindowClosing() override {
    g_space_config_dialog_widget = nullptr;
    g_active_mode = MahoSpaceConfigDialog::ActiveMode::kNone;

    if (MahoSpaceThemeState::HasPreviewOverride()) {
      MahoSpaceThemeState::ClearPreviewOverride();
      MahoSpaceThemeState::UpdateFromCore();
      ui::ColorProviderManager::Get().ResetColorProviderCache();
    }

    if (web_view_) {
      web_view_->SetWebContents(nullptr);
    }
  }

 private:
  ~MahoSpaceConfigDialogDelegate() override = default;

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

  auto* dialog = new MahoSpaceConfigDialogDelegate(profile, url);
  views::Widget* widget =
      dialog->CreateDialogWidget(dialog, parent_window, gfx::NativeView());
  if (!widget) {
    return;
  }

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
