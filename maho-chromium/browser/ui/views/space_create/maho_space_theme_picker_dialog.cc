// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/space_create/maho_space_theme_picker_dialog.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/components/constants/webui_url_constants.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom.h"
#include "ui/color/color_provider_manager.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "url/gurl.h"

namespace {

constexpr int kThemeDialogWidth = 680;
constexpr int kThemeDialogHeight = 760;

views::Widget* g_active_widget = nullptr;

class MahoSpaceThemePickerDialogDelegate;
MahoSpaceThemePickerDialogDelegate* g_active_delegate = nullptr;

std::string& GetPendingInitialThemeJsonStorage() {
  static base::NoDestructor<std::string> storage;
  return *storage;
}

class MahoSpaceThemePickerDialogDelegate : public views::DialogDelegate {
 public:
  MahoSpaceThemePickerDialogDelegate(Profile* profile,
                                     std::string initial_theme_json,
                                     MahoSpaceThemePickerDialog::ResultCallback
                                         on_result)
      : initial_theme_json_(std::move(initial_theme_json)),
        result_callback_(std::move(on_result)) {
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    SetModalType(ui::mojom::ModalType::kNone);
    SetShowCloseButton(false);
    SetShowTitle(false);
    SetCanResize(false);
    set_fixed_width(kThemeDialogWidth);
    set_margins(gfx::Insets());
    set_corner_radius(12);

    auto web_view = std::make_unique<views::WebView>(profile);
    web_view_ = web_view.get();
    web_view_->SetPreferredSize(gfx::Size(kThemeDialogWidth, kThemeDialogHeight));
    web_view_->LoadInitialURL(GURL(maho::kMahoSpaceCreateURL));
    SetContentsView(std::move(web_view));
  }

  MahoSpaceThemePickerDialogDelegate(
      const MahoSpaceThemePickerDialogDelegate&) = delete;
  MahoSpaceThemePickerDialogDelegate& operator=(
      const MahoSpaceThemePickerDialogDelegate&) = delete;

  void WindowClosing() override {
    g_active_widget = nullptr;
    g_active_delegate = nullptr;
    GetPendingInitialThemeJsonStorage().clear();

    if (MahoSpaceThemeState::HasPreviewOverride()) {
      MahoSpaceThemeState::ClearPreviewOverride();
      MahoSpaceThemeState::UpdateFromCore();
      ui::ColorProviderManager::Get().ResetColorProviderCache();
    }

    if (result_callback_) {
      std::move(result_callback_)
          .Run(committed_ ? committed_theme_json_ : std::string(),
               !committed_);
    }

    if (web_view_) {
      web_view_->SetWebContents(nullptr);
    }
  }

  void OnCommit(const std::string& theme_json) {
    committed_theme_json_ = theme_json;
    committed_ = true;
    if (g_active_widget) {
      g_active_widget->Close();
    }
  }

  void OnCancel() {
    committed_ = false;
    if (g_active_widget) {
      g_active_widget->Close();
    }
  }

  const std::string& initial_theme_json() const { return initial_theme_json_; }

 private:
  raw_ptr<views::WebView> web_view_ = nullptr;
  std::string initial_theme_json_;
  MahoSpaceThemePickerDialog::ResultCallback result_callback_;
  std::string committed_theme_json_;
  bool committed_ = false;
};

}  // namespace

// static
views::Widget* MahoSpaceThemePickerDialog::Open(
    Browser* browser,
    const std::string& initial_theme_json,
    ResultCallback on_result) {
  if (!browser) {
    return nullptr;
  }

  if (g_active_widget) {
    g_active_widget->Activate();
    return g_active_widget;
  }

  Profile* profile = browser->profile();
  if (!profile) {
    return nullptr;
  }

  GetPendingInitialThemeJsonStorage() = initial_theme_json;

  auto delegate = std::make_unique<MahoSpaceThemePickerDialogDelegate>(
      profile, initial_theme_json, std::move(on_result));
  MahoSpaceThemePickerDialogDelegate* delegate_raw = delegate.get();

  gfx::NativeWindow parent_window =
      browser->window() ? browser->window()->GetNativeWindow()
                        : gfx::NativeWindow();

  views::Widget* widget = views::DialogDelegate::CreateDialogWidget(
      std::move(delegate), parent_window, gfx::NativeView());
  if (!widget) {
    GetPendingInitialThemeJsonStorage().clear();
    return nullptr;
  }

  widget->SetZOrderLevel(ui::ZOrderLevel::kFloatingWindow);

  g_active_widget = widget;
  g_active_delegate = delegate_raw;

  widget->Show();
  widget->Activate();

  return widget;
}

// static
views::Widget* MahoSpaceThemePickerDialog::GetActiveWidgetForTesting() {
  return g_active_widget;
}

// static
void MahoSpaceThemePickerDialog::DispatchCommitToActive(
    const std::string& theme_json) {
  if (g_active_delegate) {
    g_active_delegate->OnCommit(theme_json);
  }
}

// static
void MahoSpaceThemePickerDialog::DispatchCancelToActive() {
  if (g_active_delegate) {
    g_active_delegate->OnCancel();
  }
}

// static
std::string
MahoSpaceThemePickerDialog::ConsumePendingInitialThemeJsonForActive() {
  std::string result = std::move(GetPendingInitialThemeJsonStorage());
  GetPendingInitialThemeJsonStorage().clear();
  return result;
}
