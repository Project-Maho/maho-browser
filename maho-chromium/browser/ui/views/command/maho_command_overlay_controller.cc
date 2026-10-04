// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"

#include "chrome/browser/ui/views/frame/browser_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/spaces_overlay/maho_browser_frame_overlay_host.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/metrics/histogram_macros.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_private_context_policy.h"
#include "maho/browser/ui/views/command/maho_command_overlay_view.h"
#include "maho/browser/ui/views/command/maho_command_overlay_vibrancy.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/accelerators/accelerator_manager.h"
#include "ui/compositor/layer.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/event_monitor.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"

namespace maho {

// Intentional product divergences from Zen Browser design (preserved by design):
// 1. Standalone overlay architecture: Maho uses a views::Widget-based popup
//    instead of being a urlbar provider / omnibox replacement.
// 2. Explicit open modes and mode-based disposition: kCurrentTab, kNewTab,
//    and kSearch stay distinct.
// 3. Persistent Search / Ask Maho selector and @prefix site-search.
// 4. Typed-text URL/domain/search fallback.
// 5. Calculator / unit-conversion clipboard copy behavior.
// 6. Pre-show focus restore and 200ms deactivation hide.

namespace {

constexpr base::TimeDelta kFadeOutDuration = base::Milliseconds(180);
constexpr base::TimeDelta kResizeDuration = base::Milliseconds(200);
constexpr base::TimeDelta kDeactivationHideDelay = base::Milliseconds(200);

constexpr int kPopupWidthDp = 744;
constexpr int kPopupInitialHeightDp = 232;
constexpr int kPopupMinHeightDp = 48;
constexpr int kPopupMinTopMarginDp = 62;
constexpr int kPopupMinBottomMarginDp = 32;

gfx::Rect GetWorkAreaForParentWidget(views::Widget* parent_widget,
                                     const gfx::Rect& parent_bounds) {
  display::Screen* screen = display::Screen::Get();
  if (!screen) {
    return parent_bounds;
  }

  const display::Display display = screen->GetDisplayMatching(parent_bounds);
  const gfx::Rect work_area = display.work_area();
  return work_area.IsEmpty() ? parent_bounds : work_area;
}

int ClampPopupY(int target_y, int height, const gfx::Rect& work_area) {
  const int min_y = work_area.y() + kPopupMinTopMarginDp;
  const int max_y = work_area.bottom() - kPopupMinBottomMarginDp - height;
  if (max_y >= min_y) {
    return std::clamp(target_y, min_y, max_y);
  }

  return std::clamp(target_y, work_area.y(), work_area.bottom() - height);
}

gfx::Rect ComputeCenteredPopupBounds(const gfx::Rect& parent_bounds,
                                     const gfx::Rect& work_area,
                                     int height) {
  const int x = parent_bounds.x() +
                (parent_bounds.width() - kPopupWidthDp) / 2;
  const int target_y = parent_bounds.y() +
                       (parent_bounds.height() - height) / 2;
  return gfx::Rect(x, ClampPopupY(target_y, height, work_area), kPopupWidthDp,
                   height);
}

}  // namespace

MahoCommandOverlayController::MahoCommandOverlayController(Browser* browser)
    : browser_(browser) {
  if (browser_ && browser_->GetTabStripModel()) {
    browser_->GetTabStripModel()->AddObserver(this);
  }
  resize_animation_.SetSlideDuration(kResizeDuration);
  resize_animation_.SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN);
}

// static
bool MahoCommandOverlayController::ShouldCreateForContext(
    MahoPrivateContextClass klass) {
  return klass == MahoPrivateContextClass::kRegular ||
         klass == MahoPrivateContextClass::kPrimaryIncognito;
}

MahoCommandOverlayController::~MahoCommandOverlayController() {
  Observe(nullptr);
  if (browser_ && browser_->GetTabStripModel()) {
    browser_->GetTabStripModel()->RemoveObserver(this);
  }
  event_monitor_.reset();
  UnregisterParentEscAccelerator();
  if (parent_widget_) {
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
  }
  if (widget_) {
    widget_->RemoveObserver(this);
    widget_.reset();
  }
  closing_widget_.reset();
  anchor_ = nullptr;
}

