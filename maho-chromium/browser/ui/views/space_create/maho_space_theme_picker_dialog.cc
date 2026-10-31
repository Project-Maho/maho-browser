// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/space_create/maho_space_theme_picker_dialog.h"

#include <memory>
#include <string>
#include <utility>

#include "base/containers/flat_map.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "maho/components/constants/webui_url_constants.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "url/gurl.h"

namespace {

constexpr int kThemeDialogWidth = 600;
constexpr int kThemeDialogHeight = 760;

class MahoSpaceThemePickerDialogDelegate;
using DialogRegistry = base::flat_map<
    Browser*, base::WeakPtr<MahoSpaceThemePickerDialogDelegate>>;

DialogRegistry& GetDialogRegistry();

class MahoSpaceThemePickerDialogDelegate : public views::DialogDelegate {
 public:
  MahoSpaceThemePickerDialogDelegate(Browser* browser,
                                     Profile* profile,
                                     std::string initial_theme_json,
                                     MahoSpaceThemePickerDialog::ResultCallback
                                         on_result)
      : browser_(browser),
        initial_theme_json_(std::move(initial_theme_json)),
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
    // Clip the web contents to the dialog's rounded corners; a views::WebView
    // hosts a native surface that otherwise paints square corners over the
    // rounded BubbleFrameView.
    web_view_->SetPaintToLayer();
    web_view_->layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(12));
    web_view_->layer()->SetIsFastRoundedCorner(true);
    web_view_->LoadInitialURL(GURL(maho::kMahoSpaceCreateURL));
    SetContentsView(std::move(web_view));
  }

  MahoSpaceThemePickerDialogDelegate(
      const MahoSpaceThemePickerDialogDelegate&) = delete;
  MahoSpaceThemePickerDialogDelegate& operator=(
      const MahoSpaceThemePickerDialogDelegate&) = delete;

  void WindowClosing() override;

  void OnCommit(const std::string& theme_json) {
    committed_theme_json_ = theme_json;
    committed_ = true;
    if (views::Widget* widget = GetWidget()) {
      widget->Close();
    }
  }

  void OnCancel() {
    committed_ = false;
    if (views::Widget* widget = GetWidget()) {
      widget->Close();
    }
  }

  void UpdateCallbackAndInitialTheme(
      std::string initial_theme_json,
      MahoSpaceThemePickerDialog::ResultCallback on_result) {
    if (result_callback_) {
      std::move(result_callback_).Run(std::string(), true);
    }
    initial_theme_json_ = std::move(initial_theme_json);
    result_callback_ = std::move(on_result);
    committed_ = false;
    committed_theme_json_.clear();
    if (MahoSpaceThemeState::HasPreviewOverride(browser_)) {
      MahoSpaceThemeState::ClearPreviewOverride(browser_);
      MahoSpaceThemeState::UpdateFromCore();
      MahoSpaceThemePickerDialog::RepaintBrowser(browser_);
    }

    if (web_view_) {
      web_view_->GetWebContents()->GetController().Reload(
          content::ReloadType::NORMAL, true);
    }
  }

  bool IsHostingWebContents(content::WebContents* web_contents) const {
    return web_view_ && web_view_->GetWebContents() == web_contents;
  }

  std::string ConsumeInitialThemeJson() {
    std::string result = std::move(initial_theme_json_);
    initial_theme_json_.clear();
    return result;
  }
  base::WeakPtr<MahoSpaceThemePickerDialogDelegate> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  raw_ptr<Browser> browser_ = nullptr;
  raw_ptr<views::WebView> web_view_ = nullptr;
  std::string initial_theme_json_;
  MahoSpaceThemePickerDialog::ResultCallback result_callback_;
  std::string committed_theme_json_;
  bool committed_ = false;
  base::WeakPtrFactory<MahoSpaceThemePickerDialogDelegate> weak_factory_{this};
};

DialogRegistry& GetDialogRegistry() {
  static base::NoDestructor<DialogRegistry> registry;
  return *registry;
}

