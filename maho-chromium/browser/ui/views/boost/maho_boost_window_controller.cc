// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/boost/maho_boost_window_controller.h"

#include <map>
#include <memory>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/ui/views/boost/maho_boost_window_controller_bridge.h"
#include "maho/components/constants/webui_url_constants.h"
#include "net/base/url_util.h"
#include "ui/base/ui_base_types.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "url/gurl.h"

namespace maho {

namespace {

std::map<Browser*, std::unique_ptr<MahoBoostWindowController>>&
GetBrowserControllersMap() {
  static auto* map =
      new std::map<Browser*, std::unique_ptr<MahoBoostWindowController>>();
  return *map;
}

base::WeakPtr<MahoBoostWindowController>& GetPendingControllerStorage() {
  static base::NoDestructor<base::WeakPtr<MahoBoostWindowController>> storage;
  return *storage;
}

// WidgetDelegate + View that owns the WebView for the Boost editor.
// Inherits both bases directly (WidgetDelegateView is deprecated).
class MahoBoostWindowView : public views::WidgetDelegate,
                            public views::View {
 public:
  using WindowClosingCallback = base::OnceClosure;

  MahoBoostWindowView(Profile* profile,
                      const GURL& url,
                      int initial_width,
                      int initial_height,
                      WindowClosingCallback on_closing)
      : preferred_width_(initial_width),
        preferred_height_(initial_height),
        on_closing_(std::move(on_closing)) {
    SetTitle(u"Boost Editor");
    SetCanResize(false);
    SetCanMaximize(false);
    SetCanMinimize(false);
    SetShowCloseButton(true);

    auto web_view = std::make_unique<views::WebView>(profile);
    web_view->SetPreferredSize(
        gfx::Size(initial_width, initial_height));
    web_view->LoadInitialURL(url);
    web_view_ = AddChildView(std::move(web_view));
  }

  MahoBoostWindowView(const MahoBoostWindowView&) = delete;
  MahoBoostWindowView& operator=(const MahoBoostWindowView&) = delete;
  ~MahoBoostWindowView() override = default;

  views::Widget* GetWidget() override { return views::View::GetWidget(); }
  const views::Widget* GetWidget() const override {
    return views::View::GetWidget();
  }
  views::View* GetContentsView() override { return this; }

  void WindowClosing() override {
    if (web_view_) {
      web_view_->SetWebContents(nullptr);
      web_view_ = nullptr;
    }
    if (on_closing_) {
      std::move(on_closing_).Run();
    }
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& /*available_size*/) const override {
    return gfx::Size(preferred_width_, preferred_height_);
  }

  void Layout(PassKey) override {
    LayoutSuperclass<views::View>(this);
    if (web_view_) {
      web_view_->SetBoundsRect(GetLocalBounds());
    }
  }

  void SetPreferredDimensions(int width, int height) {
    preferred_width_ = width;
    preferred_height_ = height;
    if (web_view_) {
      web_view_->SetPreferredSize(gfx::Size(width, height));
    }
  }

 private:
  int preferred_width_;
  int preferred_height_;
  raw_ptr<views::WebView> web_view_ = nullptr;
  WindowClosingCallback on_closing_;
};

}  // namespace

MahoBoostWindowController& MahoBoostWindowController::GetForBrowser(
    Browser* browser, Profile* profile) {
  auto& map = GetBrowserControllersMap();
  auto it = map.find(browser);
  if (it != map.end()) {
    return *it->second;
  }
  auto controller =
      std::make_unique<MahoBoostWindowController>(browser, profile);
  auto* raw = controller.get();
  map.insert({browser, std::move(controller)});
  return *raw;
}

void MahoBoostWindowController::HideActive() {
  for (auto& [browser, controller] : GetBrowserControllersMap()) {
    if (controller && controller->IsShowing()) {
      controller->Hide();
    }
  }
}

base::WeakPtr<MahoBoostWindowController>
MahoBoostWindowController::ConsumePendingControllerForUI() {
  base::WeakPtr<MahoBoostWindowController> result =
      std::move(GetPendingControllerStorage());
  GetPendingControllerStorage().reset();
  return result;
}

base::WeakPtr<MahoBoostWindowController>
ConsumePendingBoostControllerForUI() {
  return MahoBoostWindowController::ConsumePendingControllerForUI();
}

void SetBoostEditorKilledCallback(
    base::WeakPtr<MahoBoostWindowController> controller,
    base::RepeatingClosure callback) {
  if (controller) {
    controller->SetEditorKilledCallback(std::move(callback));
  }
}

void HideActiveBoost() {
  MahoBoostWindowController::HideActive();
}

void SetBoostWindowMode(
    base::WeakPtr<MahoBoostWindowController> controller,
    maho_boost::mojom::WindowMode mode) {
  if (controller) {
    controller->SetMode(mode);
  }
}

content::WebContents* GetActiveTabForBoost(
    base::WeakPtr<MahoBoostWindowController> controller) {
  if (!controller || !controller->browser_) {
    return nullptr;
  }
  TabStripModel* model = controller->browser_->tab_strip_model();
  if (!model) {
    return nullptr;
  }
  return model->GetActiveWebContents();
}

MahoBoostWindowController::MahoBoostWindowController(Browser* browser,
                                                     Profile* profile)
    : browser_(browser), profile_(profile) {
  if (browser_) {
    TabStripModel* tab_strip = browser_->tab_strip_model();
    if (tab_strip) {
      observed_tab_strip_ = tab_strip;
      tab_strip->AddObserver(this);
    }
  }
}

MahoBoostWindowController::~MahoBoostWindowController() {
  StopObservingTab();
  if (observed_tab_strip_) {
    observed_tab_strip_->RemoveObserver(this);
    observed_tab_strip_ = nullptr;
  }
  if (widget_) {
    DestroyWidget();
  }
}

void MahoBoostWindowController::ShowForActiveDomain(
    content::WebContents* active_tab) {
  if (!active_tab || !browser_) {
    return;
  }

  const GURL& url = active_tab->GetLastCommittedURL();
  std::string domain(url.host());
  if (domain.empty()) {
    return;
  }

  if (widget_) {
    if (current_domain_ == domain) {
      widget_->Show();
      widget_->Activate();
      return;
    }
    // Different domain — kill existing editor, broadcast OnEditorKilled.
    BroadcastEditorKilledAndClose();
  }

  StartObservingTab(active_tab);
  CreateAndShowWidget(domain);
}

void MahoBoostWindowController::Hide() {
  if (!widget_) {
    return;
  }
  StopObservingTab();
  DestroyWidget();
}

bool MahoBoostWindowController::IsShowing() const {
  return widget_ && widget_->IsVisible();
}

void MahoBoostWindowController::SetMode(
    maho_boost::mojom::WindowMode mode) {
  if (!widget_ || !browser_) {
    return;
  }

  current_mode_ = mode;

  int new_width;
  int new_height;
  if (mode == maho_boost::mojom::WindowMode::kCode) {
    new_width = kCodeModeWidth;
    new_height = kCodeModeHeight;
  } else {
    new_width = kBoostModeWidth;
    new_height = kBoostModeHeight;
  }

  gfx::Rect bounds = widget_->GetWindowBoundsInScreen();

  if (mode == maho_boost::mojom::WindowMode::kCode) {
    // Shift left by kCodeModeRightAlignOffsetPx when the widget's right edge
    // is to the right of the parent window's center (per Zen semantics).
    gfx::Rect parent_bounds;
    if (browser_->window()) {
      parent_bounds = browser_->window()->GetBounds();
    }
    int parent_center_x = parent_bounds.x() + parent_bounds.width() / 2;
    if (bounds.right() > parent_center_x) {
      bounds.set_x(bounds.x() - kCodeModeRightAlignOffsetPx);
    }
  } else {
    // Returning to boost mode: shift right to undo offset if it was applied.
    // We approximate by shifting right if the current x would place the widget
    // further left than needed. Use the width delta as the offset reversal.
    gfx::Rect parent_bounds;
    if (browser_->window()) {
      parent_bounds = browser_->window()->GetBounds();
    }
    int parent_center_x = parent_bounds.x() + parent_bounds.width() / 2;
    if ((bounds.x() + new_width) <= parent_center_x) {
      // The widget was shifted left; restore by shifting right.
      bounds.set_x(bounds.x() + kCodeModeRightAlignOffsetPx);
    }
  }

  bounds.set_width(new_width);
  bounds.set_height(new_height);

  auto* delegate = static_cast<MahoBoostWindowView*>(widget_->widget_delegate());
  if (delegate) {
    delegate->SetPreferredDimensions(new_width, new_height);
  }

  widget_->SetBounds(bounds);
}

void MahoBoostWindowController::SetEditorKilledCallback(
    base::RepeatingClosure callback) {
  editor_killed_callback_ = std::move(callback);
}

void MahoBoostWindowController::OnWidgetDestroying(views::Widget* widget) {
  DCHECK_EQ(widget, widget_.get());
  current_domain_.clear();
  current_mode_ = maho_boost::mojom::WindowMode::kBoost;
  StopObservingTab();
  widget_->RemoveObserver(this);
  widget_.release();
}

void MahoBoostWindowController::OnTabStripModelChanged(
    TabStripModel* /*tab_strip_model*/,
    const TabStripModelChange& /*change*/,
    const TabStripSelectionChange& selection) {
  if (!IsShowing()) {
    return;
  }
  if (selection.active_tab_changed()) {
    Hide();
  }
}

void MahoBoostWindowController::OnTabStripModelDestroyed(
    TabStripModel* tab_strip_model) {
  if (IsShowing()) {
    Hide();
  }
  if (tab_strip_model) {
    tab_strip_model->RemoveObserver(this);
  }
  observed_tab_strip_ = nullptr;
}

void MahoBoostWindowController::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle) {
    return;
  }
  // Close on top-level, committed, non-same-document navigations.
  if (navigation_handle->IsInPrimaryMainFrame() &&
      navigation_handle->HasCommitted() &&
      !navigation_handle->IsSameDocument()) {
    Hide();
  }
}

