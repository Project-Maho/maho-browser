// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/tabs/maho_ctrl_tab_switcher_controller.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/single_thread_task_runner.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/tabs/maho_ctrl_tab_prefs.h"
#include "maho/browser/ui/tabs/maho_ctrl_tab_slot_model.h"
#include "maho/browser/ui/tabs/maho_ctrl_tab_switcher_view.h"
#include "maho/browser/ui/tabs/maho_mru_tab_tracker.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/types/event_type.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/event_monitor.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {

constexpr size_t kDefaultMaxVisibleCards = 7;
constexpr size_t kMinMaxVisibleCards = 3;
constexpr size_t kMaxMaxVisibleCards = 20;
// Minimum gap kept between the overlay and the browser window's edges.
constexpr int kOverlayWindowMargin = 16;

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

  // Two candidates: flip to the other one immediately, no overlay.
  if (mru.size() == 2) {
    const int index = active_strip->GetIndexOfWebContents(mru[1]);
    if (index == TabStripModel::kNoTab) {
      return false;
    }
    active_strip->ActivateTabAt(
        index, TabStripUserGestureDetails(
                   TabStripUserGestureDetails::GestureType::kKeyboard));
    return true;
  }

  snapshot_ = ToWeakList(mru);
  forward_ = forward;
  FitCardsToWindow();
  // The first press carries a pending quick-release commit: forward targets
  // MRU[1], backward the least-recently-used tab (MRU wrap-to-end, like
  // Alt+Tab's Shift direction). Both targets can share an index with the
  // "+N more" slot - backward when exactly one tab is hidden, forward
  // whenever only one card is visible (index 1 IS the more slot then) - so
  // the commit intent is tracked explicitly instead of inferred from the
  // cursor index. See maho_ctrl_tab_slot_model.h.
  cursor_ = ctrl_tab_slots::InitialCursor(forward, snapshot_.size());
  pending_mru_index_ = cursor_;
  active_strip->AddObserver(this);
  // A quick Ctrl+Tab (released before the delay) commits without ever
  // flashing the overlay; holding Ctrl or pressing Tab again shows it.
  StartPendingShow(forward);
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
    // primary or secondary button there. Elsewhere a right-click is a
    // context-menu gesture and must not commit a switch.
#if BUILDFLAG(IS_MAC)
    const bool is_select_button =
        mouse->IsOnlyLeftMouseButton() || mouse->IsOnlyRightMouseButton();
#else
    const bool is_select_button = mouse->IsOnlyLeftMouseButton();
