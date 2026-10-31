// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/boost/maho_boost_window_controller.h"

#include <memory>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/task/single_thread_task_runner.h"
#include "base/unguessable_token.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/net/maho_boost_injection_handler.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "maho/browser/ui/views/boost/maho_boost_window_controller_bridge.h"
#include "maho/components/constants/webui_url_constants.h"
#include "net/base/url_util.h"
#include "third_party/blink/public/mojom/page/draggable_region.mojom.h"
#include "third_party/skia/include/core/SkRegion.h"
#include "ui/base/hit_test.h"
#include "ui/base/ui_base_types.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/window/frame_view.h"
#include "ui/views/window/non_client_view.h"
#include "url/gurl.h"

namespace maho {

namespace {

constexpr base::TimeDelta kHostCloseWatchdogTimeout = base::Seconds(5);

std::map<std::string, base::WeakPtr<MahoBoostWindowController>>&
GetPendingControllerStorage() {
  static base::NoDestructor<
      std::map<std::string, base::WeakPtr<MahoBoostWindowController>>>
      storage;
  return *storage;
}

class MahoBoostWebView : public views::WebView {
  METADATA_HEADER(MahoBoostWebView, views::WebView)

 public:
  explicit MahoBoostWebView(Profile* profile) : views::WebView(profile) {}

  void SetWebContents(content::WebContents* web_contents) override {
    views::WebView::SetWebContents(web_contents);
    if (web_contents) {
      web_contents->SetSupportsDraggableRegions(true);
    }
  }

  void DraggableRegionsChanged(
      const std::vector<blink::mojom::DraggableRegionPtr>& regions,
      content::WebContents*) override {
    draggable_region_.setEmpty();
    for (const auto& region : regions) {
      draggable_region_.op(
          SkIRect::MakeXYWH(region->bounds.x(), region->bounds.y(),
                            region->bounds.width(), region->bounds.height()),
          region->draggable ? SkRegion::kUnion_Op : SkRegion::kDifference_Op);
    }
  }

  bool ContainsDraggablePoint(const gfx::Point& point) const {
    return draggable_region_.contains(point.x(), point.y());
  }

 private:
  SkRegion draggable_region_;
};

BEGIN_METADATA(MahoBoostWebView)
END_METADATA

class MahoBoostWindowContentsView : public views::View {
 public:
  MahoBoostWindowContentsView(Profile* profile,
                              const GURL& url,
                              int initial_width,
                              int initial_height)
      : preferred_width_(initial_width),
        preferred_height_(initial_height) {
    auto web_view = std::make_unique<MahoBoostWebView>(profile);
    web_view->SetPreferredSize(
        gfx::Size(initial_width, initial_height));
    web_view->LoadInitialURL(url);
    web_view->GetWebContents()->SetSupportsDraggableRegions(true);
    web_view_ = AddChildView(std::move(web_view));
  }

  MahoBoostWindowContentsView(const MahoBoostWindowContentsView&) = delete;
  MahoBoostWindowContentsView& operator=(
      const MahoBoostWindowContentsView&) = delete;
  ~MahoBoostWindowContentsView() override = default;

