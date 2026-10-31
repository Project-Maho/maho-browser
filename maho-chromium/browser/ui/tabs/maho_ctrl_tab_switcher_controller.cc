// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/tabs/maho_ctrl_tab_switcher_controller.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/tabs/maho_ctrl_tab_prefs.h"
#include "maho/browser/ui/tabs/maho_ctrl_tab_switcher_view.h"
#include "maho/browser/ui/tabs/maho_mru_tab_tracker.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/types/event_type.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/event_monitor.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {

constexpr size_t kDefaultMaxVisibleCards = 7;
constexpr size_t kMinMaxVisibleCards = 3;
constexpr size_t kMaxMaxVisibleCards = 20;

size_t ClampMaxVisible(int raw) {
  if (raw <= 0) {
    return kDefaultMaxVisibleCards;
  }
  size_t value = static_cast<size_t>(raw);
  if (value < kMinMaxVisibleCards) {
    return kMinMaxVisibleCards;
  }
  if (value > kMaxMaxVisibleCards) {
    return kMaxMaxVisibleCards;
  }
  return value;
}

PrefService* GetPrefsFor(BrowserView* browser_view) {
  if (!browser_view || !browser_view->browser() ||
      !browser_view->browser()->GetProfile()) {
    return nullptr;
  }
  return browser_view->browser()->GetProfile()->GetPrefs();
}

bool ReadMruOrderPref(BrowserView* browser_view) {
  PrefService* prefs = GetPrefsFor(browser_view);
  return prefs ? prefs->GetBoolean(ctrl_tab_prefs::kMruOrder) : true;
}

std::string ReadScopePref(BrowserView* browser_view) {
  PrefService* prefs = GetPrefsFor(browser_view);
  return prefs ? prefs->GetString(ctrl_tab_prefs::kScope)
               : std::string(ctrl_tab_prefs::kScopeCurrentSpace);
}

size_t ReadMaxVisiblePref(BrowserView* browser_view) {
  PrefService* prefs = GetPrefsFor(browser_view);
  return prefs ? ClampMaxVisible(prefs->GetInteger(ctrl_tab_prefs::kMaxVisible))
               : kDefaultMaxVisibleCards;
}

std::vector<base::WeakPtr<content::WebContents>> ToWeakList(
    const std::vector<content::WebContents*>& raw) {
  std::vector<base::WeakPtr<content::WebContents>> out;
  out.reserve(raw.size());
  for (content::WebContents* wc : raw) {
    if (wc) {
      out.push_back(wc->GetWeakPtr());
    }
  }
  return out;
}

std::vector<content::WebContents*> LiveList(
    const std::vector<base::WeakPtr<content::WebContents>>& weak) {
  std::vector<content::WebContents*> out;
  out.reserve(weak.size());
  for (const auto& w : weak) {
    if (w) {
      out.push_back(w.get());
    }
  }
  return out;
}

}  // namespace

MahoCtrlTabSwitcherController::MahoCtrlTabSwitcherController(
    BrowserView* browser_view,
    MahoMruTabTracker* tracker)
    : browser_view_(browser_view), tracker_(tracker) {}

MahoCtrlTabSwitcherController::~MahoCtrlTabSwitcherController() {
  if (browser_view_ && browser_view_->browser() &&
      browser_view_->browser()->GetTabStripModel()) {
    browser_view_->browser()->GetTabStripModel()->RemoveObserver(this);
  }
  StopObservingParentWidget();
  RemoveEventMonitor();
  if (overlay_widget_) {
    overlay_widget_->RemoveObserver(this);
    overlay_widget_.reset();
  }
}