void MahoCommandOverlayController::Toggle(views::View* anchor,
                                         CommandOverlayMode mode) {
  if (IsVisible()) {
    Hide();
  } else {
    Show(anchor, mode);
  }
}

void MahoCommandOverlayController::Hide() {
  if (!widget_ || is_hiding_)
    return;

  deactivation_hide_timer_.Stop();
  is_hiding_ = true;
  Observe(nullptr);

  event_monitor_.reset();
  UnregisterParentEscAccelerator();

  RestoreFocusToPreShowView();

  if (parent_widget_) {
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
  }
  anchor_ = nullptr;

  auto* root_view = widget_->GetRootView();
  if (!root_view || !root_view->layer()) {
    widget_->RemoveObserver(this);
    widget_.reset();
    is_hiding_ = false;
    return;
  }

  views::AnimationBuilder()
      .OnEnded(base::BindOnce(
          &MahoCommandOverlayController::FinishHideAnimation,
          weak_factory_.GetWeakPtr()))
      .OnAborted(base::BindOnce(
          &MahoCommandOverlayController::FinishHideAnimation,
          weak_factory_.GetWeakPtr()))
      .Once()
      .SetDuration(kFadeOutDuration)
      .SetOpacity(root_view->layer(), 0.0f, gfx::Tween::EASE_IN)
      .SetTransform(root_view->layer(),
                    gfx::Transform::MakeTranslation(0, -6),
                    gfx::Tween::EASE_IN);
}

bool MahoCommandOverlayController::IsVisible() const {
  return widget_ && widget_->IsVisible() && !is_hiding_;
}

void MahoCommandOverlayController::FinishHideAnimation() {
  if (!widget_)
    return;

  Observe(nullptr);
  widget_->RemoveObserver(this);
  widget_->Hide();
  is_hiding_ = false;
  overlay_view_ = nullptr;
  // Retain ownership in `closing_widget_` instead of handing the widget to a
  // posted task: at browser shutdown the message loop stops before such a task
  // can run, which strands the widget and its entire View tree, leaking the
  // Views and their AXPlatformNodes. The controller is owned by BrowserView, so
  // keeping the widget here guarantees it is destroyed with the window even if
  // the follow-up task never runs.
  closing_widget_ = std::move(widget_);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoCommandOverlayController::DestroyClosingWidget,
                     weak_factory_.GetWeakPtr()));
}

void MahoCommandOverlayController::DestroyClosingWidget() {
  closing_widget_.reset();
}

void MahoCommandOverlayController::RestoreFocusToPreShowView() {
  views::View* saved = pre_show_focused_view_.view();
  pre_show_focused_view_.SetView(nullptr);
  if (!saved || !parent_widget_)
    return;
  views::FocusManager* fm = parent_widget_->GetFocusManager();
  if (fm && fm->ContainsView(saved))
    fm->SetFocusedView(saved);
}

void MahoCommandOverlayController::FocusOverlayTextfield() {
  if (!overlay_view_ || !widget_ || !widget_->IsVisible()) {
    return;
  }
  overlay_view_->RequestFocus();
  if (auto* focus_manager = widget_->GetFocusManager()) {
    if (!focus_manager->GetFocusedView()) {
      focus_manager->SetKeyboardAccessible(true);
      focus_manager->AdvanceFocus(false);
    }
  }
}

