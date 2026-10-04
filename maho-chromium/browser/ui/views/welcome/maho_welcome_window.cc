// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/welcome/maho_welcome_window.h"

#include <map>
#include <memory>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/task/single_thread_task_runner.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "maho/components/constants/webui_url_constants.h"
#include "ui/base/ui_base_types.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/webview/unhandled_keyboard_event_handler.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/views_delegate.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "url/gurl.h"

namespace maho {

namespace {

constexpr int kInitialWelcomeWidth = 1280;
constexpr int kInitialWelcomeHeight = 800;

// Process-wide singleton.  Welcome onboarding never races with itself: there
// is exactly one welcome window in flight, owned via this unique_ptr, and
// destroyed on widget close.
std::unique_ptr<MahoWelcomeWindow>& GetSingletonStorage() {
  static base::NoDestructor<std::unique_ptr<MahoWelcomeWindow>> storage;
  return *storage;
}

// Map from the WebContents hosting the welcome WebUI to its owning window.
// Used by the page handler / FromWebContents() so the WebUI side does not
// have to reach into globals.
std::map<content::WebContents*, MahoWelcomeWindow*>& GetContentsMap() {
  static base::NoDestructor<std::map<content::WebContents*, MahoWelcomeWindow*>>
      map;
  return *map;
}

// WidgetDelegate + View that owns the WebView for the Welcome WebUI.
class MahoWelcomeWindowView : public views::WidgetDelegate,
                              public views::View,
                              public content::WebContentsDelegate {
 public:
  using WindowClosingCallback = base::OnceClosure;

  MahoWelcomeWindowView(Profile* profile,
                        const GURL& url,
                        WindowClosingCallback on_closing,
                        base::WeakPtr<MahoWelcomeWindow> window_weak)
      : on_closing_(std::move(on_closing)), window_weak_(window_weak) {
    SetTitle(u"Welcome to Maho");
    SetCanResize(false);
    SetCanMaximize(true);
    SetCanMinimize(false);
    SetShowCloseButton(false);  // No native close button — frameless.

    auto web_view = std::make_unique<views::WebView>(profile);
    web_view->SetPreferredSize(
        gfx::Size(kInitialWelcomeWidth, kInitialWelcomeHeight));
    web_view->LoadInitialURL(url);
    web_contents_ = web_view->GetWebContents();
    web_view_ = AddChildView(std::move(web_view));

    // Route unhandled keyboard events (e.g. the Cmd+V / Ctrl+V paste key
    // equivalent) back through the FocusManager so the platform Edit menu
    // shortcuts reach the web content. Without a delegate the redispatch
    // chain never starts and clipboard paste silently fails in this
    // non-browser window.
    if (web_contents_) {
      web_contents_->SetDelegate(this);
    }

    if (web_contents_ && window_weak_) {
      GetContentsMap()[web_contents_] = window_weak_.get();
    }
  }

  MahoWelcomeWindowView(const MahoWelcomeWindowView&) = delete;
  MahoWelcomeWindowView& operator=(const MahoWelcomeWindowView&) = delete;
  ~MahoWelcomeWindowView() override = default;

  content::WebContents* GetHostedWebContents() { return web_contents_; }

  // content::WebContentsDelegate:
  bool HandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override {
    return unhandled_keyboard_event_handler_.HandleKeyboardEvent(
        event, GetFocusManager());
  }

  views::Widget* GetWidget() override { return views::View::GetWidget(); }
  const views::Widget* GetWidget() const override {
    return views::View::GetWidget();
  }
  views::View* GetContentsView() override { return this; }

  void WindowClosing() override {
    if (web_contents_) {
      web_contents_->SetDelegate(nullptr);
      auto& map = GetContentsMap();
      map.erase(web_contents_);
      web_contents_ = nullptr;
    }
    if (web_view_) {
      web_view_->SetWebContents(nullptr);
      // Explicitly tear down the WebView before AXPlatform shuts down.
      auto removed = RemoveChildViewT(web_view_.get());
      web_view_ = nullptr;
      removed.reset();
    }
    if (on_closing_) {
      std::move(on_closing_).Run();
    }
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& /*available_size*/) const override {
    return gfx::Size(kInitialWelcomeWidth, kInitialWelcomeHeight);
  }

  void Layout(PassKey) override {
    LayoutSuperclass<views::View>(this);
    if (web_view_) {
      web_view_->SetBoundsRect(GetLocalBounds());
    }
  }

 private:
  raw_ptr<views::WebView> web_view_ = nullptr;
  raw_ptr<content::WebContents> web_contents_ = nullptr;
  WindowClosingCallback on_closing_;
  base::WeakPtr<MahoWelcomeWindow> window_weak_;
  views::UnhandledKeyboardEventHandler unhandled_keyboard_event_handler_;
};

}  // namespace

// static
void MahoWelcomeWindow::Show(Profile* profile) {
  auto& storage = GetSingletonStorage();
  if (storage) {
    if (storage->widget_) {
      storage->widget_->Show();
      storage->widget_->Activate();
    }
    return;
  }
  storage.reset(new MahoWelcomeWindow(profile));
  // ctor holds a ScopedKeepAlive (pins the process while the startup browser
  // is suppressed); defer widget creation since PostProfileInit is too early
  // to spawn a renderer.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&MahoWelcomeWindow::CreateAndShowWidget,
                                storage->weak_factory_.GetWeakPtr()));
}