bool MahoCtrlTabSwitcherController::HandleAdvance(bool forward) {
  if (!tracker_ || !browser_view_) {
    return false;
  }
  if (!ReadMruOrderPref(browser_view_)) {
    return false;
  }

  TabStripModel* active_strip =
      browser_view_->browser() ? browser_view_->browser()->GetTabStripModel()
                                : nullptr;
  const int active_index =
      active_strip ? active_strip->active_index() : TabStripModel::kNoTab;
  if (!active_strip || active_index == TabStripModel::kNoTab ||
      !active_strip->GetActiveWebContents()) {
    return false;
  }

  if (state_ != State::kIdle) {
    AdvanceCursor(forward);
    if (state_ == State::kPendingShow) {
      show_timer_.Stop();
      OpenOverlay();
    }
    return true;
  }

  std::vector<content::WebContents*> mru;
  const std::string scope = ReadScopePref(browser_view_);
  if (scope == ctrl_tab_prefs::kScopeCurrentSpace) {
    const std::string& space_id =
        MahoSpaceProfileBridge::GetInstance()->GetActiveSpaceId(
            browser_view_->browser());
    if (space_id.empty()) {
      mru = tracker_->GetMruList();
    } else {
      mru = tracker_->GetMruListForSpace(space_id);
      // No global-fallback: if current space has < 2 tabs, we must NOT
      // cross into other spaces (that would surprise the user by jumping
      // out of the active space, and would fire on blank/zero-tab states
      // even after the early-return above).
    }
  } else {
    mru = tracker_->GetMruList();
  }

  if (mru.size() < 2) {
    return false;
  }

  snapshot_ = ToWeakList(mru);
  forward_ = forward;
  max_visible_ = ReadMaxVisiblePref(browser_view_);
  cursor_ = forward ? 1u : snapshot_.size() - 1;
  active_strip->AddObserver(this);
  InstallEventMonitor();
  OpenOverlay();
  return true;
}

bool MahoCtrlTabSwitcherController::IsOverlayVisibleForTesting() const {
  return overlay_widget_ && overlay_widget_->IsVisible();
}

void MahoCtrlTabSwitcherController::OnEvent(const ui::Event& event) {
  if (event.type() == ui::EventType::kMousePressed) {
    const ui::MouseEvent* mouse = event.AsMouseEvent();
    // On macOS the overlay is shown while Ctrl is held, and Control+click is
    // translated by the OS into a right mouse button press, so accept either
    // primary or secondary button.
    if (mouse_event_monitor_ &&
        (mouse->IsOnlyLeftMouseButton() || mouse->IsOnlyRightMouseButton())) {
      HandleMousePressAt(mouse_event_monitor_->GetLastMouseLocation());
    }
    return;
  }

  if (!event.IsKeyEvent()) {
    return;
  }
  const ui::KeyEvent& key = *event.AsKeyEvent();

  const bool ctrl_flag_gone =
      (key.flags() & (ui::EF_CONTROL_DOWN | ui::EF_COMMAND_DOWN)) == 0;

  const bool is_modifier_key = key.key_code() == ui::VKEY_CONTROL ||
                                key.key_code() == ui::VKEY_LCONTROL ||
                                key.key_code() == ui::VKEY_RCONTROL ||
                                key.key_code() == ui::VKEY_COMMAND ||
                                key.key_code() == ui::VKEY_LWIN ||
                                key.key_code() == ui::VKEY_RWIN;

  if (key.type() == ui::EventType::kKeyReleased && is_modifier_key) {
    CommitCurrent();
    return;
  }

  if (ctrl_flag_gone && state_ != State::kIdle &&
      key.type() == ui::EventType::kKeyReleased) {
    CommitCurrent();
    return;
  }

  if (state_ == State::kShowing &&
      key.type() == ui::EventType::kKeyPressed &&
      key.key_code() == ui::VKEY_ESCAPE) {
    CancelWithoutCommit();
    return;
  }
}

void MahoCtrlTabSwitcherController::OnWidgetDestroying(views::Widget* widget) {
  if (widget == observed_parent_widget_) {
    StopObservingParentWidget();
    return;
  }
  if (widget == overlay_widget_.get()) {
    overlay_widget_->RemoveObserver(this);
    overlay_view_ = nullptr;
    (void)overlay_widget_.release();
  }
}

void MahoCtrlTabSwitcherController::OnWidgetBoundsChanged(
    views::Widget* widget,
    const gfx::Rect& new_bounds) {
  if (widget == observed_parent_widget_ && overlay_widget_) {
    UpdateOverlayBounds();
  }
}

void MahoCtrlTabSwitcherController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (state_ == State::kIdle) {
    return;
  }
  if (change.type() != TabStripModelChange::kRemoved) {
    return;
  }
  const auto* removed = change.GetRemove();
  if (!removed) {
    return;
  }
  for (const auto& delta : removed->contents) {
    for (auto& entry : snapshot_) {
      if (entry && entry.get() == delta.contents) {
        entry.reset();
      }
    }
  }
  size_t live_count = 0;
  for (const auto& entry : snapshot_) {
    if (entry) {
      ++live_count;
    }
  }
  if (live_count < 2) {
    CancelWithoutCommit();
    return;
  }
  if (cursor_ < snapshot_.size() && !snapshot_[cursor_]) {
    AdvanceCursor(forward_);
  }
}