void MahoCommandOverlayController::OnWidgetDestroying(
    views::Widget* widget) {
  if (widget == parent_widget_) {
    event_monitor_.reset();
    // The parent focus manager is tearing down and will drop our accelerator;
    // just forget it rather than calling Unregister on a dying manager.
    esc_target_focus_manager_ = nullptr;
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
    anchor_ = nullptr;
    pre_show_focused_view_.SetView(nullptr);
  }
  if (widget == widget_.get()) {
    weak_factory_.InvalidateWeakPtrs();
    deactivation_hide_timer_.Stop();
    event_monitor_.reset();
    UnregisterParentEscAccelerator();
    is_hiding_ = false;
    overlay_view_ = nullptr;
    anchor_ = nullptr;
    RestoreFocusToPreShowView();
    if (parent_widget_) {
      parent_widget_->RemoveObserver(this);
      parent_widget_ = nullptr;
    }
    // The widget is created WIDGET_OWNS_NATIVE_WIDGET, so nothing else deletes
    // the views::Widget when its native widget goes away: releasing ownership
    // here abandoned the Widget together with its whole RootView tree, leaking
    // the Views and their AXPlatformNodes. Take the Widget into
    // `closing_widget_` instead and drop it once this notification has unwound;
    // holding it on the controller also guarantees destruction at window
    // teardown if that task never runs.
    closing_widget_ = std::move(widget_);
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&MahoCommandOverlayController::DestroyClosingWidget,
                       widget_teardown_weak_factory_.GetWeakPtr()));
  }
}

void MahoCommandOverlayController::OnWidgetBoundsChanged(
    views::Widget* widget,
    const gfx::Rect& new_bounds) {
  if (widget == parent_widget_ && widget_.get()) {
    Reposition();
  }
}

void MahoCommandOverlayController::OnWidgetActivationChanged(
    views::Widget* widget,
    bool active) {
  if (handling_activation_change_) {
    return;
  }
  base::AutoReset<bool> activation_guard(&handling_activation_change_, true);

  if (widget == parent_widget_) {
    if (active && widget_ && widget_->IsVisible() && !is_hiding_) {
      widget_->Activate();
      FocusOverlayTextfield();
    }
    return;
  }
  if (widget != widget_.get()) {
    return;
  }

  if (active) {
    FocusOverlayTextfield();
    deactivation_hide_timer_.Stop();
    return;
  }

  deactivation_hide_timer_.Start(
      FROM_HERE, kDeactivationHideDelay,
      base::BindOnce(&MahoCommandOverlayController::MaybeHideAfterDeactivation,
                     weak_factory_.GetWeakPtr()));
}

void MahoCommandOverlayController::MaybeHideAfterDeactivation() {
  if (!widget_ || is_hiding_ || widget_->IsActive()) {
    return;
  }
  // Keep the palette open when the search-engine picker steals activation.
  if (overlay_view_ && overlay_view_->IsPickerOpen()) {
    return;
  }
  // Keep the palette open when another application (not another Chrome window)
  // holds focus. GetActiveBrowser() is null iff no Chrome browser window is the
  // active window, which is exactly the "switched apps" case.
  auto* collection = GlobalBrowserCollection::GetInstance();
  if (!treat_chrome_window_active_for_testing_ &&
      (!collection || !collection->GetActiveBrowser())) {
    return;
  }
  // Ignore spurious activation churn during the open transition, but reschedule
  // instead of dropping so a genuine early click still dismisses.
  const base::TimeDelta since_open = base::TimeTicks::Now() - open_start_ticks_;
  constexpr base::TimeDelta kOpenGrace = base::Milliseconds(500);
  if (since_open < kOpenGrace) {
    deactivation_hide_timer_.Start(
        FROM_HERE, kOpenGrace - since_open,
        base::BindOnce(
            &MahoCommandOverlayController::MaybeHideAfterDeactivation,
            weak_factory_.GetWeakPtr()));
    return;
  }
  Hide();
}

void MahoCommandOverlayController::AnimationProgressed(
    const gfx::Animation* animation) {
  if (!widget_ || animation != &resize_animation_)
    return;

  const gfx::Rect bounds = gfx::Tween::RectValueBetween(
      resize_animation_.GetCurrentValue(), animation_start_bounds_,
      animation_target_bounds_);
  if (!bounds.IsEmpty()) {
    widget_->SetBounds(bounds);
  }
}

void MahoCommandOverlayController::AnimationEnded(
    const gfx::Animation* animation) {
  if (!widget_ || animation != &resize_animation_)
    return;

  if (!animation_target_bounds_.IsEmpty()) {
    widget_->SetBounds(animation_target_bounds_);
  }
}