  void DetachWebContents() {
    if (web_view_) {
      web_view_->SetWebContents(nullptr);
      web_view_ = nullptr;
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

  content::WebContents* GetWebContentsForTesting() const {
    return web_view_ ? web_view_->GetWebContents() : nullptr;
  }

  bool ContainsDraggablePoint(const gfx::Point& point) const {
    return web_view_ && web_view_->ContainsDraggablePoint(point);
  }

 private:
  int preferred_width_;
  int preferred_height_;
  raw_ptr<MahoBoostWebView> web_view_ = nullptr;
};

class MahoBoostWindowDelegate : public views::WidgetDelegate {
 public:
  explicit MahoBoostWindowDelegate(MahoBoostWindowContentsView* contents)
      : contents_(contents) {
    SetTitle(u"Boost Editor");
    SetCanResize(false);
    SetCanMaximize(false);
    SetCanMinimize(false);
    SetShowCloseButton(false);
  }

  MahoBoostWindowDelegate(const MahoBoostWindowDelegate&) = delete;
  MahoBoostWindowDelegate& operator=(const MahoBoostWindowDelegate&) = delete;
  ~MahoBoostWindowDelegate() override = default;

  void OnWidgetInitialized() override {
    GetWidget()->non_client_view()->frame_view()->set_non_client_hit_test_callback(
        base::BindRepeating(&MahoBoostWindowDelegate::NonClientHitTest,
                            base::Unretained(this)));
  }

  void WindowClosing() override {
    if (contents_) {
      contents_->DetachWebContents();
      contents_ = nullptr;
    }
  }

  void SetPreferredDimensions(int width, int height) {
    if (contents_) {
      contents_->SetPreferredDimensions(width, height);
    }
  }

  content::WebContents* GetWebContentsForTesting() const {
    return contents_ ? contents_->GetWebContentsForTesting() : nullptr;
  }

 private:
  int NonClientHitTest(const gfx::Point& point) const {
    if (contents_ && contents_->ContainsDraggablePoint(point)) {
      return HTCAPTION;
    }
    return HTNOWHERE;
  }

  raw_ptr<MahoBoostWindowContentsView> contents_;
};

}  // namespace

MahoBoostWindowController& MahoBoostWindowController::GetForBrowser(
    Browser* browser, Profile* /*profile*/) {
  MahoBoostWindowController* ctrl = GetOrCreateForBrowser(browser);
  DCHECK(ctrl);
  return *ctrl;
}

void MahoBoostWindowController::HideActive() {
  for (auto& [browser, controller] : GetInstances()) {
    // Gate on the widget existing rather than on visibility: a hidden-but-alive
    // editor widget still owns Views and AX platform nodes, and skipping it here
    // left those objects behind.
    if (controller && controller->HasWidget()) {
      controller->Hide();
    }
  }
}

base::WeakPtr<MahoBoostWindowController>
MahoBoostWindowController::ConsumePendingControllerForUI() {
  return nullptr;
}

base::WeakPtr<MahoBoostWindowController>
MahoBoostWindowController::ConsumePendingControllerForUI(
    const std::string& controller_token) {
  auto& storage = GetPendingControllerStorage();
  auto it = storage.find(controller_token);
  if (it == storage.end()) {
    return nullptr;
  }
  base::WeakPtr<MahoBoostWindowController> result = std::move(it->second);
  storage.erase(it);
  return result;
}

base::WeakPtr<MahoBoostWindowController>
ConsumePendingBoostControllerForUI() {
  return MahoBoostWindowController::ConsumePendingControllerForUI();
}

base::WeakPtr<MahoBoostWindowController>
ConsumePendingBoostControllerForUI(const std::string& controller_token) {
  return MahoBoostWindowController::ConsumePendingControllerForUI(
      controller_token);
}

void SetBoostEditorKilledCallback(
    base::WeakPtr<MahoBoostWindowController> controller,
    base::RepeatingClosure callback) {
  if (controller) {
    controller->SetEditorKilledCallback(std::move(callback));
  }
}

void CloseBoostWindow(
    base::WeakPtr<MahoBoostWindowController> controller) {
  if (controller) {
    controller->FinalizeRendererClose();
  }
}

void CompleteBoostHostClose(
    base::WeakPtr<MahoBoostWindowController> controller) {
  if (controller) {
    controller->CompleteHostClose();
  }
}

void RequestBoostWindowClose(
    base::WeakPtr<MahoBoostWindowController> controller) {
  if (controller) {
    controller->Hide();
  }
}

void SetBoostTemporaryId(
    base::WeakPtr<MahoBoostWindowController> controller,
    std::optional<std::string> boost_id) {
  if (controller) {
    controller->SetTemporaryBoostId(std::move(boost_id));
  }
}

void SetBoostHostCloseState(
    base::WeakPtr<MahoBoostWindowController> controller,
    std::optional<std::string> boost_id,
    bool dirty) {
  if (controller) {
    controller->SetHostCloseState(std::move(boost_id), dirty);
  }
}

void SetBoostWindowMode(
    base::WeakPtr<MahoBoostWindowController> controller,
    maho_boost::mojom::WindowMode mode) {
  if (controller) {
    controller->SetMode(mode);
  }
}

content::WebContents* GetTargetTabForBoost(
    base::WeakPtr<MahoBoostWindowController> controller) {
  return controller ? controller->target_web_contents_.get() : nullptr;
}

MahoBoostWindowController::MahoBoostWindowController(Browser* browser)
    : BrowserUserData<MahoBoostWindowController>(browser),
      browser_(browser),
      profile_(browser ? browser->GetProfile() : nullptr) {
  if (browser_) {
    if (auto* collection = GlobalBrowserCollection::GetInstance()) {
      browser_collection_observation_.Observe(collection);
    }
    TabStripModel* tab_strip = browser_->GetTabStripModel();
    if (tab_strip) {
      observed_tab_strip_ = tab_strip;
      tab_strip->AddObserver(this);
    }
  }
}

MahoBoostWindowController::~MahoBoostWindowController() {
  // Owned by BrowserUserData; this destructor runs when the Browser is
  // destroyed, guaranteeing the controller never outlives its Browser*.
  // Fixes audit finding C4.
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
    content::WebContents* target_tab) {
  if (!target_tab || !browser_) {
    return;
  }

  const GURL& url = target_tab->GetLastCommittedURL();
  std::string domain(url.host());
  if (domain.empty()) {
    return;
  }

  if (widget_) {
    if (!host_close_pending_ && target_web_contents_.get() == target_tab &&
        current_domain_ == domain) {
      ActivateTarget(target_tab);
      widget_->Show();
      widget_->Activate();
      return;
    }
    pending_show_target_ = target_tab->GetWeakPtr();
    RequestHostClose();
    return;
  }

  OpenForTarget(target_tab);
}

void MahoBoostWindowController::OpenForTarget(
    content::WebContents* target_tab) {
  if (!ActivateTarget(target_tab)) {
    return;
  }
  target_web_contents_ = target_tab->GetWeakPtr();
  StartObservingTab(target_tab);
  CreateAndShowWidget(std::string(target_tab->GetLastCommittedURL().host()));
}

bool MahoBoostWindowController::ActivateTarget(
    content::WebContents* target_tab) {
  if (!browser_ || !target_tab) {
    return false;
  }
  TabStripModel* model = browser_->GetTabStripModel();
  if (!model) {
    return false;
  }
  const int index = model->GetIndexOfWebContents(target_tab);
  if (index == TabStripModel::kNoTab) {
    return false;
  }
  if (browser_->GetWindow()) {
    browser_->GetWindow()->Activate();
  }
  if (model->active_index() != index) {
    model->ActivateTabAt(index);
  }
  target_tab->Focus();
  return true;
}

void MahoBoostWindowController::Hide() {
  RequestHostClose();
}

void MahoBoostWindowController::RequestHostClose() {
  if (!widget_ || host_close_pending_) {
    return;
  }
  host_close_pending_ = true;
  StopObservingTab();
  widget_->Hide();
  host_close_watchdog_.Start(
      FROM_HERE, kHostCloseWatchdogTimeout,
      base::BindOnce(&MahoBoostWindowController::ForceHostClose,
                     weak_factory_.GetWeakPtr()));
  if (editor_killed_callback_) {
    editor_killed_callback_.Run();
    return;
  }
  CompleteHostClose();
}

void MahoBoostWindowController::ForceHostClose() {
  if (content::WebContents* target = target_web_contents_.get()) {
    if (auto* handler = MahoBoostInjectionHandler::FromWebContents(target)) {
      handler->ExitCurrentEditMode();
    }
  }
  const std::optional<std::string> selected = selected_boost_id_;
  const std::optional<std::string> temporary = temporary_boost_id_;
  const bool renderer_dirty = renderer_dirty_;
  maho::PostCoreTask<bool>(
      FROM_HERE,
      base::BindOnce(
          [](std::optional<std::string> selected,
             std::optional<std::string> temporary, bool renderer_dirty) {
            MahoCore* core = maho::GetCore();
            if (!core || !selected) {
              return false;
            }
            char* json = maho_core_boost_get(core, selected->c_str());
            if (!json) {
              return false;
            }
            const std::string boost_json(json);
            maho_string_free(json);
            const bool core_dirty =
                boost_json.find("\"changeWasMade\":true") !=
                std::string::npos;
            char* result = nullptr;
            if (renderer_dirty || core_dirty) {
              result = maho_core_boost_commit(core, selected->c_str());
            } else if (temporary == selected) {
              result = maho_core_boost_discard(core, selected->c_str());
            }
            if (result) {
              maho_string_free(result);
              return true;
            }
            return false;
          },
          selected, temporary, renderer_dirty),
      base::BindOnce(
          [](base::WeakPtr<MahoBoostWindowController> controller, bool) {
            if (controller) {
              controller->CompleteHostClose();
            }
          },
          weak_factory_.GetWeakPtr()));
}

void MahoBoostWindowController::SetTemporaryBoostId(
    std::optional<std::string> boost_id) {
  temporary_boost_id_ = std::move(boost_id);
}

void MahoBoostWindowController::SetHostCloseState(
    std::optional<std::string> boost_id,
    bool dirty) {
  selected_boost_id_ = std::move(boost_id);
  renderer_dirty_ = dirty;
}

void MahoBoostWindowController::CompleteHostClose() {
  if (!host_close_pending_) {
    return;
  }
  host_close_pending_ = false;
  host_close_watchdog_.Stop();
  DestroyWidget();
  base::WeakPtr<content::WebContents> pending =
      std::move(pending_show_target_);
  pending_show_target_.reset();
  if (pending) {
    OpenForTarget(pending.get());
  }
}

void MahoBoostWindowController::FinalizeRendererClose() {
  host_close_pending_ = false;
  host_close_watchdog_.Stop();
  pending_show_target_.reset();
  DestroyWidget();
}

bool MahoBoostWindowController::IsShowing() const {
  return widget_ && widget_->IsVisible();
}

content::WebContents*
MahoBoostWindowController::GetEditorWebContentsForTesting() const {
  const auto* delegate =
      static_cast<const MahoBoostWindowDelegate*>(widget_delegate_.get());
  return delegate ? delegate->GetWebContentsForTesting() : nullptr;
}

void MahoBoostWindowController::SetMode(
    maho_boost::mojom::WindowMode mode) {
  if (!widget_ || !browser_) {
    return;
  }

  if (mode == current_mode_) {
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
    code_mode_offset_applied_ = false;
    gfx::Rect parent_bounds;
    if (browser_->GetWindow()) {
      parent_bounds = browser_->GetWindow()->GetBounds();
    }
    int parent_center_x = parent_bounds.x() + parent_bounds.width() / 2;
    if (bounds.right() > parent_center_x) {
      bounds.set_x(bounds.x() - kCodeModeRightAlignOffsetPx);
      code_mode_offset_applied_ = true;
    }
  } else {
    if (code_mode_offset_applied_) {
      // Reverse only the Code-mode offset that was actually applied.
      bounds.set_x(bounds.x() + kCodeModeRightAlignOffsetPx);
      code_mode_offset_applied_ = false;
    }
  }

  bounds.set_width(new_width);
  bounds.set_height(new_height);

  auto* delegate =
      static_cast<MahoBoostWindowDelegate*>(widget_->widget_delegate());
  if (delegate) {
    delegate->SetPreferredDimensions(new_width, new_height);
  }

  widget_->SetBounds(bounds);
}

void MahoBoostWindowController::SetEditorKilledCallback(
    base::RepeatingClosure callback) {
  editor_killed_callback_ = std::move(callback);
}

void MahoBoostWindowController::ScheduleDestroy() {
  if (removal_posted_) {
    return;
  }
  removal_posted_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoBoostWindowController::RemoveFromBrowser, browser_));
}

void MahoBoostWindowController::OnBrowserClosed(
    BrowserWindowInterface* browser) {
  if (!browser || browser != browser_) {
    return;
  }
  ScheduleDestroy();
}

void MahoBoostWindowController::OnTabStripModelChanged(
    TabStripModel* /*tab_strip_model*/,
    const TabStripModelChange& /*change*/,
    const TabStripSelectionChange& selection) {
  if (!widget_ || host_close_pending_) {
    return;
  }
  if (selection.active_tab_changed()) {
    Hide();
  }
}

void MahoBoostWindowController::OnTabStripModelDestroyed(
    TabStripModel* tab_strip_model) {
  if (widget_) {
    RequestHostClose();
  }
  if (tab_strip_model) {
    tab_strip_model->RemoveObserver(this);
  }
  observed_tab_strip_ = nullptr;
  ScheduleDestroy();
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
  const std::string controller_token =
      base::UnguessableToken::Create().ToString();
  url = net::AppendQueryParameter(url, "controller", controller_token);
  controller_token_ = controller_token;

  current_domain_ = domain;
  selected_boost_id_.reset();
  temporary_boost_id_.reset();
  renderer_dirty_ = false;
  editor_killed_callback_.Reset();
  current_mode_ = maho_boost::mojom::WindowMode::kBoost;
  code_mode_offset_applied_ = false;
  GetPendingControllerStorage()[controller_token] = weak_factory_.GetWeakPtr();

  auto contents = std::make_unique<MahoBoostWindowContentsView>(
      profile, url, kBoostModeWidth, kBoostModeHeight);
  auto delegate = std::make_unique<MahoBoostWindowDelegate>(contents.get());
  delegate->SetContentsView(std::move(contents));
  widget_delegate_ = std::move(delegate);

  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW);
  params.delegate = widget_delegate_.get();
  params.bounds = gfx::Rect(0, 0, kBoostModeWidth, kBoostModeHeight);
  params.accept_events = true;
  params.activatable = views::Widget::InitParams::Activatable::kYes;

#if BUILDFLAG(IS_MAC)
  params.remove_standard_frame = true;
#endif