void MahoCtrlTabSwitcherController::StartPendingShow(bool forward) {
  state_ = State::kPendingShow;
  InstallEventMonitor();
  show_timer_.Start(
      FROM_HERE, show_delay_,
      base::BindOnce(&MahoCtrlTabSwitcherController::OnShowTimerFired,
                     weak_factory_.GetWeakPtr()));
}

void MahoCtrlTabSwitcherController::OnShowTimerFired() {
  if (state_ == State::kPendingShow) {
    OpenOverlay();
  }
}

void MahoCtrlTabSwitcherController::OpenOverlay() {
  if (!browser_view_ || snapshot_.empty()) {
    return;
  }

  views::Widget* parent = browser_view_->GetWidget();
  if (!parent) {
    return;
  }

  std::vector<content::WebContents*> live = LiveList(snapshot_);
  if (live.size() < 2) {
    CancelWithoutCommit();
    return;
  }

  auto content_view = std::make_unique<MahoCtrlTabSwitcherView>(
      live, std::min(cursor_, live.size() - 1),
      max_visible_ ? max_visible_ : ReadMaxVisiblePref(browser_view_));

  overlay_widget_ = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params.parent = parent->GetNativeView();
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.activatable = views::Widget::InitParams::Activatable::kNo;
  overlay_widget_->Init(std::move(params));

  overlay_view_ = overlay_widget_->SetContentsView(std::move(content_view));
  overlay_view_->SetHoverCallback(base::BindRepeating(
      &MahoCtrlTabSwitcherController::HandleSlotHovered,
      weak_factory_.GetWeakPtr()));
  overlay_widget_->AddObserver(this);
  ObserveParentWidget();
  UpdateOverlayBounds();
  overlay_widget_->ShowInactive();

  state_ = State::kShowing;
}

size_t MahoCtrlTabSwitcherController::EffectiveSlotCount() const {
  if (snapshot_.empty()) {
    return 0;
  }
  size_t visible = max_visible_ ? max_visible_
                                : (browser_view_
                                       ? ReadMaxVisiblePref(browser_view_)
                                       : kDefaultMaxVisibleCards);
  visible = std::min(visible, snapshot_.size());
  const bool has_more = snapshot_.size() > visible;
  return visible + (has_more ? 1 : 0);
}

void MahoCtrlTabSwitcherController::AdvanceCursor(bool forward) {
  const size_t slot_count = EffectiveSlotCount();
  if (slot_count == 0) {
    return;
  }
  if (forward) {
    cursor_ = (cursor_ + 1) % slot_count;
  } else {
    cursor_ = (cursor_ + slot_count - 1) % slot_count;
  }
  if (overlay_view_) {
    overlay_view_->SetSelectedIndex(cursor_);
  }
}

void MahoCtrlTabSwitcherController::HandleSlotHovered(size_t index) {
  if (state_ == State::kIdle || !overlay_view_) {
    return;
  }
  cursor_ = index;
  overlay_view_->SetSelectedIndex(index);
}

void MahoCtrlTabSwitcherController::HandleMousePressAt(
    const gfx::Point& screen_point) {
  if (state_ == State::kIdle || !overlay_view_) {
    return;
  }
  const std::optional<size_t> slot =
      overlay_view_->SlotIndexAtScreen(screen_point);
  if (!slot.has_value()) {
    return;
  }
  cursor_ = *slot;
  overlay_view_->SetSelectedIndex(*slot);
  // The click arrives from the application-scoped mouse monitor. Committing
  // synchronously would run CloseOverlay() -> destroy the widget (and the
  // view) while the monitor is still dispatching on the stack. Defer to the
  // next loop iteration; the weak ptr is invalidated by Reset() so a stale
  // commit is dropped.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoCtrlTabSwitcherController::CommitCurrent,
                     weak_factory_.GetWeakPtr()));
}

