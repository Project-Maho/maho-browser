// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_agent_tasks_view.h"

#include <algorithm>
#include <memory>
#include <ranges>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "components/sessions/content/session_tab_helper.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_agent_tasks_policy.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"

namespace maho {
namespace {

std::u16string StatusText(AgentTaskStatus status) {
  switch (status) {
    case AgentTaskStatus::kRunning:
      return u"Running";
    case AgentTaskStatus::kNeedsYou:
      return u"Needs you";
    case AgentTaskStatus::kPaused:
      return u"Paused";
    case AgentTaskStatus::kDisconnected:
      return u"Disconnected";
    case AgentTaskStatus::kDone:
      return u"Done";
    case AgentTaskStatus::kFailed:
      return u"Failed";
  }
}

}  // namespace

MahoSidebarAgentTasksView::MahoSidebarAgentTasksView(
    Browser* browser,
    base::RepeatingCallback<void(int64_t)> activate_tab_callback)
    : browser_(browser),
      activate_tab_callback_(std::move(activate_tab_callback)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto header = std::make_unique<views::LabelButton>(
      base::BindRepeating(
          [](base::WeakPtr<MahoSidebarAgentTasksView> self) {
            if (!self) {
              return;
            }
            self->expanded_ = !self->expanded_;
            self->RebuildRows(false);
          },
          weak_factory_.GetWeakPtr()),
      u"Tasks");
  header->SetAccessibleName(u"Tasks");
  header_ = AddChildView(std::move(header));
  rows_container_ = AddChildView(std::make_unique<views::View>());
  auto* rows_layout = rows_container_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  rows_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  service_ = browser_ ? ai::MahoControlActivityService::GetForProfile(
                            browser_->GetProfile())
                      : nullptr;
  if (service_) {
    for (auto& activity : service_->GetActivities()) {
      activities_.insert_or_assign(activity.session_id, std::move(activity));
    }
    service_->AddObserver(this);
  }
  RebuildRows(false);
}

MahoSidebarAgentTasksView::~MahoSidebarAgentTasksView() {
  if (service_) {
    service_->RemoveObserver(this);
  }
}

void MahoSidebarAgentTasksView::OnControlActivityChanged(
    const ai::ControlActivity& activity) {
  auto existing = activities_.find(activity.session_id);
  if (existing != activities_.end() &&
      activity.observer_revision <= existing->second.observer_revision) {
    return;
  }
  activities_.insert_or_assign(activity.session_id, activity);
  RebuildRows(true);
}

void MahoSidebarAgentTasksView::RebuildRows(bool expand_for_attention) {
  std::vector<ai::ControlActivity> activities;
  activities.reserve(activities_.size());
  for (const auto& [session_id, activity] : activities_) {
    activities.push_back(activity);
  }
  rows_ = BuildAgentTaskRows(activities, base::TimeTicks::Now());
  const bool has_attention = std::ranges::any_of(
      rows_, [](const AgentTaskRow& row) { return row.attention; });
  if (expand_for_attention && has_attention) {
    expanded_ = true;
  }
  const AgentTasksSectionState state =
      ComputeAgentTasksSectionState(rows_, expanded_);
  SetVisible(state.visible);
  rows_container_->SetVisible(state.expanded);
  rows_container_->RemoveAllChildViews();

  auto* header = views::AsViewClass<views::LabelButton>(header_);
  header->SetText(state.expanded ? u"Tasks (expanded)" : u"Tasks (collapsed)");
  for (const AgentTaskRow& row : rows_) {
    auto row_view = std::make_unique<views::View>();
    auto* row_layout = row_view->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal,
            gfx::Insets::VH(2, 4), 4));
    row_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    std::u16string accessible_name =
        StatusText(row.status) + u": " + row.title;
    if (!row.last_receipt_summary.empty()) {
      accessible_name += u" - " +
                         base::UTF8ToUTF16(row.last_receipt_summary);
    }
    auto activate = std::make_unique<views::LabelButton>(
        base::BindRepeating(&MahoSidebarAgentTasksView::ActivateRow,
                            weak_factory_.GetWeakPtr(), row),
        StatusText(row.status) + u": " + row.title);
    activate->SetAccessibleName(accessible_name);
    activate->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    activate->SetElideBehavior(gfx::ELIDE_TAIL);
    auto* activate_button = row_view->AddChildView(std::move(activate));
    row_layout->SetFlexForView(activate_button, 1);

    if (!row.last_receipt_summary.empty()) {
      auto receipt = std::make_unique<views::Label>(
          base::UTF8ToUTF16(row.last_receipt_summary));
      receipt->SetAccessibleName(u"Last update: " +
                                 base::UTF8ToUTF16(row.last_receipt_summary));
      receipt->SetElideBehavior(gfx::ELIDE_TAIL);
      row_view->AddChildView(std::move(receipt));
    }

    if (CanDisconnectAgentTask(row)) {
      auto disconnect = std::make_unique<views::LabelButton>(
          base::BindRepeating(&MahoSidebarAgentTasksView::DisconnectRow,
                              weak_factory_.GetWeakPtr(), row),
          u"Disconnect");
      disconnect->SetAccessibleName(u"Disconnect " + row.title);
      row_view->AddChildView(std::move(disconnect));
    }
    rows_container_->AddChildView(std::move(row_view));
  }
}

void MahoSidebarAgentTasksView::ActivateRow(const AgentTaskRow& row) {
  if (!browser_ || row.focus_tab_id <= 0 || activate_tab_callback_.is_null()) {
    return;
  }
  TabStripModel* tab_strip = browser_->GetTabStripModel();
  for (int index = 0; tab_strip && index < tab_strip->count(); ++index) {
    content::WebContents* contents = tab_strip->GetWebContentsAt(index);
    auto* helper =
        contents ? sessions::SessionTabHelper::FromWebContents(contents)
                 : nullptr;
    if (helper && helper->session_id().id() == row.focus_tab_id) {
      activate_tab_callback_.Run(row.focus_tab_id);
      return;
    }
  }
}

void MahoSidebarAgentTasksView::DisconnectRow(const AgentTaskRow& row) {
  if (service_ && CanDisconnectAgentTask(row)) {
    service_->RequestImmediateStop(row.focus_tab_id);
  }
}

BEGIN_METADATA(MahoSidebarAgentTasksView)
END_METADATA

}  // namespace maho