#endif
    if (mouse_event_monitor_ && is_select_button) {
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

  if (state_ != State::kIdle &&
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
  if (widget != observed_parent_widget_ || !overlay_widget_) {
    return;
  }
  // The window may have shrunk below the overlay's width: refit the visible
  // cards and their width so the overlay never overflows the window. No mouse
  // press is involved here (keyboard fullscreen toggle, display change, tiling
  // WM), so the outside-press cancel cannot protect the cursor: it keeps its
  // meaning across the refit through ClampCursorAfterRefit() - it stays on the
  // "+N more" slot only if it was already there, otherwise it moves to the last
  // real card. Clamping to EffectiveSlotCount() - 1 would land it on the more
  // slot and the next Ctrl release would open the command palette instead of
  // activating a tab - the same convention OnTabStripModelChanged follows.
  const bool cursor_on_more_slot =
      !pending_mru_index_ && HasMoreSlot() && cursor_ == VisibleCardCount();
  const size_t previous_cards = max_visible_;
  const int previous_card_width = card_width_;
  FitCardsToWindow();
  if (max_visible_ != previous_cards || card_width_ != previous_card_width) {
    cursor_ = ClampCursorAfterRefit(cursor_on_more_slot);
    RebuildOverlayView();
    return;
  }
  UpdateOverlayBounds();
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

  // Drop closed tabs from the snapshot so cursor indices, the visible cards
  // and the committed target always refer to live tabs. The cursor keeps
  // pointing at the same tab, or at the tab that slid into a closed slot.
  const bool cursor_on_more_slot =
      !pending_mru_index_ && HasMoreSlot() && cursor_ == VisibleCardCount();
  size_t new_cursor = cursor_;
  std::vector<base::WeakPtr<content::WebContents>> live;
  live.reserve(snapshot_.size());
  for (size_t i = 0; i < snapshot_.size(); ++i) {
    if (snapshot_[i]) {
      live.push_back(snapshot_[i]);
    } else if (i < cursor_) {
      --new_cursor;
    }
  }
  snapshot_ = std::move(live);
  if (snapshot_.size() < 2) {
    CancelWithoutCommit();
    return;
  }
  cursor_ = new_cursor;
  cursor_ = ClampCursorAfterRefit(cursor_on_more_slot);
  if (pending_mru_index_) {
    // The pending commit tracks the same entry the cursor did: it follows
    // the shrunken snapshot and never exceeds the last live index.
    pending_mru_index_ =
        ctrl_tab_slots::PendingCommitIndex(cursor_, snapshot_.size());
  }
  RebuildOverlayView();
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

  if (LiveList(snapshot_).size() < 2) {
    CancelWithoutCommit();
    return;
  }

  // The overlay can only highlight a card in its slot ring. A quick backward
  // press parks the cursor on the last snapshot entry (wrap-to-end commit), so
  // bring it into the ring before the cards are built: the highlighted slot and
  // the tab a Ctrl release commits must be the same.
  NormalizeCursorToSlots();

  auto content_view = CreateOverlayView();

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

std::unique_ptr<MahoCtrlTabSwitcherView>
MahoCtrlTabSwitcherController::CreateOverlayView() const {
  std::vector<content::WebContents*> live = LiveList(snapshot_);
  const size_t slot_count = EffectiveSlotCount();
  return std::make_unique<MahoCtrlTabSwitcherView>(
      live, slot_count ? std::min(cursor_, slot_count - 1) : 0,
      max_visible_ ? max_visible_ : ReadMaxVisiblePref(browser_view_),
      card_width_);
}

void MahoCtrlTabSwitcherController::RebuildOverlayView() {
  if (!overlay_widget_ || !overlay_view_) {
    return;
  }
  // Replacing the contents view destroys the old one (and its stale cards).
  overlay_view_ = overlay_widget_->SetContentsView(CreateOverlayView());
  overlay_view_->SetHoverCallback(base::BindRepeating(
      &MahoCtrlTabSwitcherController::HandleSlotHovered,
      weak_factory_.GetWeakPtr()));
  UpdateOverlayBounds();
}

void MahoCtrlTabSwitcherController::FitCardsToWindow() {
  const size_t preferred = ReadMaxVisiblePref(browser_view_);
  views::Widget* parent = browser_view_ ? browser_view_->GetWidget() : nullptr;
  if (!parent) {
    max_visible_ = preferred;
    card_width_ = 0;
    return;
  }
  const int available_width =
      parent->GetClientAreaBoundsInScreen().width() - 2 * kOverlayWindowMargin;
  const MahoCtrlTabSwitcherView::Layout layout =
      MahoCtrlTabSwitcherView::FitToWidth(available_width, snapshot_.size(),
                                          preferred);
  max_visible_ = layout.visible_cards;
  card_width_ = layout.card_width;
}

size_t MahoCtrlTabSwitcherController::ClampCursorAfterRefit(
    bool was_on_more_slot) const {
  return ctrl_tab_slots::ResolveCursorAfterRefit(
      cursor_, snapshot_.size(), CurrentSlotModel(), was_on_more_slot,
      pending_mru_index_.has_value());
}

ctrl_tab_slots::SlotModel MahoCtrlTabSwitcherController::CurrentSlotModel()
    const {
  return ctrl_tab_slots::SlotModel{VisibleCardCount(), HasMoreSlot()};
}

size_t MahoCtrlTabSwitcherController::VisibleCardCount() const {
  const size_t visible =
      max_visible_ ? max_visible_
                   : (browser_view_ ? ReadMaxVisiblePref(browser_view_)
                                    : kDefaultMaxVisibleCards);
  return std::min(visible, snapshot_.size());
}

bool MahoCtrlTabSwitcherController::HasMoreSlot() const {
  return snapshot_.size() > VisibleCardCount();
}

size_t MahoCtrlTabSwitcherController::EffectiveSlotCount() const {
  if (snapshot_.empty()) {
    return 0;
  }
  return VisibleCardCount() + (HasMoreSlot() ? 1 : 0);
}

void MahoCtrlTabSwitcherController::AdvanceCursor(bool forward) {
  const size_t slot_count = EffectiveSlotCount();
  if (slot_count == 0) {
    return;
  }
  if (pending_mru_index_) {
    // The commit follows the cursor from the first Tab step on.
    pending_mru_index_.reset();
    if (!forward_) {
      // A pending backward commit sits on the last snapshot entry, which the
      // slot ring cannot address. Enter the ring at the last visible card
      // and let this press be the one that brings the selection into view,
      // so no card between the two is skipped.
      NormalizeCursorToSlots();
      if (overlay_view_) {
        overlay_view_->SetSelectedIndex(cursor_);
      }
      return;
    }
    // A pending forward commit already names a ring slot; the press steps
    // the ring normally.
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

void MahoCtrlTabSwitcherController::NormalizeCursorToSlots() {
  // Only a cursor that does not yet name the user's selection needs
  // normalizing: the pending initial cursor (a backward wrap-to-end target
  // past the ring, or the forward initial cursor colliding with the "+N
  // more" slot at a single visible card), or any cursor left past the ring.
  // A cursor deliberately stepped or hovered onto the "+N more" slot stays:
  // releasing Ctrl there intentionally opens the command palette.
  if (!pending_mru_index_ && cursor_ < EffectiveSlotCount()) {
    return;
  }
  if (ctrl_tab_slots::NeedsRingNormalization(cursor_, CurrentSlotModel())) {
    // Never the "+N more" slot: releasing Ctrl there would open the command
    // palette instead of activating a tab.
    const size_t visible = VisibleCardCount();
    cursor_ = visible ? visible - 1 : 0;
  }
  // The ring cursor now names the commit target by itself.
  pending_mru_index_.reset();
}

void MahoCtrlTabSwitcherController::HandleSlotHovered(size_t index) {
  if (state_ == State::kIdle || !overlay_view_) {
    return;
  }
  cursor_ = index;
  pending_mru_index_.reset();
  overlay_view_->SetSelectedIndex(index);
}

void MahoCtrlTabSwitcherController::HandleMousePressAt(
    const gfx::Point& screen_point) {
  if (state_ == State::kIdle) {
    return;
  }
  // A press outside the overlay (web page, sidebar, another window) means the
  // user moved on: end the session without switching tabs, so the later Ctrl
  // release does not commit a selection the user never confirmed. Deferred
  // for the same reentrancy reason as the commit below.
  if (!overlay_view_ || !overlay_widget_ ||
      !overlay_widget_->GetWindowBoundsInScreen().Contains(screen_point)) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&MahoCtrlTabSwitcherController::CancelWithoutCommit,
                       weak_factory_.GetWeakPtr()));
    return;
  }
  const std::optional<size_t> slot =
      overlay_view_->SlotIndexAtScreen(screen_point);
  if (!slot.has_value()) {
    return;
  }
  cursor_ = *slot;
  pending_mru_index_.reset();
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

  // Derived from the snapshot, not the view: during the pending-show window
  // no overlay view exists yet. A pending quick-release commit names its MRU
  // entry directly: with a single visible card the forward target (index 1)
  // collides with the "+N more" slot index, and mapping it through the slot
  // ring would open the command palette instead of switching tabs.
  base::WeakPtr<content::WebContents> target;
  bool open_more_slot = false;
  if (pending_mru_index_) {
    if (!snapshot_.empty()) {
      target = snapshot_[ctrl_tab_slots::PendingCommitIndex(
          *pending_mru_index_, snapshot_.size())];
    }
  } else {
    open_more_slot = HasMoreSlot() && cursor_ == VisibleCardCount();
    if (!open_more_slot && !snapshot_.empty()) {
      target = snapshot_[std::min(cursor_, snapshot_.size() - 1)];
    }
  }

  // Close overlay and reset state BEFORE activating the tab. ActivateTabAt
  // fires TabStripModel observers synchronously, and any re-entry through
  // HandleAdvance would otherwise operate on stale state or snapshot.
  BrowserView* view = browser_view_;
  CloseOverlay();
  Reset();

  if (open_more_slot) {
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
  pending_mru_index_.reset();
  max_visible_ = 0;
  card_width_ = 0;
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
  // Card count and card width are already fitted to the window
  // (FitCardsToWindow); clamp the final rect to the window and the display
  // work area so a window partly off-screen still keeps the overlay visible.
  const int width = std::min(preferred.width(), parent_bounds.width());
  const int height = std::min(preferred.height(), parent_bounds.height());
  gfx::Rect bounds(parent_bounds.x() + (parent_bounds.width() - width) / 2,
                   parent_bounds.y() + (parent_bounds.height() - height) / 2,
                   width, height);
  if (display::Screen* screen = display::Screen::Get()) {
    bounds.AdjustToFit(screen->GetDisplayMatching(parent_bounds).work_area());
  }
  overlay_widget_->SetBounds(bounds);
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