void MahoCtrlTabSwitcherController::CommitCurrent() {
  if (state_ == State::kIdle) {
    return;
  }

  const bool is_more_slot =
      overlay_view_ && overlay_view_->has_more_affordance() &&
      cursor_ == overlay_view_->more_affordance_index();

  base::WeakPtr<content::WebContents> target;
  if (!is_more_slot && cursor_ < snapshot_.size()) {
    target = snapshot_[cursor_];
  }

  // Close overlay and reset state BEFORE activating the tab. ActivateTabAt
  // fires TabStripModel observers synchronously, and any re-entry through
  // HandleAdvance would otherwise operate on stale state or snapshot.
  BrowserView* view = browser_view_;
  CloseOverlay();
  Reset();

  if (is_more_slot) {
    if (view) {
      view->ShowMahoCommandOverlayForSearch(std::string());
    }
    return;
  }

  if (!target || !view) {
    return;
  }
  TabStripModel* strip =
      view->browser() ? view->browser()->GetTabStripModel() : nullptr;
  if (!strip) {
    return;
  }
  const int index = strip->GetIndexOfWebContents(target.get());
  if (index == TabStripModel::kNoTab) {
    return;
  }
  strip->ActivateTabAt(
      index, TabStripUserGestureDetails(
                 TabStripUserGestureDetails::GestureType::kKeyboard));
}

void MahoCtrlTabSwitcherController::CancelWithoutCommit() {
  CloseOverlay();
  Reset();
}

void MahoCtrlTabSwitcherController::CloseOverlay() {
  show_timer_.Stop();
  StopObservingParentWidget();
  if (overlay_widget_) {
    overlay_widget_->RemoveObserver(this);
    overlay_view_ = nullptr;
    overlay_widget_.reset();
  }
}

void MahoCtrlTabSwitcherController::Reset() {
  if (browser_view_ && browser_view_->browser() &&
      browser_view_->browser()->GetTabStripModel()) {
    browser_view_->browser()->GetTabStripModel()->RemoveObserver(this);
  }
  RemoveEventMonitor();
  weak_factory_.InvalidateWeakPtrs();
  snapshot_.clear();
  cursor_ = 0;
  max_visible_ = 0;
  state_ = State::kIdle;
}

void MahoCtrlTabSwitcherController::InstallEventMonitor() {
  if (event_monitor_ || !browser_view_ || !browser_view_->GetWidget()) {
    return;
  }
  event_monitor_ = views::EventMonitor::CreateWindowMonitor(
      this, browser_view_->GetWidget()->GetNativeWindow(),
      {ui::EventType::kKeyPressed, ui::EventType::kKeyReleased});
  // A window monitor cannot observe clicks on the overlay: the overlay is its
  // own (child) window, so a mouseDown targets that window, not the parent.
  // Use an application-scoped monitor so the press is seen process-wide and
  // hit-tested against the overlay geometry in OnEvent.
  mouse_event_monitor_ = views::EventMonitor::CreateApplicationMonitor(
      this, browser_view_->GetWidget()->GetNativeWindow(),
      {ui::EventType::kMousePressed});
}

void MahoCtrlTabSwitcherController::RemoveEventMonitor() {
  event_monitor_.reset();
  mouse_event_monitor_.reset();
}

void MahoCtrlTabSwitcherController::UpdateOverlayBounds() {
  if (!overlay_widget_ || !overlay_view_ || !browser_view_) {
    return;
  }
  views::Widget* parent = browser_view_->GetWidget();
  if (!parent) {
    return;
  }
  const gfx::Size preferred = overlay_view_->GetPreferredSize();
  const gfx::Rect parent_bounds = parent->GetClientAreaBoundsInScreen();
  const int x = parent_bounds.x() +
                std::max(0, (parent_bounds.width() - preferred.width()) / 2);
  const int y = parent_bounds.y() +
                std::max(0, (parent_bounds.height() - preferred.height()) / 2);
  overlay_widget_->SetBounds(
      gfx::Rect(x, y, preferred.width(), preferred.height()));
}

void MahoCtrlTabSwitcherController::ObserveParentWidget() {
  if (observed_parent_widget_ || !browser_view_) {
    return;
  }
  views::Widget* parent = browser_view_->GetWidget();
  if (!parent) {
    return;
  }
  observed_parent_widget_ = parent;
  parent->AddObserver(this);
}

void MahoCtrlTabSwitcherController::StopObservingParentWidget() {
  if (observed_parent_widget_) {
    observed_parent_widget_->RemoveObserver(this);
    observed_parent_widget_ = nullptr;
  }
}

}  // namespace maho