bool MahoWelcomeWindow::ActivateIfOpen() {
  auto& storage = GetSingletonStorage();
  if (!storage) {
    return false;
  }
  if (storage->widget_) {
    storage->widget_->Show();
    storage->widget_->Activate();
  }
  return true;
}

content::WebContents* MahoWelcomeWindow::GetHostedWebContents() {
  auto& map = GetContentsMap();
  return map.empty() ? nullptr : map.begin()->first;
}

// static
void MahoWelcomeWindow::CloseInstance() {
  auto& storage = GetSingletonStorage();
  if (!storage) {
    return;
  }
  // Close() will trigger OnWidgetDestroying which clears the singleton via
  // a posted task.  We must not delete here while the widget is still alive.
  storage->Close();
}

// static
MahoWelcomeWindow* MahoWelcomeWindow::FromWebContents(
    content::WebContents* web_contents) {
  if (!web_contents) {
    return nullptr;
  }
  auto& map = GetContentsMap();
  auto it = map.find(web_contents);
  return it == map.end() ? nullptr : it->second;
}

views::Widget* MahoWelcomeWindow::GetWidget() {
  return widget_.get();
}

void MahoWelcomeWindow::Close() {
  if (widget_) {
    widget_->Close();
  }
}

void MahoWelcomeWindow::OnWidgetDestroying(views::Widget* widget) {
  DCHECK_EQ(widget, widget_.get());
  if (widget_) {
    widget_->RemoveObserver(this);
  }
  // The Widget is being torn down; the singleton must not outlive it.  Post
  // the storage reset so we are not destroyed mid-callback.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce([] {
        auto& storage = GetSingletonStorage();
        storage.reset();
      }));
}

MahoWelcomeWindow::MahoWelcomeWindow(Profile* profile) : profile_(profile) {
  keep_alive_ = std::make_unique<ScopedKeepAlive>(
      KeepAliveOrigin::USER_MANAGER_VIEW, KeepAliveRestartOption::DISABLED);
  views::ViewsDelegate::GetInstance()->AddRef();
}

MahoWelcomeWindow::~MahoWelcomeWindow() {
  if (web_contents_) {
    auto& map = GetContentsMap();
    map.erase(web_contents_);
    web_contents_ = nullptr;
  }
  if (widget_) {
    widget_->RemoveObserver(this);
    widget_.reset();
  }
  views::ViewsDelegate::GetInstance()->ReleaseRef();
}

void MahoWelcomeWindow::CreateAndShowWidget() {
  DCHECK(!widget_);

  GURL url("chrome://" + std::string(maho::kMahoWelcomeHost) + "/");

  auto* delegate = new MahoWelcomeWindowView(
      profile_, url,
      /*on_closing=*/base::DoNothing(),
      weak_factory_.GetWeakPtr());

  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW);
  params.delegate = delegate;
  params.bounds =
      gfx::Rect(0, 0, kInitialWelcomeWidth, kInitialWelcomeHeight);
  params.accept_events = true;
  params.activatable = views::Widget::InitParams::Activatable::kYes;

#if BUILDFLAG(IS_MAC)
  params.remove_standard_frame = true;
#endif

  auto widget = std::make_unique<views::Widget>();
  widget->Init(std::move(params));
  widget->AddObserver(this);

  gfx::Rect work_area = widget->GetWorkAreaBoundsInScreen();
  widget->SetBounds(work_area);
  widget->Show();
  widget->Activate();

  web_contents_ = delegate->GetHostedWebContents();
  widget_ = std::move(widget);
}

}  // namespace maho