void MahoSpaceThemePickerDialogDelegate::WindowClosing() {
  DialogRegistry& registry = GetDialogRegistry();
  const auto it = registry.find(browser_);
  if (it != registry.end() && it->second.get() == this) {
    registry.erase(it);
  }

  initial_theme_json_.clear();
  if (!committed_ && MahoSpaceThemeState::HasPreviewOverride(browser_)) {
    MahoSpaceThemeState::ClearPreviewOverride(browser_);
    MahoSpaceThemeState::UpdateFromCore();
    MahoSpaceThemePickerDialog::RepaintBrowser(browser_);
  }

  if (result_callback_) {
    std::move(result_callback_)
        .Run(committed_ ? committed_theme_json_ : std::string(), !committed_);
  }

  if (web_view_) {
    web_view_->SetWebContents(nullptr);
  }
}

}  // namespace

// static
views::Widget* MahoSpaceThemePickerDialog::Open(
    Browser* browser,
    const std::string& initial_theme_json,
    ResultCallback on_result) {
  if (!browser) {
    return nullptr;
  }

  DialogRegistry& registry = GetDialogRegistry();
  const auto existing = registry.find(browser);
  if (existing != registry.end()) {
    MahoSpaceThemePickerDialogDelegate* delegate = existing->second.get();
    views::Widget* widget = delegate ? delegate->GetWidget() : nullptr;
    if (widget && !widget->IsClosed()) {
      delegate->UpdateCallbackAndInitialTheme(initial_theme_json,
                                              std::move(on_result));
      widget->Activate();
      return widget;
    }
    registry.erase(existing);
  }

  Profile* profile = browser->GetProfile();
  if (!profile) {
    return nullptr;
  }

  auto delegate = std::make_unique<MahoSpaceThemePickerDialogDelegate>(
      browser, profile, initial_theme_json, std::move(on_result));
  MahoSpaceThemePickerDialogDelegate* delegate_raw = delegate.get();

  gfx::NativeWindow parent_window =
      browser->GetWindow() ? browser->GetWindow()->GetNativeWindow()
                        : gfx::NativeWindow();

  views::Widget* widget = views::DialogDelegate::CreateDialogWidget(
      std::move(delegate), parent_window, gfx::NativeView());
  if (!widget) {
    return nullptr;
  }

  widget->SetZOrderLevel(ui::ZOrderLevel::kFloatingWindow);

  registry.emplace(browser, delegate_raw->GetWeakPtr());

  widget->Show();
  widget->Activate();

  return widget;
}

// static
views::Widget* MahoSpaceThemePickerDialog::GetActiveWidgetForTesting() {
  for (const auto& entry : GetDialogRegistry()) {
    const base::WeakPtr<MahoSpaceThemePickerDialogDelegate>& delegate =
        entry.second;
    if (delegate && delegate->GetWidget() && !delegate->GetWidget()->IsClosed()) {
      return delegate->GetWidget();
    }
  }
  return nullptr;
}

// static
void MahoSpaceThemePickerDialog::DispatchCommitToActive(
    Browser* browser,
    const std::string& theme_json) {
  const auto it = GetDialogRegistry().find(browser);
  if (it != GetDialogRegistry().end() && it->second) {
    it->second->OnCommit(theme_json);
  }
}

// static
void MahoSpaceThemePickerDialog::DispatchCancelToActive(Browser* browser) {
  const auto it = GetDialogRegistry().find(browser);
  if (it != GetDialogRegistry().end() && it->second) {
    it->second->OnCancel();
  }
}

// static
std::string MahoSpaceThemePickerDialog::ConsumePendingInitialThemeJson(
    Browser* browser,
    content::WebContents* web_contents) {
  const auto it = GetDialogRegistry().find(browser);
  if (it == GetDialogRegistry().end() || !it->second ||
      !it->second->IsHostingWebContents(web_contents)) {
    return std::string();
  }
  return it->second->ConsumeInitialThemeJson();
}

void MahoSpaceThemePickerDialog::RepaintBrowser(Browser* browser) {
  maho::MahoSidebarView::RefreshSpaceThemeForBrowser(browser);
}

// static
Browser* MahoSpaceThemePickerDialog::GetOwnerBrowserForWebContents(
    content::WebContents* web_contents) {
  for (const auto& [browser, delegate] : GetDialogRegistry()) {
    if (delegate && delegate->IsHostingWebContents(web_contents)) {
      return browser;
    }
  }
  return nullptr;
}