  auto widget = std::make_unique<views::Widget>();
  widget->Init(std::move(params));
  widget->MakeCloseSynchronous(base::BindOnce(
      [](base::WeakPtr<MahoBoostWindowController> controller,
         views::Widget::ClosedReason) {
        if (controller) {
          controller->Hide();
        }
      },
      weak_factory_.GetWeakPtr()));

  widget->SetZOrderLevel(ui::ZOrderLevel::kFloatingWindow);
  widget->CenterWindow(gfx::Size(kBoostModeWidth, kBoostModeHeight));
  widget->Show();
  widget->Activate();

  widget_ = std::move(widget);
}

void MahoBoostWindowController::DestroyWidget() {
  if (!widget_) {
    return;
  }
  auto widget = std::move(widget_);
  auto delegate = std::move(widget_delegate_);
  host_close_watchdog_.Stop();
  editor_killed_callback_.Reset();
  if (!controller_token_.empty()) {
    GetPendingControllerStorage().erase(controller_token_);
    controller_token_.clear();
  }
  current_domain_.clear();
  target_web_contents_.reset();
  current_mode_ = maho_boost::mojom::WindowMode::kBoost;
  code_mode_offset_applied_ = false;
  StopObservingTab();
  widget->CloseNow();
  widget.reset();
  delegate.reset();
}

void MahoBoostWindowController::StartObservingTab(
    content::WebContents* target_tab) {
  StopObservingTab();
  if (target_tab) {
    content::WebContentsObserver::Observe(target_tab);
  }
}

void MahoBoostWindowController::StopObservingTab() {
  content::WebContentsObserver::Observe(nullptr);
}

}  // namespace maho