void MahoBoostWindowController::CreateAndShowWidget(const std::string& domain) {
  DCHECK(!widget_);

  Profile* profile = profile_;

  GURL url(kMahoBoostUntrustedURL);
  url = net::AppendQueryParameter(url, "domain", domain);

  auto* delegate = new MahoBoostWindowView(
      profile, url, kBoostModeWidth, kBoostModeHeight,
      base::BindOnce(&MahoBoostWindowController::OnDelegateWindowClosing,
                     weak_factory_.GetWeakPtr()));

  views::Widget::InitParams params(
      views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW);
  params.delegate = delegate;
  params.bounds = gfx::Rect(0, 0, kBoostModeWidth, kBoostModeHeight);
  params.accept_events = true;
  params.activatable = views::Widget::InitParams::Activatable::kYes;

#if BUILDFLAG(IS_MAC)
  params.remove_standard_frame = true;
#endif

  auto widget = std::make_unique<views::Widget>();
  widget->Init(std::move(params));
  widget->AddObserver(this);

  widget->SetZOrderLevel(ui::ZOrderLevel::kFloatingWindow);
  widget->CenterWindow(gfx::Size(kBoostModeWidth, kBoostModeHeight));
  widget->Show();
  widget->Activate();

  widget_ = std::move(widget);
  current_domain_ = domain;
  current_mode_ = maho_boost::mojom::WindowMode::kBoost;

  GetPendingControllerStorage() = weak_factory_.GetWeakPtr();
}

void MahoBoostWindowController::DestroyWidget() {
  if (!widget_) {
    return;
  }
  current_domain_.clear();
  current_mode_ = maho_boost::mojom::WindowMode::kBoost;
  widget_->RemoveObserver(this);
  widget_->CloseNow();
  widget_.release();
}

void MahoBoostWindowController::OnDelegateWindowClosing() {
  current_domain_.clear();
  current_mode_ = maho_boost::mojom::WindowMode::kBoost;
  StopObservingTab();
  if (widget_) {
    widget_->RemoveObserver(this);
    widget_.release();
  }
}

void MahoBoostWindowController::BroadcastEditorKilledAndClose() {
  if (editor_killed_callback_) {
    editor_killed_callback_.Run();
    editor_killed_callback_.Reset();
  }
  StopObservingTab();
  DestroyWidget();
}

void MahoBoostWindowController::StartObservingTab(
    content::WebContents* active_tab) {
  StopObservingTab();
  if (active_tab) {
    content::WebContentsObserver::Observe(active_tab);
  }
}

void MahoBoostWindowController::StopObservingTab() {
  content::WebContentsObserver::Observe(nullptr);
}

}  // namespace maho
