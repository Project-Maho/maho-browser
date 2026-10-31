// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_sidebar_folder_hover_controller.h"

#include "base/i18n/rtl.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "ui/display/screen.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {
constexpr base::TimeDelta kHoverShowDelay = base::Milliseconds(225);
constexpr base::TimeDelta kHideGraceMs = base::Milliseconds(150);
constexpr int kPopupAnchorGapDp = 4;
}

MahoSidebarFolderHoverController::MahoSidebarFolderHoverController(Browser* browser)
    : browser_(browser) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

MahoSidebarFolderHoverController::~MahoSidebarFolderHoverController() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  HideImmediately();
}

void MahoSidebarFolderHoverController::ScheduleShow(
    views::View* anchor,
    std::string folder_id,
    std::u16string folder_name,
    std::vector<SidebarTreeNode> children) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (children.empty()) {
    HideImmediately();
    return;
  }
  if (popup_widget_ && open_folder_id_ == folder_id) {
    hide_grace_timer_.Stop();
    return;
  }

  if (popup_widget_) {
    HideImmediately();
  }

  hide_grace_timer_.Stop();
  anchor_tracker_.SetView(anchor);
  pending_folder_id_ = std::move(folder_id);
  pending_folder_name_ = std::move(folder_name);
  pending_children_ = std::move(children);

  show_timer_.Start(FROM_HERE, kHoverShowDelay,
                    base::BindOnce(&MahoSidebarFolderHoverController::ShowNow,
                                   weak_factory_.GetWeakPtr()));
}

void MahoSidebarFolderHoverController::OnRowMouseExited() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  show_timer_.Stop();
  grace_rearm_count_ = 0;
  hide_grace_timer_.Start(FROM_HERE, kHideGraceMs,
                          base::BindOnce(&MahoSidebarFolderHoverController::OnHideGraceTimerFired,
                                         weak_factory_.GetWeakPtr()));
}

void MahoSidebarFolderHoverController::HideImmediately() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  show_timer_.Stop();
  hide_grace_timer_.Stop();
  pending_folder_id_.clear();
  pending_folder_name_.clear();
  pending_children_.clear();
  grace_rearm_count_ = 0;

  if (observed_browser_widget_) {
    observed_browser_widget_->RemoveObserver(this);
    observed_browser_widget_ = nullptr;
  }

  if (browser_ && browser_->GetTabStripModel()) {
    browser_->GetTabStripModel()->RemoveObserver(this);
  }

  if (popup_widget_) {
    views::Widget* widget = popup_widget_;
    popup_widget_ = nullptr;
    open_folder_id_.clear();
    widget->RemoveObserver(this);
    widget->CloseNow();
  }
}

void MahoSidebarFolderHoverController::OnWidgetDestroying(views::Widget* widget) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (widget == observed_browser_widget_) {
    observed_browser_widget_ = nullptr;
  }
  if (widget == popup_widget_) {
    popup_widget_ = nullptr;
    open_folder_id_.clear();
    if (browser_ && browser_->GetTabStripModel()) {
      browser_->GetTabStripModel()->RemoveObserver(this);
    }
  }
}

void MahoSidebarFolderHoverController::OnWidgetActivationChanged(views::Widget* widget, bool active) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (widget == observed_browser_widget_ && !active) {
    HideImmediately();
  }
}

void MahoSidebarFolderHoverController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (selection.active_tab_changed()) {
    HideImmediately();
  }
}

void MahoSidebarFolderHoverController::ShowNow() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  views::View* anchor = anchor_tracker_.view();
  if (!anchor || !anchor->GetWidget()) {
    HideImmediately();
    return;
  }

  // Preserve state before clearing pending params (M1)
  std::u16string folder_name = std::move(pending_folder_name_);
  std::string folder_id = std::move(pending_folder_id_);
  std::vector<SidebarTreeNode> children = std::move(pending_children_);

  // M1: Clear pending parameters to prevent pending state staleness
  pending_folder_id_.clear();
  pending_folder_name_.clear();
  pending_children_.clear();

  views::Widget* widget = MahoSidebarFolderHoverPopupView::Show(
      anchor, browser_, folder_name, folder_id, std::move(children),
      base::BindRepeating(&MahoSidebarFolderHoverController::OnActivateTab,
                          weak_factory_.GetWeakPtr()));

  if (!widget) {
    HideImmediately();
    return;
  }

  popup_widget_ = widget;
  open_folder_id_ = folder_id;
  popup_widget_->AddObserver(this);

  if (anchor && anchor->GetWidget()) {
    observed_browser_widget_ = anchor->GetWidget()->GetTopLevelWidget();
    if (observed_browser_widget_ && observed_browser_widget_ != popup_widget_) {
      observed_browser_widget_->AddObserver(this);
    }
  }

  if (browser_ && browser_->GetTabStripModel()) {
    browser_->GetTabStripModel()->AddObserver(this);
  }
}

void MahoSidebarFolderHoverController::OnHideGraceTimerFired() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (IsCursorInsidePopup() && grace_rearm_count_ < kMaxGraceRearms) {
    ++grace_rearm_count_;
    hide_grace_timer_.Start(FROM_HERE, kHideGraceMs,
                            base::BindOnce(&MahoSidebarFolderHoverController::OnHideGraceTimerFired,
                                           weak_factory_.GetWeakPtr()));
    return;
  }
  HideImmediately();
}

void MahoSidebarFolderHoverController::OnActivateTab(const std::string& tab_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!browser_) {
    HideImmediately();
    return;
  }

  TabStripModel* strip = browser_->GetTabStripModel();
  if (!strip) {
    HideImmediately();
    return;
  }

  int index = -1;
  for (int i = 0; i < strip->count(); ++i) {
    auto* contents = strip->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    auto* helper = MahoTabIdHelper::FromWebContents(contents);
    if (helper && helper->stable_tab_id() == tab_id) {
      index = i;
      break;
    }
  }

  HideImmediately();

  if (index >= 0) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&MahoSidebarFolderHoverController::OnActivateTabDelayed,
                       weak_factory_.GetWeakPtr(), index));
  }
}

void MahoSidebarFolderHoverController::OnActivateTabDelayed(int index) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!browser_) {
    return;
  }
  TabStripModel* strip = browser_->GetTabStripModel();
  if (strip && strip->ContainsIndex(index)) {
    strip->ActivateTabAt(index);
  }
}

bool MahoSidebarFolderHoverController::IsCursorInsidePopup() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!popup_widget_) {
    return false;
  }

  gfx::Point cursor_point = display::Screen::Get()->GetCursorScreenPoint();
  
  gfx::Rect popup_bounds = popup_widget_->GetWindowBoundsInScreen();
  if (base::i18n::IsRTL()) {
    popup_bounds.Inset(gfx::Insets::TLBR(0, 0, 0, -kPopupAnchorGapDp));
  } else {
    popup_bounds.Inset(gfx::Insets::TLBR(0, -kPopupAnchorGapDp, 0, 0));
  }
  if (popup_bounds.Contains(cursor_point)) {
    return true;
  }

  const views::View* anchor = anchor_tracker_.view();
  if (anchor && anchor->GetWidget()) {
    if (anchor->GetBoundsInScreen().Contains(cursor_point)) {
      return true;
    }
  }

  return false;
}

}  // namespace maho