gfx::Rect MahoCommandOverlayController::ComputePopupBounds() const {
  if (!parent_widget_)
    return gfx::Rect();

  const gfx::Rect parent_bounds = parent_widget_->GetWindowBoundsInScreen();
  const gfx::Rect work_area =
      GetWorkAreaForParentWidget(parent_widget_, parent_bounds);
  return ComputeCenteredPopupBounds(parent_bounds, work_area,
                                    kPopupInitialHeightDp);
}

gfx::Rect MahoCommandOverlayController::ComputeBoundsForHeight(int height) const {
  if (!parent_widget_)
    return gfx::Rect();

  const gfx::Rect parent_bounds = parent_widget_->GetWindowBoundsInScreen();
  if (parent_bounds.IsEmpty())
    return gfx::Rect();

  height = std::max(height, kPopupMinHeightDp);

  const gfx::Rect work_area =
      GetWorkAreaForParentWidget(parent_widget_, parent_bounds);
  const int x = parent_bounds.x() +
                (parent_bounds.width() - kPopupWidthDp) / 2;
  const int target_y = widget_
                           ? widget_->GetWindowBoundsInScreen().y()
                           : parent_bounds.y() +
                                 (parent_bounds.height() - height) / 2;
  return gfx::Rect(x, ClampPopupY(target_y, height, work_area), kPopupWidthDp,
                   height);
}

void MahoCommandOverlayController::Reposition() {
  if (!widget_ || !parent_widget_)
    return;

  const gfx::Rect parent_bounds = parent_widget_->GetWindowBoundsInScreen();
  if (parent_bounds.IsEmpty())
    return;

  const gfx::Rect work_area =
      GetWorkAreaForParentWidget(parent_widget_, parent_bounds);
  int current_height = widget_->GetWindowBoundsInScreen().height();
  current_height = std::max(current_height, kPopupMinHeightDp);
  gfx::Rect bounds = ComputeCenteredPopupBounds(
      parent_bounds, work_area, current_height);
  if (!bounds.IsEmpty()) {
    widget_->SetBounds(bounds);
  }
}

void MahoCommandOverlayController::AnimateBoundsToHeight(int height) {
  if (!widget_)
    return;

  height = std::max(height, kPopupMinHeightDp);

  gfx::Rect target_bounds = ComputeBoundsForHeight(height);
  if (target_bounds.IsEmpty())
    return;

  gfx::Rect current_bounds = widget_->GetWindowBoundsInScreen();
  if (current_bounds.IsEmpty()) {
    widget_->SetBounds(target_bounds);
    return;
  }
  if (current_bounds == target_bounds)
    return;

  animation_start_bounds_ = current_bounds;
  animation_target_bounds_ = target_bounds;
  resize_animation_.Reset(0.0);
  resize_animation_.Show();
}

