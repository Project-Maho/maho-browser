// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_AGENT_TASKS_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_AGENT_TASKS_VIEW_H_

#include <map>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "maho/browser/ai/maho_control_activity_service.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_agent_task_model.h"
#include "ui/views/view.h"

class Browser;

namespace maho {

class MahoSidebarAgentTasksView
    : public views::View,
      public ai::MahoControlActivityService::Observer {
 public:
  MahoSidebarAgentTasksView(
      Browser* browser,
      base::RepeatingCallback<void(int64_t)> activate_tab_callback);
  MahoSidebarAgentTasksView(const MahoSidebarAgentTasksView&) = delete;
  MahoSidebarAgentTasksView& operator=(const MahoSidebarAgentTasksView&) =
      delete;
  ~MahoSidebarAgentTasksView() override;

  void OnControlActivityChanged(const ai::ControlActivity& activity) override;

 private:
  void RebuildRows(bool expand_for_attention);
  void ActivateRow(const AgentTaskRow& row);
  void DisconnectRow(const AgentTaskRow& row);

  raw_ptr<Browser> browser_;
  base::RepeatingCallback<void(int64_t)> activate_tab_callback_;
  raw_ptr<ai::MahoControlActivityService> service_ = nullptr;
  raw_ptr<views::View> header_ = nullptr;
  raw_ptr<views::View> rows_container_ = nullptr;
  std::map<std::string, ai::ControlActivity, std::less<>> activities_;
  std::vector<AgentTaskRow> rows_;
  bool expanded_ = false;
  base::WeakPtrFactory<MahoSidebarAgentTasksView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_AGENT_TASKS_VIEW_H_