void MahoCommandOverlayController::Show(views::View* anchor,
                                         CommandOverlayMode mode,
                                         const std::string& initial_text,
                                         bool select_initial_text) {
  DCHECK(anchor);
  DCHECK(anchor->GetWidget());
  if (!anchor || !anchor->GetWidget())
    return;

  if (browser_ && !ShouldCreateForContext(
                      MahoClassifyProfile(browser_->GetProfile()))) {
    return;
  }

  if (browser_) {
    BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
    auto* container =
        browser_view
            ? views::AsViewClass<MahoSidebarContainerView>(
                  browser_view->maho_sidebar_container())
            : nullptr;
    if (container) {
      if (auto* host = container->GetOrCreateOverlayHost()) {
        host->DismissAllOverlaysExcept(
            MahoBrowserFrameOverlayHost::OverlayKind::kCommand);
      }
    }
  }

  open_start_ticks_ = base::TimeTicks::Now();

  event_monitor_.reset();
  UnregisterParentEscAccelerator();

  if (widget_) {
    widget_->RemoveObserver(this);
    widget_.reset();
  }
  if (parent_widget_) {
    parent_widget_->RemoveObserver(this);
    parent_widget_ = nullptr;
  }
  is_hiding_ = false;

  anchor_ = anchor;
  parent_widget_ = anchor->GetWidget();
  parent_widget_->AddObserver(this);

  views::FocusManager* parent_focus_manager = parent_widget_->GetFocusManager();
  pre_show_focused_view_.SetView(
      parent_focus_manager ? parent_focus_manager->GetFocusedView() : nullptr);

  auto dismiss_cb = base::BindOnce(
      [](base::WeakPtr<MahoCommandOverlayController> self) {
        if (self)
          self->Hide();
      },
      weak_factory_.GetWeakPtr());
  auto resize_cb = base::BindRepeating(
      [](base::WeakPtr<MahoCommandOverlayController> self, int height) {
        if (self)
          self->AnimateBoundsToHeight(height);
      },
      weak_factory_.GetWeakPtr());

  std::unique_ptr<MahoCommandOverlayView> overlay_view;
  if (browser_) {
    overlay_view = std::make_unique<MahoCommandOverlayView>(
        browser_, mode, initial_text, select_initial_text,
        std::move(dismiss_cb), std::move(resize_cb));
  } else {
    overlay_view = std::make_unique<MahoCommandOverlayView>(
        MahoCommandOverlayView::ForTestingTag{}, mode, initial_text,
        select_initial_text, std::move(dismiss_cb), std::move(resize_cb));
  }

  auto* overlay_ptr = overlay_view.get();

  views::Widget::InitParams params(
      views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params.parent = parent_widget_->GetNativeView();
  params.context = parent_widget_->GetNativeWindow();
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.bounds = ComputePopupBounds();
  params.accept_events = true;
  params.activatable = views::Widget::InitParams::Activatable::kYes;
  params.shadow_type = views::Widget::InitParams::ShadowType::kDrop;

  auto widget = std::make_unique<views::Widget>();
  auto* widget_ptr = widget.get();
  widget_ptr->Init(std::move(params));
  widget_ptr->SetContentsView(std::move(overlay_view));
  widget_ptr->AddObserver(this);

  auto* root_view = widget_ptr->GetRootView();
  if (root_view) {
    root_view->SetPaintToLayer();
    root_view->layer()->SetFillsBoundsOpaquely(false);
  }

  gfx::Size preferred = overlay_ptr->GetPreferredSize();
  const int initial_height = std::max(preferred.height(), kPopupMinHeightDp);
  gfx::Rect bounds = ComputeBoundsForHeight(initial_height);
  if (bounds.IsEmpty()) {
    const gfx::Rect parent_bounds = parent_widget_->GetWindowBoundsInScreen();
    const gfx::Rect work_area =
        GetWorkAreaForParentWidget(parent_widget_, parent_bounds);
    bounds = ComputeCenteredPopupBounds(
        parent_bounds.IsEmpty() ? work_area : parent_bounds,
        work_area, initial_height);
  }
  if (!bounds.IsEmpty()) {
    widget_ptr->SetBounds(bounds);
    animation_start_bounds_ = bounds;
    animation_target_bounds_ = bounds;
  }
  if (auto* root = widget_ptr->GetRootView()) {
    root->DeprecatedLayoutImmediately();
  }

  widget_ = std::move(widget);
  overlay_view_ = overlay_ptr;

  ApplyVibrancyToCommandOverlay(widget_ptr);
  widget_ptr->Show();
  widget_ptr->Activate();
  {
    const base::TimeDelta open_latency =
        base::TimeTicks::Now() - open_start_ticks_;
    TRACE_EVENT_INSTANT("ui", "MahoCommandPalette.OpenToFirstPaint",
                        "latency_ms", open_latency.InMilliseconds());
    UMA_HISTOGRAM_TIMES("Maho.CommandPalette.OpenLatency", open_latency);
  }

  StartOutsideClickMonitor();
  RegisterParentEscAccelerator();

  if (browser_ && browser_->GetTabStripModel()) {
    Observe(browser_->GetTabStripModel()->GetActiveWebContents());
  }

  // RequestFocus() is posted rather than called synchronously so that the
  // platform's native window activation (triggered by Activate() above) has
  // a chance to be processed before the view-system focus is set.  Without
  // this, the FocusManager may set focused_view_ before the native window
  // owns OS focus, which causes the first key event dispatched via
  // SendKeyPressSync to be routed to the wrong window.
  FocusOverlayTextfield();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoCommandOverlayController::FocusOverlayTextfield,
                     weak_factory_.GetWeakPtr()));
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&MahoCommandOverlayController::FocusOverlayTextfield,
                     weak_factory_.GetWeakPtr()),
      base::Milliseconds(50));
}

void MahoCommandOverlayController::OnEvent(const ui::Event& event) {
  if (!IsVisible() || !event_monitor_ || !widget_) {
    return;
  }
  if (event.type() != ui::EventType::kMousePressed &&
      event.type() != ui::EventType::kTouchPressed) {
    return;
  }
  const gfx::Point screen_point = event_monitor_->GetLastMouseLocation();
  if (widget_->GetWindowBoundsInScreen().Contains(screen_point)) {
    return;
  }
  if (overlay_view_ && overlay_view_->PickerContainsScreenPoint(screen_point)) {
    return;
  }
  Hide();
}

bool MahoCommandOverlayController::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  if (accelerator.key_code() == ui::VKEY_ESCAPE && IsVisible()) {
    Hide();
    return true;
  }
  return false;
}

bool MahoCommandOverlayController::CanHandleAccelerators() const {
  return IsVisible();
}

void MahoCommandOverlayController::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInPrimaryMainFrame() &&
      navigation_handle->HasCommitted() &&
      !navigation_handle->IsSameDocument()) {
    if (IsVisible()) {
      Hide();
    }
  }
}

void MahoCommandOverlayController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed()) {
    if (IsVisible()) {
      Hide();
    }
    Observe(selection.new_contents);
  }
}

void MahoCommandOverlayController::OnTabStripModelDestroyed(
    TabStripModel* tab_strip_model) {
  tab_strip_model->RemoveObserver(this);
}

void MahoCommandOverlayController::OnWillChangeFocus(
    views::View* focused_before,
    views::View* focused_now) {}

void MahoCommandOverlayController::OnDidChangeFocus(
    views::View* focused_before,
    views::View* focused_now) {
  if (!IsVisible() || !focused_now || !widget_) {
    return;
  }
  if (focused_now->GetWidget() == widget_.get()) {
    return;
  }
  if (overlay_view_ && overlay_view_->IsPickerOpen()) {
    return;
  }
  Hide();
}

void MahoCommandOverlayController::OnFocusManagerDestroying(
    views::FocusManager* focus_manager) {
  if (focus_manager == esc_target_focus_manager_) {
    focus_manager->RemoveFocusChangeListener(this);
    focus_manager->UnregisterAccelerator(
        ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE), this);
    esc_target_focus_manager_ = nullptr;
  }
}

void MahoCommandOverlayController::StartOutsideClickMonitor() {
  if (!parent_widget_) {
    return;
  }
  event_monitor_ = views::EventMonitor::CreateApplicationMonitor(
      this, parent_widget_->GetNativeWindow(),
      {ui::EventType::kMousePressed, ui::EventType::kTouchPressed});
}

void MahoCommandOverlayController::RegisterParentEscAccelerator() {
  if (esc_target_focus_manager_ || !parent_widget_) {
    return;
  }
  views::FocusManager* focus_manager = parent_widget_->GetFocusManager();
  if (!focus_manager) {
    return;
  }
  esc_target_focus_manager_ = focus_manager;
  focus_manager->RegisterAccelerator(
      ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE),
      ui::AcceleratorManager::kHighPriority, this);
  focus_manager->AddFocusChangeListener(this);
}

void MahoCommandOverlayController::UnregisterParentEscAccelerator() {
  if (!esc_target_focus_manager_) {
    return;
  }
  esc_target_focus_manager_->RemoveFocusChangeListener(this);
  esc_target_focus_manager_->UnregisterAccelerator(
      ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE), this);
  esc_target_focus_manager_ = nullptr;
}

}  // namespace maho
